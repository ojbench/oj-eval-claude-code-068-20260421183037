#include <iostream>
#include <vector>
#include <string>
#include <map>
#include "allocator.hpp"

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    
    std::size_t poolSize;
    if (!(std::cin >> poolSize)) return 0;
    
    TLSFAllocator allocator(poolSize);
    
    std::string op;
    std::map<int, void*> ptrs;
    
    while (std::cin >> op) {
        if (op == "alloc") {
            int id;
            std::size_t size;
            std::cin >> id >> size;
            void* ptr = allocator.allocate(size);
            if (ptr) {
                ptrs[id] = ptr;
                std::cout << "success" << "\n";
            } else {
                std::cout << "failed" << "\n";
            }
        } else if (op == "free") {
            int id;
            std::cin >> id;
            if (ptrs.count(id)) {
                allocator.deallocate(ptrs[id]);
                ptrs.erase(id);
                std::cout << "success" << "\n";
            } else {
                std::cout << "failed" << "\n";
            }
        } else if (op == "max") {
            std::cout << allocator.getMaxAvailableBlockSize() << "\n";
        }
    }
    
    return 0;
}
