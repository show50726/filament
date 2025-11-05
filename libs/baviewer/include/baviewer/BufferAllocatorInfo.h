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

#ifndef BAVIEWER_BUFFERALLOCATORINFO_H
#define BAVIEWER_BUFFERALLOCATORINFO_H

#include <vector>
#include <cstdint>

namespace filament::baviewer {

struct SlotInfo {
    uint32_t offset;
    uint32_t size;
    bool isAllocated;
    uint32_t gpuUseCount;

    bool operator==(const SlotInfo& other) const {
        return offset == other.offset &&
               size == other.size &&
               isAllocated == other.isAllocated &&
               gpuUseCount == other.gpuUseCount;
    }
};

struct BufferAllocatorInfo {
    uint64_t frameId = 0;
    uint32_t totalSize;
    uint32_t slotSize;
    std::vector<SlotInfo> slots;
    bool hasChanged = false;

    bool operator==(const BufferAllocatorInfo& other) const {
        return totalSize == other.totalSize &&
               slotSize == other.slotSize &&
               slots == other.slots;
    }
};

} // namespace filament::baviewer

#endif // BAVIEWER_BUFFERALLOCATORINFO_H
