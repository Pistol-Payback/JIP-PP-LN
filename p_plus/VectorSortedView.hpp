// VectorSortedView.h
// A container that stores objects contiguously for cache locality
// and maintains a separately sorted view via pointers or indices.

#pragma once

#include <vector>
#include <algorithm>
#include <cstddef>
#include <span>

template <typename T, typename Compare = std::less<T>>
class VectorSortedView {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;
    using comparator_type = Compare;

    static_assert(std::is_invocable_r_v<bool, Compare, const T&, const T&>, "VectorSortedView requires Compare to be callable as `bool cmp(const T&, const T&)`");

    // Constructors / Destructors
    VectorSortedView()
        : storage_(nullptr), view_(nullptr), size_(0), capacity_(0), comp_() {}

    explicit VectorSortedView(const Compare& comp)
        : storage_(nullptr), view_(nullptr), size_(0), capacity_(0), comp_(comp) {}

    ~VectorSortedView() {
        clear();
        ::operator delete[](storage_);
        delete[] view_;
    }

    // Disable copy (can be added as needed)
    VectorSortedView(const VectorSortedView&) = delete;
    VectorSortedView& operator=(const VectorSortedView&) = delete;

    // Move semantics
    VectorSortedView(VectorSortedView&& other) noexcept
        : storage_(other.storage_), view_(other.view_)
        , size_(other.size_), capacity_(other.capacity_), comp_(std::move(other.comp_))
    {
        other.storage_ = nullptr;
        other.view_ = nullptr;
        other.size_ = other.capacity_ = 0;
    }

    VectorSortedView& operator=(VectorSortedView&& other) noexcept {
        if (this != &other) {
            clear();
            ::operator delete[](storage_);
            delete[] view_;

            storage_ = other.storage_;
            view_ = other.view_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            comp_ = std::move(other.comp_);

            other.storage_ = nullptr;
            other.view_ = nullptr;
            other.size_ = other.capacity_ = 0;
        }
        return *this;
    }

    // Reserve capacity
    void reserve(size_type newCap) {
        if (newCap <= capacity_) return;
        // Allocate raw storage and view arrays
        T* newStorage = static_cast<T*>(::operator new[](sizeof(T)* newCap));
        T** newView = new T * [newCap];

        // Move-construct existing elements
        for (size_type i = 0; i < size_; ++i) {
            new (&newStorage[i]) T(std::move(storage_[i]));
            newView[i] = &newStorage[i];
            storage_[i].~T();
        }

        // Delete old arrays
        ::operator delete[](storage_);
        delete[] view_;

        storage_ = newStorage;
        view_ = newView;
        capacity_ = newCap;
    }

    // Number of elements
    size_type size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    size_type capacity() const noexcept { return capacity_; }

    // Clear all elements (does not change capacity)
    void clear() noexcept {
        for (size_type i = 0; i < size_; ++i)
            storage_[i].~T();
        size_ = 0;
    }

    // Emplace and sort later
    template <typename... Args>
    void emplace_back(Args&&... args) {
        if (size_ == capacity_)
            reserve(capacity_ ? capacity_ * 2 : 1);

        // Placement-new
        new (&storage_[size_]) T(std::forward<Args>(args)...);
        view_[size_] = &storage_[size_];
        ++size_;
    }

    template <typename... Args>
    void emplace(Args&&... args) {

        if (size_ == capacity_)
            reserve(capacity_ ? capacity_ * 2 : 1);

        new (&storage_[size_]) T(std::forward<Args>(args)...);
        pointer pNew = &storage_[size_];

        // find insert position in view_ via binary search
        auto cmp_ptr = [&](pointer a, pointer b) { return comp_(*a, *b); };
        pointer* ins = std::upper_bound(view_, view_ + size_, pNew, cmp_ptr);

        // shift existing pointers up to make room
        //std::memmove(ins + 1, ins, (view_ + size_ - ins) * sizeof(pointer));
        std::move_backward(ins, view_ + size_, view_ + size_ + 1);

        *ins = pNew;

        ++size_;
    }

    void push(const T& value) {
        if (size_ == capacity_)
            reserve(capacity_ ? capacity_ * 2 : 1);

        new (&storage_[size_]) T(value);
        pointer pNew = &storage_[size_];

        auto cmp_ptr = [&](pointer a, pointer b) { return comp_(*a, *b); };
        pointer* ins = std::upper_bound(view_, view_ + size_, pNew, cmp_ptr);
        std::memmove(ins + 1, ins, (view_ + size_ - ins) * sizeof(pointer));
        *ins = pNew;

        ++size_;
    }

