#include "media_downloader.hpp"
#include "ansi_colors.hpp"
#include "utils.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <iomanip>
#include <cmath>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace MediaDownloader {

DownloadResult downloadFullVideo(
    EmbyClient& client,
    const std::string& itemId,
    const json& source,
    const std::string& outputPath,
    uint64_t expectedSize
) {
    DownloadResult result;
    result.path = outputPath;

    // Hedef dizini oluştur
    fs::path outP(outputPath);
    if (outP.has_parent_path()) {
        fs::create_directories(outP.parent_path());
    }

    std::string partPath = outputPath + ".part";
    std::string videoUrl = client.buildDirectVideoUrl(itemId, source);

    // Resmi indirme uç noktası kontrolü (/Items/{Id}/Download)
    try {
        std::string directDownloadUrl = client.getServerUrl() + client.getPrefix() + "/Items/" + itemId + "/Download?api_key=" + Utils::urlEncode(client.getToken());
        HttpResponse testRes = client.getHttpClient().head(directDownloadUrl, client.getHeaders());
        if (testRes.isOk()) {
            videoUrl = directDownloadUrl;
        }
    } catch (...) {}

    const int maxRetries = 5;

    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        uint64_t existingBytes = 0;
        if (fs::exists(partPath)) {
            try {
                existingBytes = fs::file_size(partPath);
            } catch (...) {}
        }

        if (expectedSize > 0 && existingBytes >= expectedSize) {
            if (fs::exists(outputPath)) fs::remove(outputPath);
            fs::rename(partPath, outputPath);
            result.success = true;
            result.size = existingBytes;
            result.formattedSize = Utils::formatBytes(existingBytes);
            result.verified = (existingBytes == expectedSize);
            return result;
        }

        auto lastPrintTime = std::chrono::steady_clock::now();

        auto progressCallback = [&](uint64_t currentBytes, uint64_t totalBytes, double speedBps) -> bool {
            auto now = std::chrono::steady_clock::now();
            double printElapsedMs = std::chrono::duration<double, std::milli>(now - lastPrintTime).count();
            if (printElapsedMs > 200.0) {
                lastPrintTime = now;

                double percent = 0.0;
                if (totalBytes > 0) {
                    percent = std::min(100.0, (static_cast<double>(currentBytes) / static_cast<double>(totalBytes)) * 100.0);
                }

                std::string line = Utils::buildProgressLine("Dogrudan Indiriliyor", percent, currentBytes, totalBytes, speedBps);
                std::cout << line << std::flush;
            }
            return true;
        };

        bool ok = client.getHttpClient().downloadToFile(
            videoUrl,
            partPath,
            existingBytes,
            expectedSize,
            client.getHeaders(),
            progressCallback
        );

        // Satiri temizle
        std::cout << "\r\033[2K" << std::flush;

        if (ok) {
            if (fs::exists(outputPath)) fs::remove(outputPath);
            fs::rename(partPath, outputPath);

            uint64_t finalSize = fs::file_size(outputPath);
            result.success = true;
            result.size = finalSize;
            result.formattedSize = Utils::formatBytes(finalSize);
            result.verified = (expectedSize > 0 && finalSize == expectedSize);
            return result;
        }

        if (attempt < maxRetries) {
            std::cout << "\n" << Colors::BrightYellow << "[UYARI] Dogrudan video indirme kesintisi. 3 saniye icinde kaldigi yerden ("
                      << Utils::formatBytes(existingBytes) << ") devam edilecek... (Deneme "
                      << attempt << "/" << maxRetries << ")" << Colors::Reset << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
        } else {
            result.error = "Video indirme " + std::to_string(maxRetries) + " deneme sonrasinda tamamlanamadi.";
        }
    }

    return result;
}

DownloadResult downloadSubtitle(
    EmbyClient& client,
    const std::string& itemId,
    const std::string& sourceId,
    const json& subtitleStream,
    const std::string& outputPath
) {
    DownloadResult result;
    result.path = outputPath;

    fs::path outP(outputPath);
    if (outP.has_parent_path()) {
        fs::create_directories(outP.parent_path());
    }

    int subIndex = subtitleStream.value("Index", 0);
    std::string codec = subtitleStream.value("Codec", "srt");
    std::string ext = (codec == "subrip" || codec == "srt") ? "srt" : codec;

    std::string subUrl = client.getSubtitleUrl(itemId, sourceId, subIndex, ext);
    HttpResponse res = client.getHttpClient().get(subUrl, client.getHeaders());

    if (!res.isOk() && ext != "srt") {
        std::string fallbackUrl = client.getSubtitleUrl(itemId, sourceId, subIndex, "srt");
        res = client.getHttpClient().get(fallbackUrl, client.getHeaders());
    }

    if (!res.isOk()) {
        result.error = "Altyazi indirilemedi (HTTP " + std::to_string(res.statusCode) + "): " + res.error;
        return result;
    }

    std::ofstream out(outputPath, std::ios::binary);
    if (!out.is_open()) {
        result.error = "Altyazi dosyasina yazilamadi: " + outputPath;
        return result;
    }

    out << res.body;
    out.close();

    uint64_t size = fs::file_size(outputPath);
    result.success = true;
    result.size = size;
    result.formattedSize = Utils::formatBytes(size);
    return result;
}

}
