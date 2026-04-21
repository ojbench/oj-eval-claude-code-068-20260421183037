#include <iostream>
#include <vector>
#include <string>
#include "allocator.hpp"

int main() {
    std::size_t poolSize;
    if (!(std::cin >> poolSize)) return 0;
    
    TLSFAllocator allocator(poolSize);
    
    int numOps;
    if (!(std::cin >> numOps)) return 0;
    
    std::vector<void*> ptrs;
    
    for (int i = 0; i < numOps; ++i) {
        std::string op;
        std::cin >> op;
        if (op == "alloc") {
            std::size_t size;
            std::cin >> size;
            void* ptr = allocator.allocate(size);
            if (ptr) {
                ptrs.push_back(ptr);
                std::cout << ptrs.size() - 1 << std::endl;
            } else {
                std::cout << -1 << std::endl;
            }
        } else if (op == "free") {
            int index;
            std::cin >> index;
            if (index >= 0 && index < ptrs.size() && ptrs[index] != nullptr) {
                allocator.deallocate(ptrs[index]);
                ptrs[index] = nullptr;
                std::cout << "success" << std::endl;
            } else {
                std::cout << "failed" << std::endl;
            }
        } else if (op == "max") {
            std::cout << allocator.getMaxAvailableBlockSize() << std::endl;
        }
    }
    
    return 0;
}