    // Push and resort later
    void push_back(const T& value) {
        if (size_ == capacity_)
            reserve(capacity_ ? capacity_ * 2 : 1);
        new (&storage_[size_]) T(value);
        view_[size_] = &storage_[size_];
        ++size_;
    }

    void erase(size_type view_idx) {
        if (view_idx >= size_) return;  // or throw

        // pointer to object to remove
        pointer pRem = view_[view_idx];
        // compute its storage index
        size_type sidx = static_cast<size_type>(pRem - storage_);

        // destroy the object
        storage_[sidx].~T();

        // shift storage down
        for (size_type i = sidx + 1; i < size_; ++i) {
            new (&storage_[i - 1]) T(std::move(storage_[i]));
            storage_[i].~T();
        }

        // adjust all view_ pointers past the removed slot
        for (size_type i = 0; i < size_; ++i) {
            if (view_[i] > pRem)
                view_[i] = view_[i] - 1;
        }

        // erase the one slot in view_
        std::memmove(view_ + view_idx, view_ + view_idx + 1, (size_ - view_idx - 1) * sizeof(pointer));
        --size_;
    }

    // Since the relative order in view_ hasn’t changed, it remains sorted.
    // If your predicate could violate sortedness (e.g. removes out-of-order),
    // you can call resort(); but in most cases it isn’t needed.
    template <typename Pred>
    size_t remove_if(Pred pred) {
        // Compact storage in-place, counting how many get removed.
        size_t write = 0, removed = 0;
        for (size_t read = 0; read < size_; ++read) {
            if (pred(storage_[read])) {
                storage_[read].~T();
                ++removed;
            }
            else {
                if (write != read) {
                    new (&storage_[write]) T(std::move(storage_[read]));
                    storage_[read].~T();
                }
                ++write;
            }
        }
        if (removed == 0) return 0;
        size_t oldSize = size_;
        size_ = write;

        // Rebuild the view_ array so it points 1-to-1 into the new storage.
        for (size_t i = 0; i < size_; ++i)
            view_[i] = &storage_[i];

        return removed;
    }

    // Sorted iteration (default)
    T** begin()       noexcept { return view_; }
    T** end()         noexcept { return view_ + size_; }
    T* const* begin() const noexcept { return view_; }
    T* const* end()   const noexcept { return view_ + size_; }

    // Raw insertion-order iteration
    T* storage_begin()       noexcept { return storage_; }
    T* storage_end()         noexcept { return storage_ + size_; }
    T* storage_begin() const noexcept { return storage_; }
    T* storage_end()   const noexcept { return storage_ + size_; }

    // Re-sort view
    void resort() {
        std::sort(view_, view_ + size_, [&](T* a, T* b) {
            return comp_(*a, *b);
            });
    }

    // Re-sort with custom comparator
    void resort(const Compare& comp) {
        comp_ = comp;
        std::sort(view_, view_ + size_, [&](T* a, T* b) {
            return comp_(*a, *b);
            });
    }

    // Access comparator
    Compare& comparator() { return comp_; }
    const Compare& comparator() const { return comp_; }

    // Direct access
    T& operator[](size_type i) { return *view_[i]; }
    const T& operator[](size_type i) const { return *view_[i]; }


    std::span<T*>        getView()    noexcept { return { view_, size_ }; }
    std::span<T* const>  getView() const noexcept { return { view_, size_ }; }

    std::span<T>         getStorage() noexcept { return { storage_, size_ }; }
    std::span<T const>   getStorage() const noexcept { return { storage_, size_ }; }

private:

    T*  storage_;
    T** view_;      //An ordered view into storage
    size_type size_;
    size_type capacity_;
    Compare comp_;
};

/* Usage example:

#include "VectorSortedView.h"

// Custom comparator for path length, then lexicographically
struct PathComparator {
    bool operator()(const std::string& a, const std::string& b) const {
        return a.size() < b.size()
            || (a.size() == b.size() && a < b);
    }
};

VectorSortedView<std::string, PathComparator> paths;
paths.reserve(100);
paths.emplace_back("/root/child");
paths.emplace_back("/r");

// After potential mutations, re-sort view:
paths.resort();
for (auto ptr : paths.sorted_view()) {
    std::cout << *ptr << "\n";
}
*/