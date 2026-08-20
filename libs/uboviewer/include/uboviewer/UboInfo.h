/*
 * Copyright (C) 2026 The Android Open Source Project
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

#ifndef UBOVIEWER_UBOINFO_H
#define UBOVIEWER_UBOINFO_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace filament::uboviewer {

enum class AllocationState : uint8_t {
    FREE,
    ALLOCATED,
    RETIRED,
};

struct AllocationInfo {
    uint64_t owner = 0;
    uint32_t id = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t requestedSize = 0;
    uint32_t gpuUseCount = 0;
    AllocationState state = AllocationState::FREE;
    std::string name;

    [[nodiscard]] bool hasSameLayout(AllocationInfo const& rhs) const noexcept {
        return owner == rhs.owner && id == rhs.id && offset == rhs.offset && size == rhs.size &&
                requestedSize == rhs.requestedSize && state == rhs.state && name == rhs.name;
    }
};

struct UboInfo {
    uint64_t frame = 0;
    uint32_t totalSize = 0;
    bool reallocated = false;
    std::vector<AllocationInfo> allocations;

    [[nodiscard]] bool hasSameLayout(UboInfo const& rhs) const noexcept {
        if (totalSize != rhs.totalSize || allocations.size() != rhs.allocations.size()) {
            return false;
        }
        for (size_t i = 0; i < allocations.size(); ++i) {
            if (!allocations[i].hasSameLayout(rhs.allocations[i])) {
                return false;
            }
        }
        return true;
    }
};

} // namespace filament::uboviewer

#endif // UBOVIEWER_UBOINFO_H
