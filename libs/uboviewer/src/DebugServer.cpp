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
#include "uboviewer_resources.h"

#include <uboviewer/DebugServer.h>

#include <utils/debug.h>
#include <utils/Logger.h>
#include <utils/Mutex.h>

#include <CivetServer.h>

#include <string>
#include <string_view>
#include <unordered_map>

namespace {

struct Asset {
    std::string_view mime;
    std::string_view data;
};

std::unordered_map<std::string_view, Asset> makeAssetMap() {
    return {
            { "/index.html",
                    { "text/html", { (char const*) UBOVIEWER_RESOURCES_INDEX_DATA } } },
            { "/app.js",
                    { "text/javascript", { (char const*) UBOVIEWER_RESOURCES_APP_DATA } } },
    };
}

} // anonymous namespace

namespace filament::uboviewer {

class FileRequestHandler : public CivetHandler {
public:
    bool handleGet(CivetServer*, struct mg_connection* connection) override {
        mg_request_info const* request = mg_get_request_info(connection);
        std::string_view uri = request->local_uri;
        if (uri == "/") {
            uri = "/index.html";
        }

        auto const asset = mAssets.find(uri);
        if (asset == mAssets.end()) {
            return false;
        }

        auto const& [mime, data] = asset->second;
        mg_printf(connection,
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: %s\r\n"
                "Cache-Control: no-cache\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n\r\n",
                mime.data(), data.size());
        mg_write(connection, data.data(), data.size());
        return true;
    }

private:
    std::unordered_map<std::string_view, Asset> const mAssets = makeAssetMap();
};

DebugServer::DebugServer(int port, size_t historyCapacity)
        : mHistoryCapacity(historyCapacity) {
    assert_invariant(historyCapacity > 0);

    std::string const portString = std::to_string(port);
    char const* options[] = {
            "listening_ports", portString.c_str(),
            "num_threads", "5",
            "error_log_file", "civetweb.txt",
            nullptr,
    };

    mServer = new CivetServer(options);
    if (!mServer->getContext()) {
        delete mServer;
        mServer = nullptr;
        LOG(ERROR) << "[uboviewer] Unable to start DebugServer, see civetweb.txt for details.";
        return;
    }

    mFileHandler = new FileRequestHandler();
    mApiHandler = new ApiHandler(this);
    mServer->addHandler("/api", mApiHandler);
    mServer->addHandler("", mFileHandler);

    LOG(INFO) << "[uboviewer] DebugServer listening at http://localhost:" << port;
}

DebugServer::~DebugServer() {
    if (!mServer) {
        return;
    }
    mServer->close();
    delete mFileHandler;
    delete mApiHandler;
    delete mServer;
}

void DebugServer::publish(UboInfo&& info) {
    utils::LockGuard const lock(mEventsMutex);
    if (!info.reallocated && !mEvents.empty() && mEvents.back().info.hasSameLayout(info)) {
        return;
    }

    mEvents.push_back({ ++mLatestSequence, std::move(info) });
    if (mEvents.size() > mHistoryCapacity) {
        mEvents.pop_front();
    }
}

DebugServer::EventQuery DebugServer::getEventsAfter(uint64_t sequence) const {
    utils::LockGuard const lock(mEventsMutex);
    EventQuery result;
    result.latestSequence = mLatestSequence;
    if (mEvents.empty()) {
        return result;
    }

    result.oldestSequence = mEvents.front().sequence;
    result.reset = sequence < result.oldestSequence - 1;
    if (result.reset) {
        sequence = result.oldestSequence - 1;
    }
    for (auto const& event : mEvents) {
        if (event.sequence > sequence) {
            result.events.push_back(event);
        }
    }
    return result;
}

} // namespace filament::uboviewer
