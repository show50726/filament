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

#include "details/BufferAllocator.h"

#include <private/utils/Tracing.h>

#include <utils/algorithm.h>
#include <utils/compiler.h>
#include <utils/debug.h>
#include <utils/Panic.h>

namespace filament {
namespace {

#ifndef NDEBUG
constexpr static bool isPowerOfTwo(uint32_t n) {
    return (n > 0) && ((n & (n - 1)) == 0);
}
#endif

constexpr static uint8_t powerOfTwoShift(uint32_t n) noexcept {
    if (UTILS_VERY_UNLIKELY(n == 0)) {
        return 0;
    }
    return 31 - utils::clz(n);
}

} // anonymous namespace

BufferAllocator::BinIndex BufferAllocator::binIndexFromSlotCount(uint32_t const slotCount) noexcept {
    assert_invariant(slotCount > 0);
    if (slotCount < SL_COUNT) {
        return { 0, slotCount };
    }
    uint32_t const msb = 31 - utils::clz(slotCount);
    return { msb - SL_BITS + 1, (slotCount >> (msb - SL_BITS)) - SL_COUNT };
}

uint32_t BufferAllocator::findFreeBlock(uint32_t const slotCount) const noexcept {
    auto const [fl, sl] = binIndexFromSlotCount(slotCount);

    // A bin holds the sizes [lowerBound, lowerBound + span). Every block in the request's own
    // bin is large enough only if the request is the bin's lower bound. Otherwise, start the
    // search at the next bin. Bins below 2 * SL_COUNT slots hold a single size (span == 1).
    uint32_t const span = (fl == 0) ? 1u : (1u << (fl - 1));
    bool const wholeBinFits = (slotCount & (span - 1)) == 0;

    // Smallest non-empty bin at or above the starting bin: O(1).
    uint32_t foundFl = fl;
    uint32_t slBitmap = mSlBitmaps[fl] & (~0u << (wholeBinFits ? sl : sl + 1));
    if (!slBitmap) {
        uint32_t const flBitmap = mFlBitmap & (~0u << (fl + 1));
        if (flBitmap) {
            foundFl = utils::ctz(flBitmap);
            slBitmap = mSlBitmaps[foundFl];
        }
    }
    if (slBitmap) {
        return mBins[foundFl][utils::ctz(slBitmap)].head;
    }

    // No larger bin has a block, but the request's own bin may still hold one that fits. This
    // linear scan only runs when the buffer is nearly full.
    if (!wholeBinFits) {
        for (uint32_t i = mBins[fl][sl].head; i != INVALID_INDEX; i = mNodes[i].nextFree) {
            if ((mNodes[i].slot.slotSize >> mSlotSizeShift) >= slotCount) {
                return i;
            }
        }
    }
    return INVALID_INDEX;
}

void BufferAllocator::insertFreeBlock(uint32_t const headIndex) noexcept {
    InternalSlotNode& node = mNodes[headIndex];
    auto const [fl, sl] = binIndexFromSlotCount(node.slot.slotSize >> mSlotSizeShift);
    Bin& bin = mBins[fl][sl];

    // Append, so that blocks of the same size are reused in the order they were freed.
    node.prevFree = bin.tail;
    node.nextFree = INVALID_INDEX;
    if (bin.tail != INVALID_INDEX) {
        mNodes[bin.tail].nextFree = headIndex;
    } else {
        bin.head = headIndex;
    }
    bin.tail = headIndex;

    mSlBitmaps[fl] |= 1u << sl;
    mFlBitmap |= 1u << fl;
}

void BufferAllocator::removeFreeBlock(uint32_t const headIndex) noexcept {
    InternalSlotNode const& node = mNodes[headIndex];
    // Must be called before the block's size changes, since the size selects the bin.
    auto const [fl, sl] = binIndexFromSlotCount(node.slot.slotSize >> mSlotSizeShift);
    Bin& bin = mBins[fl][sl];

    if (node.prevFree != INVALID_INDEX) {
        mNodes[node.prevFree].nextFree = node.nextFree;
    } else {
        assert_invariant(bin.head == headIndex);
        bin.head = node.nextFree;
    }
    if (node.nextFree != INVALID_INDEX) {
        mNodes[node.nextFree].prevFree = node.prevFree;
    } else {
        assert_invariant(bin.tail == headIndex);
        bin.tail = node.prevFree;
    }

    if (bin.head == INVALID_INDEX) {
        mSlBitmaps[fl] &= ~(1u << sl);
        if (!mSlBitmaps[fl]) {
            mFlBitmap &= ~(1u << fl);
        }
    }
}

BufferAllocator::BufferAllocator(allocation_size_t totalSize, allocation_size_t slotSize)
    : mTotalSize(totalSize),
      mSlotSize(slotSize),
      mSlotSizeShift(powerOfTwoShift(slotSize)) {
    assert_invariant(mSlotSize > 0);
    assert_invariant(isPowerOfTwo(mSlotSize));

    reset(mTotalSize);
}

void BufferAllocator::reset(allocation_size_t newTotalSize) {
    assert_invariant(newTotalSize % mSlotSize == 0);

    mTotalSize = newTotalSize;
    mFlBitmap = 0;
    mSlBitmaps.fill(0);
    for (auto& bins : mBins) {
        bins.fill(Bin{});
    }
    mAllocationCount = 0;

    // Resize mNodes to the number of slots
    const size_t slotCount = mTotalSize >> mSlotSizeShift;
    mNodes.clear();
    mNodes.reserve(slotCount);
    mNodes.resize(slotCount);

    // Initialize the single free block covering the entire buffer
    InternalSlotNode* node = &mNodes[0];
    node->slot.offset = 0;
    node->slot.slotSize = mTotalSize;
    node->slot.isAllocated = false;
    insertFreeBlock(0);

    // Set the tail tag
    copySlotToTail(slotCount - 1, node);
}

std::pair<BufferAllocator::AllocationId, BufferAllocator::allocation_size_t>
    BufferAllocator::allocate(allocation_size_t size) noexcept {
    if (size == 0) {
        return { UNALLOCATED, 0 };
    }

    const allocation_size_t alignedSize = alignUp(size);
    if (UTILS_UNLIKELY(alignedSize == 0 || alignedSize > mTotalSize)) {
        // Larger than the whole buffer, or alignUp() overflowed.
        return { REALLOCATION_REQUIRED, 0 };
    }
    const uint32_t headIndex = findFreeBlock(alignedSize >> mSlotSizeShift);

    if (headIndex == INVALID_INDEX) {
        return { REALLOCATION_REQUIRED, 0 };
    }

    InternalSlotNode* targetNode = &mNodes[headIndex];
    const allocation_size_t originalSlotSize = targetNode->slot.slotSize;

    removeFreeBlock(headIndex);

    const allocation_size_t remainingSize = targetNode->slot.slotSize - alignedSize;
    const allocation_size_t offset = targetNode->slot.offset;
    assert_invariant(remainingSize % mSlotSize == 0);
    assert_invariant((offset + alignedSize) % mSlotSize == 0);
    targetNode->slot.isAllocated = true;
    ++mAllocationCount;

    // Split the slot if it is larger than what we need.
    if (originalSlotSize > alignedSize) {
        // Update Head block
        targetNode->slot.slotSize = alignedSize;

        // Update Tail block
        const size_t endSlotIndex = slotIndexFromOffset(offset + alignedSize) - 1;
        copySlotToTail(endSlotIndex, targetNode);

        // The Head of remaining free block
        const size_t nextSlotIndex = endSlotIndex + 1;
        InternalSlotNode* nextNode = &mNodes[nextSlotIndex];
        nextNode->slot.offset = offset + alignedSize;
        nextNode->slot.slotSize = remainingSize;
        nextNode->slot.isAllocated = false;
        insertFreeBlock(uint32_t(nextSlotIndex));

        // Update the Tail of remaining free block
        const size_t nextEndSlotIndex =
                slotIndexFromOffset(nextNode->slot.offset + nextNode->slot.slotSize) - 1;
        copySlotToTail(nextEndSlotIndex, nextNode);
    } else {
        // Allocate the whole block
        // Update Tail block
        const size_t endSlotIndex =
                slotIndexFromOffset(offset + targetNode->slot.slotSize) - 1;
        copySlotToTail(endSlotIndex, targetNode);
    }

    return { calculateIdByOffset(offset), offset };
}

BufferAllocator::InternalSlotNode* BufferAllocator::getNodeById(AllocationId id) {
    assert_invariant(id > 0);
    assert_invariant(id <= mNodes.size());
    return &mNodes[id - 1];
}

const BufferAllocator::InternalSlotNode* BufferAllocator::getNodeById(
        AllocationId id) const {
    assert_invariant(id > 0);
    assert_invariant(id <= mNodes.size());
    return &mNodes[id - 1];
}

void BufferAllocator::copySlotToTail(allocation_size_t tailIndex,
        const InternalSlotNode* head) noexcept {
    mNodes[tailIndex].slot = head->slot;
}

void BufferAllocator::retire(AllocationId id) {
    InternalSlotNode* targetNode = getNodeById(id);
    assert_invariant(targetNode != nullptr);
    assert_invariant(targetNode->slot.isAllocated);

    targetNode->slot.isAllocated = false;
    assert_invariant(mAllocationCount > 0);
    --mAllocationCount;
    freeSlot(targetNode);
}

void BufferAllocator::freeSlot(InternalSlotNode* node) {
    size_t currentStartIdx = slotIndexFromOffset(node->slot.offset);
    size_t currentEndIdx = slotIndexFromOffset(node->slot.offset + node->slot.slotSize) - 1;

    // Check Previous (Left Neighbor)
    if (currentStartIdx > 0) {
        InternalSlotNode* prev = &mNodes[currentStartIdx - 1];
        size_t prevHeadIdx = slotIndexFromOffset(prev->slot.offset);
        InternalSlotNode* prevHead = &mNodes[prevHeadIdx];

        if (prevHead->slot.isFree()) {
            // Merge with prev
            // prev is the TAIL of the left block.

            assert_invariant(prevHead->slot.offset == prev->slot.offset);
            assert_invariant(prevHead->slot.slotSize == prev->slot.slotSize);

            removeFreeBlock(uint32_t(prevHeadIdx));
            prevHead->slot.slotSize += node->slot.slotSize;
            assert_invariant(prevHead->slot.slotSize % mSlotSize == 0);

            // Switch current node pointer to prevHead for potential next merge
            node = prevHead;
            currentStartIdx = prevHeadIdx;
        }
    }

    // Check Next (Right Neighbor)
    size_t nextStartIdx = currentEndIdx + 1;
    if (nextStartIdx < mNodes.size()) {
        InternalSlotNode* next = &mNodes[nextStartIdx];
        if (next->slot.isFree()) {
            // Merge with next

            removeFreeBlock(uint32_t(nextStartIdx));
            node->slot.slotSize += next->slot.slotSize;
            assert_invariant(node->slot.slotSize % mSlotSize == 0);

            // currentEndIdx increases
            currentEndIdx = slotIndexFromOffset(node->slot.offset + node->slot.slotSize) - 1;
        }
    }

    assert_invariant(slotIndexFromOffset(node->slot.offset) == currentStartIdx);

    // Push merged free block to the list
    insertFreeBlock(uint32_t(currentStartIdx));

    // Copy Head to Tail
    copySlotToTail(currentEndIdx, node);
}

BufferAllocator::allocation_size_t BufferAllocator::getTotalSize() const noexcept {
    return mTotalSize;
}

BufferAllocator::allocation_size_t
    BufferAllocator::getAllocationOffset(AllocationId id) const {
    return getNodeById(id)->slot.offset;
}

BufferAllocator::allocation_size_t BufferAllocator::slotIndexFromOffset(
        allocation_size_t offset) const noexcept {
    return offset >> mSlotSizeShift;
}

BufferAllocator::AllocationId BufferAllocator::calculateIdByOffset(
        allocation_size_t offset) const {
    assert_invariant(offset % mSlotSize == 0);

    // The ID is 1-based since we use 0 for UNALLOCATED.
    return slotIndexFromOffset(offset) + 1;
}

BufferAllocator::allocation_size_t BufferAllocator::getAllocationSize(AllocationId id) const {
    return getNodeById(id)->slot.slotSize;
}

bool BufferAllocator::isValid(AllocationId id) {
    return id != UNALLOCATED && id != REALLOCATION_REQUIRED;
}

BufferAllocator::allocation_size_t BufferAllocator::alignUp(
        allocation_size_t size) const noexcept {
    if (size == 0) return 0;

    return (size + mSlotSize - 1) & ~(mSlotSize - 1);
}

} // namespace filament
