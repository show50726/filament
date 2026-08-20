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

#include "ApiHandler.h"

#include <uboviewer/DebugServer.h>
#include <uboviewer/UboInfo.h>

#include <charconv>
#include <CivetServer.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace filament::uboviewer {
namespace {

void appendEscaped(std::string& output, std::string_view value) {
    output.push_back('"');
    for (char c : value) {
        switch (c) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) >= 0x20) {
                    output.push_back(c);
                }
        }
    }
    output.push_back('"');
}

std::string_view stateName(AllocationState state) {
    switch (state) {
        case AllocationState::FREE: return "free";
        case AllocationState::ALLOCATED: return "allocated";
        case AllocationState::RETIRED: return "retired";
    }
    return "unknown";
}

void appendAllocation(std::string& output, AllocationInfo const& allocation) {
    output += "{\"owner\":\"" + std::to_string(allocation.owner) +
            "\",\"id\":" + std::to_string(allocation.id) +
            ",\"offset\":" + std::to_string(allocation.offset) +
            ",\"size\":" + std::to_string(allocation.size) +
            ",\"requestedSize\":" + std::to_string(allocation.requestedSize) +
            ",\"gpuUseCount\":" + std::to_string(allocation.gpuUseCount) +
            ",\"state\":\"" + std::string(stateName(allocation.state)) + "\",\"name\":";
    appendEscaped(output, allocation.name);
    output.push_back('}');
}

void appendEvent(std::string& output, DebugServer::Event const& event) {
    UboInfo const& info = event.info;
    output += "{\"sequence\":" + std::to_string(event.sequence) +
            ",\"frame\":" + std::to_string(info.frame) +
            ",\"totalSize\":" + std::to_string(info.totalSize) +
            ",\"reallocated\":" + (info.reallocated ? "true" : "false") +
            ",\"allocations\":[";
    bool first = true;
    for (auto const& allocation : info.allocations) {
        if (!first) {
            output.push_back(',');
        }
        first = false;
        appendAllocation(output, allocation);
    }
    output += "]}";
}

uint64_t getSequence(mg_request_info const* request) {
    if (!request->query_string) {
        return 0;
    }
    std::string_view const query = request->query_string;
    constexpr std::string_view prefix = "after=";
    size_t const position = query.find(prefix);
    if (position == std::string_view::npos) {
        return 0;
    }

    uint64_t value = 0;
    char const* begin = query.data() + position + prefix.size();
    char const* end = query.data() + query.size();
    std::from_chars(begin, end, value);
    return value;
}

} // anonymous namespace

bool ApiHandler::handleGet(CivetServer*, struct mg_connection* connection) {
    mg_request_info const* request = mg_get_request_info(connection);
    if (std::string_view(request->local_uri) != "/api/events") {
        return false;
    }

    DebugServer::EventQuery const query = mServer->getEventsAfter(getSequence(request));
    std::string response = "{\"oldestSequence\":" + std::to_string(query.oldestSequence) +
            ",\"latestSequence\":" + std::to_string(query.latestSequence) +
            ",\"reset\":" + (query.reset ? "true" : "false") + ",\"events\":[";
    bool first = true;
    for (auto const& event : query.events) {
        if (!first) {
            response.push_back(',');
        }
        first = false;
        appendEvent(response, event);
    }
    response += "]}";

    mg_printf(connection,
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Cache-Control: no-cache, no-store\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n",
            response.size());
    mg_write(connection, response.data(), response.size());
    return true;
}

} // namespace filament::uboviewer
