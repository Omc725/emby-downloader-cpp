#include "audio_extractor.hpp"
#include "ansi_colors.hpp"
#include "utils.hpp"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <regex>
#include <cmath>
#include <chrono>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace AudioExtractor {

static bool runFfmpegCommand(
    const std::string& inputUrl,
    const std::string& mapArg,
    const std::string& outputPath,
    double totalDurationSeconds,
    int bitRate,
    std::string& errorOutput
) {
    std::ostringstream cmd;
    cmd << "ffmpeg -y -hide_banner -loglevel info -stats "
        << "-reconnect 1 -reconnect_at_eof 1 -reconnect_streamed 1 "
        << "-reconnect_on_network_error 1 -reconnect_delay_max 15 "
        << "-i \"" << inputUrl << "\" ";

    if (!mapArg.empty()) {
        cmd << "-map " << mapArg << " ";
    }

    cmd << "-vn -c:a copy \"" << outputPath << "\" 2>&1";

    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        errorOutput = "ffmpeg alt sureci baslatilamadi.";
        return false;
    }

    uint64_t expectedTotalBytes = (bitRate > 0 && totalDurationSeconds > 0)
        ? static_cast<uint64_t>(std::round((bitRate / 8.0) * totalDurationSeconds))
        : 0;

    auto startWall = std::chrono::steady_clock::now();
    auto lastPrintTime = std::chrono::steady_clock::now();

    std::string buffer;
    int ch;
    while ((ch = fgetc(pipe)) != EOF) {
        if (ch == '\r' || ch == '\n') {
            if (!buffer.empty()) {
                errorOutput += buffer + "\n";

                // ffmpeg stats ayristirma
                if (buffer.find("size=") != std::string::npos && buffer.find("time=") != std::string::npos) {
                    auto now = std::chrono::steady_clock::now();
                    double elapsedMs = std::chrono::duration<double, std::milli>(now - lastPrintTime).count();

                    if (elapsedMs > 200.0) {
                        lastPrintTime = now;

                        // size ayristir (kB)
                        uint64_t currentBytes = 0;
                        std::smatch mSize;
                        if (std::regex_search(buffer, mSize, std::regex("size=\\s*([0-9]+)kB"))) {
                            currentBytes = std::stoull(mSize[1].str()) * 1024;
                        }

                        // time ayristir (HH:MM:SS.ms)
                        double currentTimeSec = 0.0;
                        std::smatch mTime;
                        if (std::regex_search(buffer, mTime, std::regex("time=([0-9]{2}):([0-9]{2}):([0-9]{2}\\.[0-9]+)"))) {
                            double h = std::stod(mTime[1].str());
                            double m = std::stod(mTime[2].str());
                            double s = std::stod(mTime[3].str());
                            currentTimeSec = (h * 3600.0) + (m * 60.0) + s;
                        }

                        // speed ayristir (...x)
                        std::string speedStr = "N/A";
                        std::smatch mSpeed;
                        if (std::regex_search(buffer, mSpeed, std::regex("speed=\\s*([0-9.]+)x"))) {
                            speedStr = mSpeed[1].str() + "x";
                        }

                        double percent = 0.0;
                        if (totalDurationSeconds > 0.0 && currentTimeSec > 0.0) {
                            percent = std::min(100.0, (currentTimeSec / totalDurationSeconds) * 100.0);
                        }

                        const int barWidth = 20;
                        int filled = std::min(barWidth, static_cast<int>(std::round((percent / 100.0) * barWidth)));
                        std::string bar = "";
                        for (int i = 0; i < filled; ++i) bar += "█";
                        for (int i = filled; i < barWidth; ++i) bar += "░";

                        std::string currentSizeStr = Utils::formatBytes(currentBytes);
                        std::string totalSizeStr = (expectedTotalBytes > 0) ? Utils::formatBytes(expectedTotalBytes) : "? MB";

                        double wallElapsedSec = std::chrono::duration<double>(now - startWall).count();
                        double speedBps = (wallElapsedSec > 0.1) ? (static_cast<double>(currentBytes) / wallElapsedSec) : 0.0;
                        std::string bpsStr = Utils::formatBytes(static_cast<uint64_t>(speedBps)) + "/s";
                        double mbps = (speedBps * 8.0) / (1000.0 * 1000.0);
                        std::ostringstream mbpsSs;
                        mbpsSs << std::fixed << std::setprecision(1) << mbps << " Mbps";

                        std::string etaStr = "";
                        if (percent > 0.0 && percent < 100.0 && speedBps > 1000.0 && expectedTotalBytes > currentBytes) {
                            uint64_t remainBytes = expectedTotalBytes - currentBytes;
                            uint64_t remainSec = static_cast<uint64_t>(std::round(static_cast<double>(remainBytes) / speedBps));
                            uint64_t m = remainSec / 60;
                            uint64_t s = remainSec % 60;
                            std::ostringstream etaSs;
                            etaSs << " | " << Colors::BrightCyan << "Kalan: " << Colors::Reset
                                  << std::setw(2) << std::setfill('0') << m << ":"
                                  << std::setw(2) << std::setfill('0') << s;
                            etaStr = etaSs.str();
                        }

                        std::ostringstream status;
                        status << "\r" << Colors::Cyan << "[Ses Cikariliyor (Kopyalama)]" << Colors::Reset
                               << " [" << Colors::Green << bar << Colors::Reset << "] "
                               << Colors::BrightGreen << std::setw(5) << std::fixed << std::setprecision(1) << percent << "%" << Colors::Reset
                               << " | " << Colors::BrightWhite << currentSizeStr << " / " << totalSizeStr << Colors::Reset
                               << " | " << Colors::BrightYellow << "Hiz: " << speedStr << " (" << bpsStr << " - " << mbpsSs.str() << ")" << Colors::Reset
                               << etaStr << "   ";

                        std::cout << status.str() << std::flush;
                    }
                }
                buffer.clear();
            }
        } else {
            buffer += static_cast<char>(ch);
        }
    }

    int status = pclose(pipe);
    std::cout << "\r" << std::string(110, ' ') << "\r" << std::flush;

    return (status == 0) && fs::exists(outputPath) && (fs::file_size(outputPath) > 0);
}

