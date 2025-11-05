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

#include <baviewer/BufferAllocatorInfo.h>
#include <baviewer/JsonWriter.h>

#include <utils/CString.h>

#include <cstddef>
#include <ostream>
#include <sstream>
#include <vector>

namespace filament::baviewer {

namespace {

void writeSlots(std::ostream& os, const BufferAllocatorInfo& info) {
    os << "  \"slots\": [\n";
    auto& slots = info.slots;
    for (size_t i = 0; i < slots.size(); ++i) {
        const SlotInfo& slot = slots[i];
        os << "    {\n";
        os << "      \"offset\": " << slot.offset << ",\n";
        os << "      \"size\": " << slot.size << ",\n";
        os << "      \"isAllocated\": " << (slot.isAllocated ? "true" : "false") << ",\n";
        os << "      \"gpuUseCount\": " << slot.gpuUseCount << "\n";
        os << "    }";
        if (i + 1 < slots.size()) os << ",";
        os << "\n";
    }
    os << "  ]\n";
}

} // anonymous

const char* JsonWriter::getJsonString() const {
    return mJsonString.c_str();
}

size_t JsonWriter::getJsonSize() const {
    return mJsonString.size();
}

bool JsonWriter::writeBufferAllocatorInfo(const BufferAllocatorInfo& info) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"frameId\": " << info.frameId << ",\n";
    os << "  \"totalSize\": " << info.totalSize << ",\n";
    os << "  \"slotSize\": " << info.slotSize << ",\n";
    writeSlots(os, info);
    os << "}\n";
    mJsonString = utils::CString(os.str().c_str());
    return true;
}

} // namespace filament::baviewer
