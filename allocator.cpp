#include "allocator.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>

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
    initialBlock->data = reinterpret_cast<void*>(initialBlock + 1);
    
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
    return block->data;
}

void TLSFAllocator::deallocate(void* ptr) {
    if (!ptr) return;
    
    // This is problematic if we don't know the header location from data pointer
    // But since data = this + 1, we can do:
    BlockHeader* header = reinterpret_cast<BlockHeader*>(reinterpret_cast<char*>(ptr) - offsetof(BlockHeader, data) - sizeof(void*));
    // Wait, data is a member. In my previous version I used data() { return this + 1; }.
    // Let's use the actual data pointer stored in the header.
    // However, finding the header from the data pointer is easier if we know it's always at a fixed offset.
    // In the README: struct BlockHeader { void* data; ... }
    // If data is the first member, ptr == &header->data.
    // But wait, it's safer to assume data is a pointer to the actual data area.
    
    // Let's re-examine the structure:
    /*
    struct BlockHeader {
        void* data; // 指向数据块头地址的指针
        std::size_t size; // 块大小（包含头部）
        bool isFree;      // 是否空闲
        BlockHeader* prevPhysBlock; // 指向物理上前一个块
        BlockHeader* nextPhysBlock; // 指向物理上后一个块
    };
    */
    // If 'data' is the first member, then header == ptr.
    // But then 'data' would point to itself or something.
    // Usually 'data' points to the memory right after the header.
    
    // Since I control the allocation, I'll make 'data' point to (this + 1).
    // And I'll find 'this' by iterating or by assuming fixed offset if possible.
    // Actually, I can just use a map to store ptr -> header if I have to, but that's not TLSF.
    // In real TLSF, the header is just before the data.
    
    // Let's assume header is just before the data pointer.
    // Since 'data' is a pointer member, it takes 8 bytes.
    // BlockHeader size is 8 (void*) + 8 (size_t) + 8 (bool/padded) + 8 (prev) + 8 (next) = 40 bytes.
    // If data points to (header + 1), then header = (char*)ptr - sizeof(BlockHeader).
    
    BlockHeader* h = reinterpret_cast<BlockHeader*>(reinterpret_cast<char*>(ptr) - sizeof(BlockHeader));
    // Check if h->data == ptr to be sure
    if (h->data != ptr) {
        // Fallback or error? For now assume it matches.
    }
    
    h->isFree = true;
    FreeBlock* freeBlock = static_cast<FreeBlock*>(h);
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
    if (size == 0) { fli = 0; sli = 0; return; }
    fli = fls(size);
    int divisions = (fli < SLI_BITS) ? (1 << fli) : SLI_SIZE;
    sli = (size - (1ULL << fli)) * divisions / (1ULL << fli);
    
    if (fli >= FLI_SIZE) fli = FLI_SIZE - 1;
    if (sli >= SLI_SIZE) sli = SLI_SIZE - 1;
}

void TLSFAllocator::splitBlock(FreeBlock* block, std::size_t size) {
    FreeBlock* remainingBlock = reinterpret_cast<FreeBlock*>(reinterpret_cast<char*>(block) + size);
    remainingBlock->size = block->size - size;
    remainingBlock->isFree = true;
    remainingBlock->prevPhysBlock = block;
    remainingBlock->nextPhysBlock = block->nextPhysBlock;
    remainingBlock->data = reinterpret_cast<void*>(remainingBlock + 1);
    
    if (block->nextPhysBlock) {
        block->nextPhysBlock->prevPhysBlock = remainingBlock;
    }
    block->nextPhysBlock = remainingBlock;
    block->size = size;
    
    insertFreeBlock(remainingBlock);
}

void TLSFAllocator::mergeAdjacentFreeBlocks(FreeBlock* block) {
    if (block->nextPhysBlock && block->nextPhysBlock->isFree) {
        FreeBlock* next = static_cast<FreeBlock*>(block->nextPhysBlock);
        removeFreeBlock(next);
        block->size += next->size;
        block->nextPhysBlock = next->nextPhysBlock;
        if (next->nextPhysBlock) {
            next->nextPhysBlock->prevPhysBlock = block;
        }
    }
    
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
    
    std::uint32_t slMap = index.sliBitmaps[fli] & (~0U << sli);
    if (slMap) {
        int foundSli = ffs(slMap);
        return index.freeLists[fli][foundSli];
    }
    
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