DownloadResult extractAudioStream(
    const std::string& directAudioUrl,
    const std::string& fallbackVideoUrl,
    const json& audioStream,
    const std::string& outputPath,
    double totalDurationSeconds
) {
    DownloadResult result;
    result.path = outputPath;

    fs::path outP(outputPath);
    if (outP.has_parent_path()) {
        fs::create_directories(outP.parent_path());
    }

    int bitRate = audioStream.value("BitRate", 0);
    int audioIndex = audioStream.value("Index", 0);
    std::string err;

    // 1. Dogrudan ses akis URL'sini dene
    bool ok = runFfmpegCommand(directAudioUrl, "", outputPath, totalDurationSeconds, bitRate, err);

    // 2. Basarisiz olursa video akisi uzerinden map ile dene
    if (!ok && !fallbackVideoUrl.empty()) {
        std::cout << "\n" << Colors::Yellow << "[BILGI] Dogrudan ses akisi alinamadi, video kapsayicisi uzerinden kopyalaniyor..." << Colors::Reset << "\n";
        std::string mapArg = "0:" + std::to_string(audioIndex);
        ok = runFfmpegCommand(fallbackVideoUrl, mapArg, outputPath, totalDurationSeconds, bitRate, err);
    }

    if (ok && fs::exists(outputPath)) {
        uint64_t size = fs::file_size(outputPath);
        result.success = true;
        result.size = size;
        result.formattedSize = Utils::formatBytes(size);
    } else {
        result.error = "Ses akisi cikarilamadi: " + err;
    }

    return result;
}

}
