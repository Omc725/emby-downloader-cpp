#pragma once

#include "media_downloader.hpp"

#include <string>
#include <nlohmann/json.hpp>

namespace AudioExtractor {
    DownloadResult extractAudioStream(
        const std::string& directAudioUrl,
        const std::string& fallbackVideoUrl,
        const nlohmann::json& audioStream,
        const std::string& outputPath,
        double totalDurationSeconds = 0.0
    );
}
