#pragma once

#include "emby_client.hpp"

#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

struct DownloadResult {
    bool success = false;
    std::string path;
    uint64_t size = 0;
    std::string formattedSize;
    bool verified = false;
    std::string error;
};

namespace MediaDownloader {
    DownloadResult downloadFullVideo(
        EmbyClient& client,
        const std::string& itemId,
        const nlohmann::json& source,
        const std::string& outputPath,
        uint64_t expectedSize
    );

    DownloadResult downloadSubtitle(
        EmbyClient& client,
        const std::string& itemId,
        const std::string& sourceId,
        const nlohmann::json& subtitleStream,
        const std::string& outputPath
    );
}
