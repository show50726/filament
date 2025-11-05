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

#ifndef BAVIEWER_JSONWRITER_H
#define BAVIEWER_JSONWRITER_H

#include <utils/CString.h>

namespace filament::baviewer {

struct BufferAllocatorInfo;

// This class generates portions of JSON messages that are sent to the web client.
class JsonWriter {
public:

    // Retrieves the most recently generated string.
    const char* getJsonString() const;
    size_t getJsonSize() const;

    // Generates a JSON string describing the given BufferAllocatorInfo.
    bool writeBufferAllocatorInfo(const BufferAllocatorInfo& info);

private:
    utils::CString mJsonString;
};

} // namespace filament::baviewer

#endif  // BAVIEWER_JSONWRITER_H
