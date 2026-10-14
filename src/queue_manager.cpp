#include "queue_manager.hpp"
#include "media_downloader.hpp"
#include "audio_extractor.hpp"
#include "config_manager.hpp"
#include "ansi_colors.hpp"
#include "utils.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;
using json = nlohmann::json;

QueueManager::QueueManager() {
    load();
}

std::string QueueManager::getQueueFilePath() {
    return ConfigManager::getConfigDir() + "/queue.json";
}

bool QueueManager::load() {
    m_items.clear();
    std::string path = getQueueFilePath();

    if (!fs::exists(path)) {
        const char* home = std::getenv("HOME");
        if (home) {
            std::string legacyPath = std::string(home) + "/.config/emby-audio-downloader/queue.json";
            if (fs::exists(legacyPath)) {
                path = legacyPath;
            }
        }
    }

    if (!fs::exists(path)) return false;

    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        json j;
        file >> j;

        if (j.is_array()) {
            for (const auto& it : j) {
                QueueItem item;
                item.id = it.value("id", "");
                item.type = it.value("type", "video");
                item.typeLabel = it.value("typeLabel", "");
                item.name = it.value("name", "");
                item.outputPath = it.value("outputPath", "");
                item.itemId = it.value("itemId", "");
                item.sourceId = it.value("sourceId", "");
                item.container = it.value("container", "mkv");
                item.expectedSize = it.value("expectedSize", static_cast<uint64_t>(0));
                item.totalDurationSeconds = it.value("totalDurationSeconds", 0.0);
                if (it.contains("audioStream")) item.audioStream = it["audioStream"];
                if (it.contains("subtitleStream")) item.subtitleStream = it["subtitleStream"];
                item.status = it.value("status", "pending");
                item.error = it.value("error", "");
                m_items.push_back(item);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool QueueManager::save() {
    try {
        fs::create_directories(ConfigManager::getConfigDir());
        std::ofstream file(getQueueFilePath());
        if (!file.is_open()) return false;

        json j = json::array();
        for (const auto& it : m_items) {
            json item;
            item["id"] = it.id;
            item["type"] = it.type;
            item["typeLabel"] = it.typeLabel;
            item["name"] = it.name;
            item["outputPath"] = it.outputPath;
            item["itemId"] = it.itemId;
            item["sourceId"] = it.sourceId;
            item["container"] = it.container;
            item["expectedSize"] = it.expectedSize;
            item["totalDurationSeconds"] = it.totalDurationSeconds;
            item["audioStream"] = it.audioStream;
            item["subtitleStream"] = it.subtitleStream;
            item["status"] = it.status;
            item["error"] = it.error;
            j.push_back(item);
        }

        file << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

void QueueManager::add(QueueItem item) {
    if (item.id.empty()) {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        item.id = std::to_string(now);
    }
    item.status = "pending";
    item.error.clear();
    m_items.push_back(item);
    save();
}

void QueueManager::addAll(const std::vector<QueueItem>& items) {
    for (auto item : items) {
        if (item.id.empty()) {
            auto now = std::chrono::system_clock::now().time_since_epoch().count();
            item.id = std::to_string(now);
        }
        item.status = "pending";
        item.error.clear();
        m_items.push_back(item);
    }
    save();
}

bool QueueManager::remove(size_t index) {
    if (index < m_items.size()) {
        m_items.erase(m_items.begin() + index);
        save();
        return true;
    }
    return false;
}

void QueueManager::clear() {
    m_items.clear();
    save();
}

size_t QueueManager::getPendingCount() const {
    size_t count = 0;
    for (const auto& it : m_items) {
        if (it.status == "pending") count++;
    }
    return count;
}

std::vector<QueueItem>& QueueManager::getItems() {
    return m_items;
}

const std::vector<QueueItem>& QueueManager::getItems() const {
    return m_items;
}

void QueueManager::processQueue(EmbyClient& client) {
    std::vector<size_t> pendingIndices;
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].status == "pending") {
            pendingIndices.push_back(i);
        }
    }

    if (pendingIndices.empty()) {
        std::cout << "\n" << Colors::Yellow << "[BILGI] Kuyrukta bekleyen herhangi bir indirme gorevi bulunmuyor." << Colors::Reset << "\n\n";
        return;
    }

    Utils::printHeader("INDIRME KUYRUGU CALISTIRILIYOR (" + std::to_string(pendingIndices.size()) + " Gorev)");

    size_t completedCount = 0;
    size_t errorCount = 0;

    for (size_t idx = 0; idx < pendingIndices.size(); ++idx) {
        size_t realIdx = pendingIndices[idx];
        auto& item = m_items[realIdx];

        std::cout << "\n" << Colors::Bold << Colors::BrightCyan << "[" << (idx + 1) << "/" << pendingIndices.size() << "] ["
                  << item.typeLabel << "] " << item.name << Colors::Reset << "\n";
        std::cout << Colors::Dim << "Kayit Yolu: " << item.outputPath << Colors::Reset << "\n";

        item.status = "downloading";
        save();

        auto startTime = std::chrono::steady_clock::now();
        DownloadResult res;

        if (item.type == "video") {
            std::cout << Colors::Green << "[BILGI] Orijinal dosya hicbir donusturme uygulanmadan dogrudan indiriliyor (Bit-for-bit direct download)..." << Colors::Reset << "\n";
            json sourceObj;
            sourceObj["Id"] = item.sourceId;
            sourceObj["Container"] = item.container;
            sourceObj["Size"] = item.expectedSize;
            res = MediaDownloader::downloadFullVideo(client, item.itemId, sourceObj, item.outputPath, item.expectedSize);
        } else if (item.type == "audio") {
            json sourceObj;
            sourceObj["Id"] = item.sourceId;
            sourceObj["Container"] = item.container;
            std::string directAudioUrl = client.buildDirectAudioStreamUrl(item.itemId, sourceObj, item.audioStream);
            std::string fallbackVideoUrl = client.buildDirectVideoUrl(item.itemId, sourceObj);
            res = AudioExtractor::extractAudioStream(directAudioUrl, fallbackVideoUrl, item.audioStream, item.outputPath, item.totalDurationSeconds);
        } else if (item.type == "subtitle") {
            res = MediaDownloader::downloadSubtitle(client, item.itemId, item.sourceId, item.subtitleStream, item.outputPath);
        }

        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();

        if (res.success) {
            item.status = "completed";
            item.error.clear();
            completedCount++;
            save();

            std::string verifyNote = "";
            if (item.type == "video" && res.verified) {
                verifyNote = " | " + Colors::BrightGreen + "[Birebir Dogrulandi]" + Colors::Reset;
            }

            std::ostringstream ss;
            ss << std::fixed << std::setprecision(1) << elapsed;
            std::cout << Colors::Green << "[TAMAMLANDI]" << Colors::Reset << " Dosya kaydedildi: "
                      << res.formattedSize << " (" << ss.str() << " sn)" << verifyNote << "\n";
        } else {
            item.status = "error";
            item.error = res.error;
            errorCount++;
            save();

            std::cout << Colors::Red << "[BASARISIZ]" << Colors::Reset << " " << res.error << "\n";
        }
    }

    std::cout << "\n" << Colors::Bold << "Kuyruk Tamamlandi: "
              << Colors::Green << completedCount << " Basarili" << Colors::Reset << ", "
              << (errorCount > 0 ? Colors::Red : Colors::Dim) << errorCount << " Hatali" << Colors::Reset << "\n\n";
}
