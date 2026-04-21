#include "allocator.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iostream>

TLSFAllocator::TLSFAllocator(std::size_t memoryPoolSize) : poolSize(memoryPoolSize) {
    memoryPool = std::malloc(poolSize);
    std::memset(memoryPool, 0, poolSize);
    
    // Initialize index
    for (int i = 0; i < FLI_SIZE; ++i) {
        for (int j = 0; j < SLI_SIZE; ++j) {
            index.freeLists[i][j] = nullptr;
        }
        index.sliBitmaps[i] = 0;
    }
    index.fliBitmap = 0;
    
    initializeMemoryPool(poolSize);
}

TLSFAllocator::~TLSFAllocator() {
    std::free(memoryPool);
}

void TLSFAllocator::initializeMemoryPool(std::size_t size) {
    if (size < sizeof(FreeBlock)) return;
    
    FreeBlock* initialBlock = reinterpret_cast<FreeBlock*>(memoryPool);
    initialBlock->size = size;
    initialBlock->isFree = true;
    initialBlock->prevPhysBlock = nullptr;
    initialBlock->nextPhysBlock = nullptr;
    initialBlock->prevFree = nullptr;
    initialBlock->nextFree = nullptr;
    
    insertFreeBlock(initialBlock);
}

void* TLSFAllocator::allocate(std::size_t size) {
    // Total size needed including header
    std::size_t totalSize = size + sizeof(BlockHeader);
    // Align to 8 bytes
    totalSize = (totalSize + 7) & ~7;
    
    if (totalSize < sizeof(FreeBlock)) totalSize = sizeof(FreeBlock);

    FreeBlock* block = findSuitableBlock(totalSize);
    if (!block) return nullptr;
    
    removeFreeBlock(block);
    
    if (block->size >= totalSize + sizeof(FreeBlock)) {
        splitBlock(block, totalSize);
    }
    
    block->isFree = false;
    return block->data();
}

void TLSFAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    
    BlockHeader* header = reinterpret_cast<BlockHeader*>(reinterpret_cast<char*>(ptr) - sizeof(BlockHeader));
    header->isFree = true;
    
    FreeBlock* freeBlock = static_cast<FreeBlock*>(header);
    freeBlock->prevFree = nullptr;
    freeBlock->nextFree = nullptr;
    
    mergeAdjacentFreeBlocks(freeBlock);
}

void* TLSFAllocator::getMemoryPoolStart() const {
    return memoryPool;
}

std::size_t TLSFAllocator::getMemoryPoolSize() const {
    return poolSize;
}

std::size_t TLSFAllocator::getMaxAvailableBlockSize() const {
    std::size_t maxSize = 0;
    for (int i = FLI_SIZE - 1; i >= 0; --i) {
        if (index.fliBitmap & (1U << i)) {
            for (int j = SLI_SIZE - 1; j >= 0; --j) {
                if (index.sliBitmaps[i] & (1U << j)) {
                    FreeBlock* curr = index.freeLists[i][j];
                    while (curr) {
                        if (curr->size > maxSize) maxSize = curr->size;
                        curr = curr->nextFree;
                    }
                }
            }
        }
    }
    return (maxSize >= sizeof(BlockHeader)) ? (maxSize - sizeof(BlockHeader)) : 0;
}

void TLSFAllocator::mappingFunction(std::size_t size, int& fli, int& sli) {
    fli = fls(size);
    if (fli < SLI_BITS) {
        fli = fli; // Not really useful but keeps the structure
        sli = size >> (fli - fli); // Should be linear?
        // Actually, for size < 16, it's very special.
        // Let's stick to the formula provided in README:
        // SLI = ((size - (1 << FLI)) / ((1 << FLI) / divisions))
    }
    
    int divisions = (fli < SLI_BITS) ? (1 << fli) : SLI_SIZE;
    if (fli < SLI_BITS) {
        // Linear mapping for small sizes
        sli = size - (1ULL << fli);
    } else {
        sli = (size - (1ULL << fli)) >> (fli - SLI_BITS);
    }
    
    // Bounds check
    if (fli >= FLI_SIZE) fli = FLI_SIZE - 1;
    if (sli >= SLI_SIZE) sli = SLI_SIZE - 1;
}

