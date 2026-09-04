#pragma once
#include <vector>
#include <memory>
#include <cstddef>
#include <algorithm>
#include <type_traits>
#include <string>
#include <string_view>

namespace shell_lite {


class Arena {
public:
    static constexpr size_t DEFAULT_ARENA_CHUNK_SIZE = 64 * 1024;

    explicit Arena(size_t chunk_size = DEFAULT_ARENA_CHUNK_SIZE) : chunk_size_(chunk_size) {
        alloc_chunk();
    }


    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    template <typename T, typename... Args>
    T* emplace(Args&&... args) {
        size_t size = sizeof(T);
        size_t align = alignof(T);


        void* ptr = static_cast<void*>(current_ptr_);
        size_t space = remaining_capacity();

        if (!std::align(align, size, ptr, space)) {
            alloc_chunk();
            ptr = static_cast<void*>(current_ptr_);
            space = remaining_capacity();
            std::align(align, size, ptr, space);
        }

        T* result = new (ptr) T(std::forward<Args>(args)...);
        current_ptr_ = static_cast<std::byte*>(ptr) + size;

        if constexpr (!std::is_trivially_destructible_v<T>) {
            cleanups_.push_back({result, [](void* p) { static_cast<T*>(p)->~T(); }});
        }

        return result;
    }

    std::string_view emplace_string(const std::string& str) {
        size_t size = str.size();
        if (remaining_capacity() < size + 1) alloc_chunk((std::max)(chunk_size_, size + 1));
        char* dest = reinterpret_cast<char*>(current_ptr_);
        std::copy(str.begin(), str.end(), dest);
        dest[size] = '\0';
        std::string_view view(dest, size);
        current_ptr_ += size + 1;
        return view;
    }

    ~Arena() {
        for (auto it = cleanups_.rbegin(); it != cleanups_.rend(); ++it) {
            it->destroy(it->ptr);
        }
        for (auto& chunk : chunks_) {
            delete[] chunk;
        }
    }

private:
    struct Cleanup {
        void* ptr;
        void (*destroy)(void*);
    };

    void alloc_chunk(size_t min_capacity = 0) {
        size_t cap = (std::max)(chunk_size_, min_capacity);
        auto* chunk = new std::byte[cap];
        chunks_.push_back(chunk);
        current_ptr_ = chunk;
        end_ptr_ = chunk + cap;
        chunk_size_ = (std::min)(chunk_size_ * 2, static_cast<size_t>(1024 * 1024));
    }

    size_t remaining_capacity() const {
        return end_ptr_ - current_ptr_;
    }

    size_t chunk_size_;
    std::vector<std::byte*> chunks_;
    std::byte* current_ptr_ = nullptr;
    std::byte* end_ptr_ = nullptr;
    std::vector<Cleanup> cleanups_;
};

}
