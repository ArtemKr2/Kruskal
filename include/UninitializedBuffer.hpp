#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>

namespace kruskal::detail {

    // Буфер без ініціалізації елементів

    template<typename T>
    concept TriviallyStorable = std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>;

    template<TriviallyStorable T>
    class UninitializedBuffer {
    public:
        explicit UninitializedBuffer(std::size_t n) : alloc_(), data_(alloc_.allocate(n)), size_(n) {}
        ~UninitializedBuffer() { alloc_.deallocate(data_, size_); }
        UninitializedBuffer(const UninitializedBuffer&) = delete;
        UninitializedBuffer& operator=(const UninitializedBuffer&) = delete;

        [[nodiscard]] std::span<T> span() noexcept { return { data_, size_ }; }

    private:
        std::allocator<T> alloc_;
        T* data_;
        std::size_t size_;
    };

} // namespace kruskal::detail