void TLSFAllocator::splitBlock(FreeBlock* block, std::size_t size) {
    FreeBlock* remainingBlock = reinterpret_cast<FreeBlock*>(reinterpret_cast<char*>(block) + size);
    remainingBlock->size = block->size - size;
    remainingBlock->isFree = true;
    remainingBlock->prevPhysBlock = block;
    remainingBlock->nextPhysBlock = block->nextPhysBlock;
    
    if (block->nextPhysBlock) {
        block->nextPhysBlock->prevPhysBlock = remainingBlock;
    }
    block->nextPhysBlock = remainingBlock;
    block->size = size;
    
    insertFreeBlock(remainingBlock);
}

void TLSFAllocator::mergeAdjacentFreeBlocks(FreeBlock* block) {
    // Merge with next
    if (block->nextPhysBlock && block->nextPhysBlock->isFree) {
        FreeBlock* next = static_cast<FreeBlock*>(block->nextPhysBlock);
        removeFreeBlock(next);
        block->size += next->size;
        block->nextPhysBlock = next->nextPhysBlock;
        if (next->nextPhysBlock) {
            next->nextPhysBlock->prevPhysBlock = block;
        }
    }
    
    // Merge with prev
    if (block->prevPhysBlock && block->prevPhysBlock->isFree) {
        FreeBlock* prev = static_cast<FreeBlock*>(block->prevPhysBlock);
        removeFreeBlock(prev);
        prev->size += block->size;
        prev->nextPhysBlock = block->nextPhysBlock;
        if (block->nextPhysBlock) {
            block->nextPhysBlock->prevPhysBlock = prev;
        }
        block = prev;
    }
    
    insertFreeBlock(block);
}

TLSFAllocator::FreeBlock* TLSFAllocator::findSuitableBlock(std::size_t size) {
    int fli, sli;
    mappingFunction(size, fli, sli);
    
    // Search in current FLI
    std::uint32_t slMap = index.sliBitmaps[fli] & (~0U << sli);
    if (slMap) {
        int foundSli = ffs(slMap);
        return index.freeLists[fli][foundSli];
    }
    
    // Search in higher FLI
    std::uint32_t flMap = index.fliBitmap & (~0U << (fli + 1));
    if (flMap) {
        int foundFli = ffs(flMap);
        int foundSli = ffs(index.sliBitmaps[foundFli]);
        return index.freeLists[foundFli][foundSli];
    }
    
    return nullptr;
}

void TLSFAllocator::insertFreeBlock(FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    
    block->nextFree = index.freeLists[fli][sli];
    block->prevFree = nullptr;
    if (index.freeLists[fli][sli]) {
        index.freeLists[fli][sli]->prevFree = block;
    }
    index.freeLists[fli][sli] = block;
    
    index.fliBitmap |= (1U << fli);
    index.sliBitmaps[fli] |= (1U << sli);
}

void TLSFAllocator::removeFreeBlock(FreeBlock* block) {
    int fli, sli;
    mappingFunction(block->size, fli, sli);
    
    if (block->prevFree) {
        block->prevFree->nextFree = block->nextFree;
    } else {
        index.freeLists[fli][sli] = block->nextFree;
    }
    
    if (block->nextFree) {
        block->nextFree->prevFree = block->prevFree;
    }
    
    if (index.freeLists[fli][sli] == nullptr) {
        index.sliBitmaps[fli] &= ~(1U << sli);
        if (index.sliBitmaps[fli] == 0) {
            index.fliBitmap &= ~(1U << fli);
        }
    }
}
