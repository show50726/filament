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

#include <baviewer/DebugServer.h>
#include <baviewer/BufferAllocatorInfo.h>

#include "ApiHandler.h"

#include <CivetServer.h>

#include <utils/Log.h>
#include <utils/Mutex.h>
#include <utils/ostream.h>

#include <mutex>
#include <string>
#include <string_view>


// If set to 0, this serves HTML from a resgen resource. Use 1 only during local development, which
// serves files directly from the source code tree.
#define SERVE_FROM_SOURCE_TREE 0

#if SERVE_FROM_SOURCE_TREE

namespace {
std::string const BASE_URL = "libs/baviewer/web";
} // anonymous

#else

#include "baviewer_resources.h"
#include <unordered_map>

namespace {

struct Asset {
    std::string_view mime;
    std::string_view data;
};

std::unordered_map<std::string_view, Asset> ASSET_MAP;

} // anonymous

#endif // SERVE_FROM_SOURCE_TREE

namespace filament::baviewer {

using namespace utils;

std::string_view const DebugServer::kSuccessHeader =
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n"
        "Connection: close\r\n\r\n";

std::string_view const DebugServer::kErrorHeader =
        "HTTP/1.1 404 Not Found\r\nContent-Type: %s\r\n"
        "Connection: close\r\n\r\n";


class FileRequestHandler : public CivetHandler {
public:
    FileRequestHandler(DebugServer* server) {}
    bool handleGet(CivetServer *server, struct mg_connection *conn) {
        auto const& kSuccessHeader = DebugServer::kSuccessHeader;
        struct mg_request_info const* request = mg_get_request_info(conn);
        std::string uri(request->request_uri);
        if (uri == "/") {
            uri = "/index.html";
        }

#if SERVE_FROM_SOURCE_TREE
        if (uri == "/index.html" || uri == "/app.js" || uri == "/api.js") {
            mg_send_file(conn, (BASE_URL + uri).c_str());
            return true;
        }
#else
        auto const& asset_itr = ASSET_MAP.find(uri);
        if (asset_itr != ASSET_MAP.end()) {
            auto const& mime = asset_itr->second.mime;
            auto const& data = asset_itr->second.data;
            mg_printf(conn, kSuccessHeader.data(), mime.data());
            mg_write(conn, data.data(), data.size());
            return true;
        }
#endif
        slog.e << "[baviewer] DebugServer: bad request at line " <<  __LINE__ << ": " << uri << io::endl;
        return false;
    }
};

DebugServer::DebugServer(int port, size_t historySize) : mMaxHistorySize(historySize) {
#if !SERVE_FROM_SOURCE_TREE
    ASSET_MAP["/index.html"] = {
        .mime = "text/html",
        .data = {(char const*) BAVIEWER_RESOURCES_INDEX_DATA},
    };
    ASSET_MAP["/app.js"] = {
        .mime = "text/javascript",
        .data = {(char const*) BAVIEWER_RESOURCES_APP_DATA},
    };
    ASSET_MAP["/api.js"] = {
        .mime = "text/javascript",
        .data = {(char const*) BAVIEWER_RESOURCES_API_DATA},
    };
#endif

    const char* kServerOptions[] = {
        "listening_ports", "8085",
        "num_threads", "10",
        "error_log_file", "civetweb.txt",
        nullptr
    };
    std::string portString = std::to_string(port);
    kServerOptions[1] = portString.c_str();

    mServer = new CivetServer(kServerOptions);
    if (!mServer->getContext()) {
        delete mServer;
        mServer = nullptr;
        slog.e << "[baviewer] Unable to start DebugServer, see civetweb.txt for details." << io::endl;
        return;
    }

    mFileHandler = new FileRequestHandler(this);
    mApiHandler = new ApiHandler(this);

    mServer->addHandler("/api", mApiHandler);
    mServer->addHandler("", mFileHandler);

    slog.i << "[baviewer] DebugServer listening at http://localhost:" << port << io::endl;
}

DebugServer::~DebugServer() {
    mServer->close();

    delete mFileHandler;
    delete mApiHandler;
    delete mServer;
}

void DebugServer::update(BufferAllocatorInfo info) {
    if (mPaused.load()) {
        return;
    }
    std::unique_lock<utils::Mutex> lock(mHistoryMutex);
    info.frameId = mFrameCounter++;
    if (!mHistory.empty()) {
        info.hasChanged = !(info == mHistory.back());
    } else {
        info.hasChanged = true;
    }

    if (info.hasChanged) {
        if (mHistory.size() >= mMaxHistorySize) {
            mHistory.pop_front();
        }
        mHistory.push_back(std::move(info));
        mApiHandler->notify();
    }
}

void DebugServer::setPaused(bool paused) {
    mPaused.store(paused);
}

} // namespace filament::baviewer
