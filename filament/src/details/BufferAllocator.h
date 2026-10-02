/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef TNT_FILAMENT_DETAILS_BUFFERALLOCATOR_H
#define TNT_FILAMENT_DETAILS_BUFFERALLOCATOR_H

#include <utils/FixedCapacityVector.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>


namespace filament {

// This class is NOT thread-safe.
//
// It internally manages shared state (e.g., mSlotPool, mFreeList, mOffsetMap) without any
// synchronization primitives. Concurrent access from multiple threads to the same
// BufferAllocator instance will result in data races and undefined behavior.
//
// If an instance of this class is to be shared between threads, all calls to its member
// functions MUST be protected by external synchronization (e.g., a utils::Mutex).
class BufferAllocator {
public:
    using allocation_size_t = uint32_t;
    using AllocationId = uint32_t;

    static constexpr AllocationId UNALLOCATED = 0;
    static constexpr AllocationId REALLOCATION_REQUIRED = ~0u;

    struct Slot {
        allocation_size_t offset;             // 4 bytes
        allocation_size_t slotSize;           // 4 bytes
        bool isAllocated;                     // 1 byte
        char padding[3];                      // 3 bytes

        [[nodiscard]] bool isFree() const noexcept {
            return !isAllocated;
        }
    };

    // `slotSize` is derived from the GPU's uniform buffer offset alignment requirement,
    // which can be up to 256 bytes.
    explicit BufferAllocator(allocation_size_t totalSize,
            allocation_size_t slotSize);

    BufferAllocator(BufferAllocator const&) = delete;
    BufferAllocator(BufferAllocator&&) = delete;

    // Allocate a new slot and return its id and slot offset in the UBO.
    // If the returned id is not valid, that means there's no large enough slot for allocation.
    [[nodiscard]] std::pair<AllocationId, allocation_size_t> allocate(
            allocation_size_t size) noexcept;

    // Call it when a slot is no longer in use by the MaterialInstance or GPU.
    // The slot is released and potential merging is performed immediately.
    void retire(AllocationId id);

    // Resets the allocator to its initial state with a new total size.
    // All existing allocations are cleared.
    void reset(allocation_size_t newTotalSize);

    // Size of the UBO in bytes.
    [[nodiscard]] allocation_size_t getTotalSize() const noexcept;

    // Query the allocation offset by AllocationId.
    [[nodiscard]] allocation_size_t getAllocationOffset(AllocationId id) const;

    [[nodiscard]] allocation_size_t alignUp(allocation_size_t size) const noexcept;

    [[nodiscard]] allocation_size_t getAllocationSize(AllocationId id) const;

    [[nodiscard]] static bool isValid(AllocationId id);

    // Number of allocations that have not been retired yet.
    [[nodiscard]] uint32_t getAllocationCount() const noexcept { return mAllocationCount; }

private:
    [[nodiscard]] allocation_size_t slotIndexFromOffset(allocation_size_t offset) const noexcept;
    [[nodiscard]] AllocationId calculateIdByOffset(allocation_size_t offset) const;

    // Recycles the nodes of mFreeList. allocate() and freeSlot() insert and erase free-list
    // entries all the time, and with the default allocator every insertion is a malloc/free
    // pair. All nodes have the same size, so freed nodes are kept on an intrusive list and
    // reused. Memory is only returned to the system when the pool is destroyed.
    class FreeListNodePool {
    public:
        FreeListNodePool() noexcept = default;
        FreeListNodePool(FreeListNodePool const&) = delete;
        FreeListNodePool& operator=(FreeListNodePool const&) = delete;
        ~FreeListNodePool() noexcept;

        [[nodiscard]] void* alloc(size_t size);
        void free(void* p, size_t size) noexcept;

    private:
        struct Link {
            Link* next;
        };

        static constexpr size_t NODES_PER_CHUNK = 256;
        static constexpr size_t ALIGNMENT = alignof(std::max_align_t);

        size_t mNodeSize = 0;       // set by the first allocation
        Link* mFreeNodes = nullptr; // recycled nodes
        Link* mChunks = nullptr;    // all chunks, linked through their first bytes
        char* mCurrent = nullptr;   // next unused node in the newest chunk
        char* mEnd = nullptr;       // end of the newest chunk
    };

    // STL allocator that forwards to a FreeListNodePool.
    template<typename T>
    struct FreeListAllocator {
        using value_type = T;

        explicit FreeListAllocator(FreeListNodePool* pool) noexcept : pool(pool) {}

        template<typename U>
        FreeListAllocator(FreeListAllocator<U> const& rhs) noexcept : pool(rhs.pool) {} // NOLINT

        [[nodiscard]] T* allocate(size_t n) {
            static_assert(alignof(T) <= alignof(std::max_align_t));
            return static_cast<T*>(pool->alloc(n * sizeof(T)));
        }

        void deallocate(T* p, size_t n) noexcept {
            pool->free(p, n * sizeof(T));
        }

        template<typename U>
        bool operator==(FreeListAllocator<U> const& rhs) const noexcept {
            return pool == rhs.pool;
        }

        template<typename U>
        bool operator!=(FreeListAllocator<U> const& rhs) const noexcept {
            return pool != rhs.pool;
        }

        FreeListNodePool* pool;
    };

    struct InternalSlotNode;

    using FreeList = std::multimap</*slot size*/ allocation_size_t, InternalSlotNode*,
            std::less<allocation_size_t>,
            FreeListAllocator<std::pair<const allocation_size_t, InternalSlotNode*>>>;

    // Having an internal node type holding the base slot node and additional information.
    struct InternalSlotNode {
        Slot slot;
        FreeList::iterator freeListIterator;
    };

    [[nodiscard]] InternalSlotNode* getNodeById(AllocationId id);
    [[nodiscard]] const InternalSlotNode* getNodeById(AllocationId id) const;

    void copySlotToTail(allocation_size_t tailIndex, const InternalSlotNode* head) noexcept;
    void freeSlot(InternalSlotNode* node);

    allocation_size_t mTotalSize;
    const allocation_size_t mSlotSize; // Size of a single slot in bytes
    const uint8_t mSlotSizeShift;
    utils::FixedCapacityVector<InternalSlotNode> mNodes;
    FreeListNodePool mFreeListNodePool; // must outlive mFreeList
    FreeList mFreeList;
    uint32_t mAllocationCount = 0;
};

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_BUFFERALLOCATOR_H
