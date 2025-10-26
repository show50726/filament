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

#include "details/UboManager.h"

#include "details/FenceManager.h"

#include "MaterialInstance.h"
#include "details/FBufferAllocator.h"

#include <backend/DriverEnums.h>

namespace filament {

namespace {
using namespace utils;
using namespace backend;

using AllocationId = FBufferAllocator::AllocationId;
using allocation_size_t = FBufferAllocator::allocation_size_t;
} // anonymous namespace

UboManager::UboManager(DriverApi& driver, std::unique_ptr<BufferAllocator> allocator,
        allocation_size_t defaultSlotSizeInBytes, allocation_size_t defaultTotalSizeInBytes)
        : mAllocator(allocator ? std::move(allocator)
                               : std::unique_ptr<BufferAllocator>(new FBufferAllocator(
                                         defaultTotalSizeInBytes, defaultSlotSizeInBytes))) {
    reallocate(driver, defaultTotalSizeInBytes);
}

void UboManager::beginFrame(DriverApi& driver,
        const std::unordered_map<const FMaterial*, ResourceList<FMaterialInstance>>&
                materialInstances) {
    // Check finished frames and decrement GPU count accordingly.
    mFenceManager.reclaimCompletedResources(driver,
            [this](AllocationId id) { mAllocator->releaseGpu(id); });

    // Actually merge the slots.
    mAllocator->releaseFreeSlots();

    // Traverse all MIs and see which of them need slot allocation.
    AllocationResult allocationResult =
            updateMaterialInstanceAllocations(materialInstances, ON_DEMAND);

    if (allocationResult == SUCCESS) {
        // No need to grow the buffer, so we can just map the buffer for writing and return.
        mMemoryMappedBufferHandle = driver.mapBuffer(mUbHandle, 0, mUboSize,
                MapBufferAccessFlags::WRITE_BIT, "UboManager");

        return;
    }

    // Calculate the required size and grow the Ubo.
    const allocation_size_t requiredSize = calculateRequiredSize(materialInstances);
    reallocate(driver, requiredSize);

    // Allocate slots for each MI on the new Ubo.
    UTILS_UNUSED_IN_RELEASE const AllocationResult needReallocationAgain =
            updateMaterialInstanceAllocations(materialInstances, ALWAYS);
    assert_invariant(needReallocationAgain != REALLOCATION_REQUIRED);

    // Map the buffer so that we can write to it
    mMemoryMappedBufferHandle =
            driver.mapBuffer(mUbHandle, 0, mUboSize, MapBufferAccessFlags::WRITE_BIT, "UboManager");

    // Migrate all MI data to the new allocated slots.
    for (const auto& materialInstance: materialInstances) {
        materialInstance.second.forEach([this, &driver](const FMaterialInstance* mi) {
            if (!mi->isUsingUboBatching()) return;

            const AllocationId allocationId = mi->getAllocationId();
            assert_invariant(FBufferAllocator::isValid(allocationId));
            updateSlot(driver, allocationId, mi->getUniformBuffer().toBufferDescriptor(driver));
        });
    }
}

void UboManager::finishBeginFrame(DriverApi& driver) {
    if (mMemoryMappedBufferHandle) {
        driver.unmapBuffer(mMemoryMappedBufferHandle);
        mMemoryMappedBufferHandle.clear();
    }
}

void UboManager::endFrame(DriverApi& driver,
        const std::unordered_map<const FMaterial*, ResourceList<FMaterialInstance>>&
                materialInstances) {
    std::unordered_set<AllocationId> allocationIds;
    for (const auto& materialInstance: materialInstances) {
        materialInstance.second.forEach(
                [allocator = mAllocator.get(), &allocationIds](const FMaterialInstance* mi) {
                    if (!mi->isUsingUboBatching()) return;

                    const AllocationId id = mi->getAllocationId();
                    if (!FBufferAllocator::isValid(id)) {
                        return;
                    }

                    allocator->acquireGpu(id);
                    allocationIds.insert(id);
                });
    }

    mFenceManager.track(driver, std::move(allocationIds));
}

void UboManager::terminate(DriverApi& driver) {
    mFenceManager.reset(driver);

    driver.destroyBufferObject(mUbHandle);
    mAllocator.reset();
}

void UboManager::updateSlot(DriverApi& driver, AllocationId id,
        BufferDescriptor bufferDescriptor) const {
    if (!mMemoryMappedBufferHandle) return;

    const allocation_size_t offset = mAllocator->getAllocationOffset(id);
    driver.copyToMemoryMappedBuffer(mMemoryMappedBufferHandle, offset, std::move(bufferDescriptor));
}

void UboManager::retireSlot(FBufferAllocator::AllocationId id) const {
    if (!FBufferAllocator::isValid(id)) return;
    mAllocator->retire(id);
}

allocation_size_t UboManager::getTotalSize() const noexcept { return mUboSize; }

allocation_size_t UboManager::getAllocationOffset(AllocationId id) const {
    return mAllocator->getAllocationOffset(id);
}

UboManager::AllocationResult UboManager::updateMaterialInstanceAllocations(
        const std::unordered_map<const FMaterial*, ResourceList<FMaterialInstance>>&
                materialInstances,
        AllocationMode allocationMode) {
    AllocationResult result = SUCCESS;
    for (const auto& materialInstance: materialInstances) {
        materialInstance.second.forEach([this, &result, allocationMode](FMaterialInstance* mi) {
            if (tryAllocateMaterialInstanceSlot(mi, mAllocator.get(), mUbHandle, allocationMode) ==
                    REALLOCATION_REQUIRED) {
                result = REALLOCATION_REQUIRED;
            }
        });
    }

    return result;
}

UboManager::AllocationResult UboManager::tryAllocateMaterialInstanceSlot(FMaterialInstance* mi,
        BufferAllocator* allocator, const Handle<HwBufferObject>& ubHandle,
        AllocationMode allocationMode) {
    if (!mi->isUsingUboBatching()) return SUCCESS;

    const AllocationId id = mi->getAllocationId();
    auto allocateAndAssign = [&](AllocationId originalId) -> AllocationResult {
        auto [newId, newOffset] = allocator->allocate(mi->getUniformBuffer().getSize());
        // Special handling for instances that were previously UNALLOCATED:
        // If the new allocation also fails, keep it UNALLOCATED to signal initial failure.
        if (originalId == FBufferAllocator::UNALLOCATED &&
                newId == FBufferAllocator::REALLOCATION_REQUIRED) {
            newId = FBufferAllocator::UNALLOCATED;
        }

        // Even if the allocation is not valid, we need to set it to let the following
        // process knows.
        mi->assignUboAllocation(ubHandle, newId, newOffset);

        if (!FBufferAllocator::isValid(newId)) {
            return REALLOCATION_REQUIRED;
        }
        return SUCCESS;
    };

    if (!FBufferAllocator::isValid(id) || allocationMode == ALWAYS) {
        // The material instance is first time being allocated (if !isValid)
        // or is being forcibly re-allocated (if ALWAYS).
        return allocateAndAssign(id);
    }

    if (mi->getUniformBuffer().isDirty() && allocator->isLockedByGpu(id)) {
        // If the uniform buffer is updated and the slot is still being locked by GPU,
        // we will need to allocate a new slot to write the updated content.
        // This is known as "orphaning".
        allocator->retire(id);
        return allocateAndAssign(id);
    }
    // Else, the slot is valid and no need to allocate a new slot.

    return SUCCESS;
}

void UboManager::reallocate(DriverApi& driver, allocation_size_t requiredSize) {
    if (mUbHandle) {
        driver.destroyBufferObject(mUbHandle);
    }

    mFenceManager.reset(driver);
    mAllocator->reset(requiredSize);
    mUboSize = requiredSize;
    mUbHandle = driver.createBufferObject(requiredSize, BufferObjectBinding::UNIFORM,
            BufferUsage::DYNAMIC | BufferUsage::SHARED_WRITE_BIT);
}

allocation_size_t UboManager::calculateRequiredSize(
        const std::unordered_map<const FMaterial*, ResourceList<FMaterialInstance>>&
                materialInstances) const {
    allocation_size_t newBufferSize = 0;
    for (const auto& materialInstance: materialInstances) {
        materialInstance.second.forEach(
                [&newBufferSize, allocator = mAllocator.get()](const FMaterialInstance* mi) {
                    if (!mi->isUsingUboBatching()) return;

                    const AllocationId allocationId = mi->getAllocationId();
                    if (allocationId == FBufferAllocator::REALLOCATION_REQUIRED) {
                        // For MIs whose parameters have been updated, aside from the slot it is
                        // being occupied by the GPU, we need to preserve an additional slot for it.
                        newBufferSize += 2 * allocator->alignUp(mi->getUniformBuffer().getSize());
                    } else {
                        newBufferSize += allocator->alignUp(mi->getUniformBuffer().getSize());
                    }
                });
    }
    return mAllocator->alignUp(newBufferSize * BUFFER_SIZE_GROWTH_MULTIPLIER);
}

} // namespace filament
