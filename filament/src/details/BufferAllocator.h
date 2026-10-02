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

#include <array>
#include <cstdint>
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

    // Free blocks are kept in segregated bins (TLSF-style), indexed by their size in slots:
    // - sizes below SL_COUNT slots each have their own bin;
    // - larger sizes are split by their highest bit (first level), then into SL_COUNT linear
    //   ranges (second level).
    // Each bin is an intrusive doubly-linked list threaded through the head nodes of its free
    // blocks, and two bitmaps record which bins are non-empty. Finding, inserting and removing
    // a free block is O(1) and never allocates memory.
    static constexpr uint32_t INVALID_INDEX = ~0u;
    static constexpr uint32_t SL_BITS = 4;
    static constexpr uint32_t SL_COUNT = 1u << SL_BITS;
    // Enough first-level bins for any 32-bit slot count.
    static constexpr uint32_t FL_COUNT = 32 - SL_BITS + 1;

    // Having an internal node type holding the base slot node and additional information.
    struct InternalSlotNode {
        Slot slot;
        // Neighbors in the bin's free list, as node indices. Only valid for the head node of a
        // free block.
        uint32_t prevFree;
        uint32_t nextFree;
    };

    struct Bin {
        uint32_t head = INVALID_INDEX; // oldest free block, allocated first
        uint32_t tail = INVALID_INDEX; // newest free block
    };

    struct BinIndex {
        uint32_t fl;
        uint32_t sl;
    };

    [[nodiscard]] static BinIndex binIndexFromSlotCount(uint32_t slotCount) noexcept;

    // Returns the head node index of a free block of at least `slotCount` slots, or
    // INVALID_INDEX if there's none.
    [[nodiscard]] uint32_t findFreeBlock(uint32_t slotCount) const noexcept;
    void insertFreeBlock(uint32_t headIndex) noexcept;
    void removeFreeBlock(uint32_t headIndex) noexcept;

    [[nodiscard]] InternalSlotNode* getNodeById(AllocationId id);
    [[nodiscard]] const InternalSlotNode* getNodeById(AllocationId id) const;

    void copySlotToTail(allocation_size_t tailIndex, const InternalSlotNode* head) noexcept;
    void freeSlot(InternalSlotNode* node);

    allocation_size_t mTotalSize;
    const allocation_size_t mSlotSize; // Size of a single slot in bytes
    const uint8_t mSlotSizeShift;
    utils::FixedCapacityVector<InternalSlotNode> mNodes;
    uint32_t mFlBitmap = 0;                       // bit fl: some bin in mBins[fl] is non-empty
    std::array<uint32_t, FL_COUNT> mSlBitmaps{};  // bit sl of [fl]: mBins[fl][sl] is non-empty
    std::array<std::array<Bin, SL_COUNT>, FL_COUNT> mBins{};
    uint32_t mAllocationCount = 0;
};

} // namespace filament

#endif // TNT_FILAMENT_DETAILS_BUFFERALLOCATOR_H
