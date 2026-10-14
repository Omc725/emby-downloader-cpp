#include "ansi_colors.hpp"
#include "utils.hpp"
#include "http_client.hpp"
#include "config_manager.hpp"
#include "emby_client.hpp"
#include "media_downloader.hpp"
#include "audio_extractor.hpp"
#include "queue_manager.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <set>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using json = nlohmann::json;

struct MediaMeta {
    bool isTv = false;
    std::string title;
    std::string year;
    std::string seriesName;
    int seasonIndex = 1;
    int episodeIndex = 1;
    std::string episodeName;
    double runTimeSeconds = 0.0;
};

// Fonksiyon bildirimleri
std::string promptExecutionChoice(std::vector<QueueItem>& tasks, QueueManager& queue, EmbyClient& client);
void handleQueueFlow(EmbyClient& client, QueueManager& queue);
void handleDownloadTypesFlow(EmbyClient& client, const std::string& itemId, const json& selectedSource, const json& item, const MediaMeta& meta, QueueManager& queue);
void handleMovieFlow(EmbyClient& client, const json& item, QueueManager& queue);
void handleEpisodeFlow(EmbyClient& client, const json& seriesItem, const json& season, const json& episode, QueueManager& queue);
void handleBatchEpisodesFlow(EmbyClient& client, const json& seriesItem, const json& season, const std::vector<json>& selectedEpisodes, QueueManager& queue);
void handleSeasonFlow(EmbyClient& client, const json& seriesItem, const json& season, QueueManager& queue);
void handleSeriesFlow(EmbyClient& client, const json& seriesItem, QueueManager& queue);
void searchMenu(EmbyClient& client, QueueManager& queue);
EmbyClient setupAuthentication(ConfigManager& config);

std::string promptExecutionChoice(std::vector<QueueItem>& tasks, QueueManager& queue, EmbyClient& client) {
    while (true) {
        std::cout << "\n" << Colors::Bold << "Secilen Gorev Sayisi: " << Colors::Green << tasks.size() << Colors::Reset << "\n";
        std::cout << "  " << Colors::Green << "[1]" << Colors::Reset << " Hemen Indir\n";
        std::cout << "  " << Colors::Cyan << "[2]" << Colors::Reset << " Kuyruga Ekle\n";
        std::cout << "  " << Colors::Dim << "[0] Geri Don" << Colors::Reset << "\n\n";

        std::string choice = Utils::ask(Colors::Bold + "Islem secin (1-2, 0: Geri): " + Colors::Reset);
        if (choice == "0" || choice.empty()) {
            return "BACK";
        }

        if (choice == "2") {
            queue.addAll(tasks);
            std::cout << "\n" << Colors::Green << "[BILGI] " << tasks.size() << " adet gorev indirme kuyruguna eklendi." << Colors::Reset << "\n\n";
            Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
            return "DONE";
        } else if (choice == "1") {
            queue.addAll(tasks);
            queue.processQueue(client);
            Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
            return "DONE";
        }
    }
}

void handleQueueFlow(EmbyClient& client, QueueManager& queue) {
    while (true) {
        Utils::printHeader("INDIRME KUYRUGU YONETIMI");
        auto& items = queue.getItems();

        if (items.empty()) {
            std::cout << Colors::Yellow << "Kuyrukta kayitli herhangi bir indirme gorevi bulunmuyor." << Colors::Reset << "\n\n";
            std::cout << "  " << Colors::Dim << "[0] Ana Menuye Don" << Colors::Reset << "\n\n";
            std::string c = Utils::ask("Seciminiz (0): ");
            if (c == "0" || c.empty()) return;
            continue;
        }

        std::cout << Colors::Bold << "Kuyruk Listesi (" << items.size() << " Gorev):" << Colors::Reset << "\n";
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& it = items[i];
            std::string stColor = Colors::Yellow;
            std::string stLabel = "Bekliyor";
            if (it.status == "completed") {
                stColor = Colors::Green;
                stLabel = "Tamamlandi";
            } else if (it.status == "downloading") {
                stColor = Colors::BrightCyan;
                stLabel = "Indiriliyor";
            } else if (it.status == "error") {
                stColor = Colors::Red;
                stLabel = "Hata";
            }

            std::string sizeStr = it.expectedSize > 0 ? " (" + Utils::formatBytes(it.expectedSize) + ")" : "";
            std::cout << "  [" << (i + 1) << "] [" << it.typeLabel << "] " << it.name << sizeStr
                      << " - " << stColor << "[" << stLabel << "]" << Colors::Reset << "\n";
            if (!it.error.empty()) {
                std::cout << "      " << Colors::Red << "Hata: " << it.error << Colors::Reset << "\n";
            }
        }

        std::cout << "\n  " << Colors::Green << "[1]" << Colors::Reset << " Bekleyenleri Simdi Indir (" << queue.getPendingCount() << " Gorev)\n";
        std::cout << "  " << Colors::Yellow << "[2]" << Colors::Reset << " Kuyruktan Oge Sil\n";
        std::cout << "  " << Colors::Red << "[3]" << Colors::Reset << " Kuyrugu Tamamen Temizle\n";
        std::cout << "  " << Colors::Dim << "[0] Ana Menuye Don" << Colors::Reset << "\n\n";

        std::string choice = Utils::ask(Colors::Bold + "Seciminiz (1-3, 0: Geri): " + Colors::Reset);
        if (choice == "0" || choice.empty()) return;

        if (choice == "1") {
            queue.processQueue(client);
            Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
        } else if (choice == "2") {
            std::string delIdxStr = Utils::ask("Silinecek gorev numarasi (0: Iptal): ");
            try {
                int d = std::stoi(delIdxStr);
                if (d >= 1 && static_cast<size_t>(d) <= items.size()) {
                    queue.remove(static_cast<size_t>(d - 1));
                    std::cout << Colors::Green << "[BILGI] Gorev kuyruktan silindi." << Colors::Reset << "\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(800));
                }
            } catch (...) {}
        } else if (choice == "3") {
            std::string confirm = Utils::ask("Kuyruktaki tum gorevler silinsin mi? (e/h): ");
            if (Utils::toLower(confirm) == "e") {
                queue.clear();
                std::cout << Colors::Green << "[BILGI] Kuyruk temizlendi." << Colors::Reset << "\n";
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
            }
        }
    }
}

void handleDownloadTypesFlow(
    EmbyClient& client,
    const std::string& itemId,
    const json& selectedSource,
    const json& /*item*/,
    const MediaMeta& meta,
    QueueManager& queue
) {
    std::string downloadDir = "./downloads";
    fs::create_directories(downloadDir);

    std::string container = "mkv";
    if (selectedSource.contains("Container") && selectedSource["Container"].is_string()) {
        container = Utils::toLower(selectedSource["Container"].get<std::string>());
    }

    uint64_t sourceSize = selectedSource.value("Size", static_cast<uint64_t>(0));
    std::string sizeStr = sourceSize > 0 ? Utils::formatBytes(sourceSize) : "";

    std::vector<json> audioStreams;
    std::vector<json> subtitleStreams;

    if (selectedSource.contains("MediaStreams") && selectedSource["MediaStreams"].is_array()) {
        for (const auto& s : selectedSource["MediaStreams"]) {
            std::string t = s.value("Type", "");
            if (t == "Audio") audioStreams.push_back(s);
            else if (t == "Subtitle") subtitleStreams.push_back(s);
        }
    }

    std::string sourceId = selectedSource.value("Id", "");

    while (true) {
        std::string headerTitle = meta.isTv 
            ? (meta.seriesName + " - S" + std::to_string(meta.seasonIndex) + "E" + std::to_string(meta.episodeIndex) + " (" + meta.episodeName + ")")
            : (meta.title + (!meta.year.empty() ? " (" + meta.year + ")" : ""));

        Utils::printHeader("INDIRME SECENEKLERI - " + headerTitle);
        std::string srcName = selectedSource.value("Name", "Varsayilan");
        std::cout << "Secilen Surum: " << Colors::Bold << srcName << Colors::Reset 
                  << " (" << Utils::toLower(container) << (sizeStr.empty() ? "" : " | " + sizeStr) << ")\n\n";

        std::cout << "  " << Colors::Green << "[1]" << Colors::Reset << " Sadece Ses Indir " 
                  << Colors::Dim << "(Donusturmesiz Stream Copy - " << audioStreams.size() << " Ses Parcasi)" << Colors::Reset << "\n";
        std::cout << "  " << Colors::Green << "[2]" << Colors::Reset << " Sadece Altyazi Indir " 
                  << Colors::Dim << "(" << subtitleStreams.size() << " Altyazi Mevcut)" << Colors::Reset << "\n";
        std::cout << "  " << Colors::Green << "[3]" << Colors::Reset << " Orijinal Video Dosyasini Indir " 
                  << Colors::Dim << "(Donusturmesiz / Dogrudan Dosya Indirme" << (sizeStr.empty() ? "" : " - " + sizeStr) << ")" << Colors::Reset << "\n";
        std::cout << "  " << Colors::BrightCyan << "[4]" << Colors::Reset << " Tumunu Indir " 
                  << Colors::Dim << "(Orijinal Video + Tum Sesler + Tum Altyazilar)" << Colors::Reset << "\n";
        std::cout << "  " << Colors::Dim << "[0] Geri Don" << Colors::Reset << "\n\n";

        std::string dlType = Utils::ask(Colors::Bold + "Seciminiz (1-4, 0: Geri): " + Colors::Reset);
        if (dlType == "0" || dlType.empty()) return;

        if (dlType == "1") {
            // SADECE SES INDIR
            if (audioStreams.empty()) {
                std::cout << "\n" << Colors::Red << "[UYARI] Bu surumde ayristirilabilir ses akisi bulunamadi." << Colors::Reset << "\n";
                Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
                continue;
            }

            while (true) {
                Utils::printHeader("SES PARCASI SECIMI - " + headerTitle);
                std::cout << "Mevcut Ses Akislari:\n";
                for (size_t i = 0; i < audioStreams.size(); ++i) {
                    const auto& a = audioStreams[i];
                    std::string lang = a.value("Language", "und");
                    std::string title = a.value("Title", a.value("DisplayTitle", ""));
                    std::string codec = a.value("Codec", "");
                    std::string channels = a.value("ChannelLayout", std::to_string(a.value("Channels", 0)) + " kanal");
                    int br = a.value("BitRate", 0);
                    std::string brStr = br > 0 ? std::to_string(br / 1000) + " kbps" : "";

                    std::cout << "  [" << (i + 1) << "] " << Colors::Bold << lang << Colors::Reset
                              << (title.empty() ? "" : " (" + title + ")")
                              << " - " << codec << " " << channels
                              << (brStr.empty() ? "" : " [" + brStr + "]") << "\n";
                }
                std::cout << "\n  " << Colors::Dim << "[T] Tum Ses Parcalarini Indir" << Colors::Reset << "\n";
                std::cout << "  " << Colors::Dim << "[0] Geri Don" << Colors::Reset << "\n\n";

                std::string aChoice = Utils::ask(Colors::Bold + "Seciminiz (1-" + std::to_string(audioStreams.size()) + ", T: Tumu, 0: Geri): " + Colors::Reset);
                if (aChoice == "0" || aChoice.empty()) break;

                auto indices = Utils::parseSelectionIndices(aChoice, audioStreams.size());
                if (indices.empty()) {
                    std::cout << Colors::Red << "Gecersiz secim." << Colors::Reset << "\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(800));
                    continue;
                }

                std::vector<QueueItem> tasks;
                for (size_t idx : indices) {
                    const auto& stream = audioStreams[idx];
                    std::string codec = stream.value("Codec", "mka");
                    std::string ext = Utils::getAudioExtension(codec);
                    std::string lang = stream.value("Language", "und");

                    std::string baseFilename;
                    if (meta.isTv) {
                        std::ostringstream ss;
                        ss << meta.seriesName << " - S" << std::setw(2) << std::setfill('0') << meta.seasonIndex
                           << "E" << std::setw(2) << std::setfill('0') << meta.episodeIndex << " - "
                           << meta.episodeName << "." << lang << "." << ext;
                        baseFilename = ss.str();
                    } else {
                        std::string yStr = meta.year.empty() ? "" : " (" + meta.year + ")";
                        baseFilename = meta.title + yStr + "." + lang + "." + ext;
                    }

                    QueueItem t;
                    t.type = "audio";
                    t.typeLabel = "Ses (" + lang + ")";
                    t.name = baseFilename;
                    t.outputPath = downloadDir + "/" + Utils::sanitizeFilename(baseFilename);
                    t.itemId = itemId;
                    t.sourceId = sourceId;
                    t.container = container;
                    t.expectedSize = 0;
                    t.totalDurationSeconds = meta.runTimeSeconds;
                    t.audioStream = stream;
                    tasks.push_back(t);
                }

                std::string res = promptExecutionChoice(tasks, queue, client);
                if (res == "DONE") return;
            }

        } else if (dlType == "2") {
            // SADECE ALTYAZI INDIR
            if (subtitleStreams.empty()) {
                std::cout << "\n" << Colors::Red << "[UYARI] Bu surumde ayristirilabilir altyazi akisi bulunamadi." << Colors::Reset << "\n";
                Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
                continue;
            }

            while (true) {
                Utils::printHeader("ALTYAZI SECIMI - " + headerTitle);
                std::cout << "Mevcut Altyazilar:\n";
                for (size_t i = 0; i < subtitleStreams.size(); ++i) {
                    const auto& s = subtitleStreams[i];
                    std::string lang = s.value("Language", "und");
                    std::string title = s.value("Title", s.value("DisplayTitle", ""));
                    std::string codec = s.value("Codec", "srt");
                    bool isDefault = s.value("IsDefault", false);
                    bool isForced = s.value("IsForced", false);

                    std::string tags = "";
                    if (isDefault) tags += " [Varsayilan]";
                    if (isForced) tags += " [Forced]";

                    std::cout << "  [" << (i + 1) << "] " << Colors::Bold << lang << Colors::Reset
                              << (title.empty() ? "" : " (" + title + ")")
                              << " - " << codec << Colors::Dim << tags << Colors::Reset << "\n";
                }
                std::cout << "\n  " << Colors::Dim << "[T] Tum Altyazilari Indir" << Colors::Reset << "\n";
                std::cout << "  " << Colors::Dim << "[0] Geri Don" << Colors::Reset << "\n\n";

                std::string sChoice = Utils::ask(Colors::Bold + "Seciminiz (1-" + std::to_string(subtitleStreams.size()) + ", T: Tumu, 0: Geri): " + Colors::Reset);
                if (sChoice == "0" || sChoice.empty()) break;

                auto indices = Utils::parseSelectionIndices(sChoice, subtitleStreams.size());
                if (indices.empty()) {
                    std::cout << Colors::Red << "Gecersiz secim." << Colors::Reset << "\n";
                    std::this_thread::sleep_for(std::chrono::milliseconds(800));
                    continue;
                }

                std::vector<QueueItem> tasks;
                for (size_t idx : indices) {
                    const auto& stream = subtitleStreams[idx];
                    std::string codec = stream.value("Codec", "srt");
                    std::string ext = (codec == "subrip" || codec == "srt") ? "srt" : codec;
                    std::string lang = stream.value("Language", "und");

                    std::string baseFilename;
                    if (meta.isTv) {
                        std::ostringstream ss;
                        ss << meta.seriesName << " - S" << std::setw(2) << std::setfill('0') << meta.seasonIndex
                           << "E" << std::setw(2) << std::setfill('0') << meta.episodeIndex << " - "
                           << meta.episodeName << "." << lang << "." << ext;
                        baseFilename = ss.str();
                    } else {
                        std::string yStr = meta.year.empty() ? "" : " (" + meta.year + ")";
                        baseFilename = meta.title + yStr + "." + lang + "." + ext;
                    }

                    QueueItem t;
                    t.type = "subtitle";
                    t.typeLabel = "Altyazi (" + lang + ")";
                    t.name = baseFilename;
                    t.outputPath = downloadDir + "/" + Utils::sanitizeFilename(baseFilename);
                    t.itemId = itemId;
                    t.sourceId = sourceId;
                    t.expectedSize = 0;
                    t.subtitleStream = stream;
                    tasks.push_back(t);
                }

                std::string res = promptExecutionChoice(tasks, queue, client);
                if (res == "DONE") return;
            }

        } else if (dlType == "3") {
            // ORIJINAL VIDEO DOSYASINI INDIR (DONUSTURMESIZ / BIT-FOR-BIT)
            std::string defaultName;
            if (meta.isTv) {
                std::ostringstream ss;
                ss << meta.seriesName << " - S" << std::setw(2) << std::setfill('0') << meta.seasonIndex
                   << "E" << std::setw(2) << std::setfill('0') << meta.episodeIndex << " - "
                   << meta.episodeName << "." << container;
                defaultName = ss.str();
            } else {
                std::string yStr = meta.year.empty() ? "" : " (" + meta.year + ")";
                defaultName = meta.title + yStr + "." + container;
            }

            std::string safeFilename = Utils::getOriginalMediaFilename(selectedSource, defaultName);
            std::string targetPath = downloadDir + "/" + safeFilename;

            QueueItem t;
            t.type = "video";
            t.typeLabel = "Orijinal Video";
            t.name = safeFilename;
            t.outputPath = targetPath;
            t.itemId = itemId;
            t.sourceId = sourceId;
            t.container = container;
            t.expectedSize = sourceSize;

            std::vector<QueueItem> tasks = {t};
            std::string res = promptExecutionChoice(tasks, queue, client);
            if (res == "DONE") return;

        } else if (dlType == "4") {
            // TUMUNU INDIR (ORIJINAL VIDEO + TUM SESLER + TUM ALTYAZILAR)
            std::vector<QueueItem> tasks;

            // 1. Video
            std::string defaultVideoName;
            if (meta.isTv) {
                std::ostringstream ss;
                ss << meta.seriesName << " - S" << std::setw(2) << std::setfill('0') << meta.seasonIndex
                   << "E" << std::setw(2) << std::setfill('0') << meta.episodeIndex << " - "
                   << meta.episodeName << "." << container;
                defaultVideoName = ss.str();
            } else {
                std::string yStr = meta.year.empty() ? "" : " (" + meta.year + ")";
                defaultVideoName = meta.title + yStr + "." + container;
            }
            std::string safeVideoName = Utils::getOriginalMediaFilename(selectedSource, defaultVideoName);

            QueueItem vTask;
            vTask.type = "video";
            vTask.typeLabel = "Video";
            vTask.name = safeVideoName;
            vTask.outputPath = downloadDir + "/" + safeVideoName;
            vTask.itemId = itemId;
            vTask.sourceId = sourceId;
            vTask.container = container;
            vTask.expectedSize = sourceSize;
            tasks.push_back(vTask);

            // 2. Tum Sesler
            for (const auto& a : audioStreams) {
                std::string codec = a.value("Codec", "mka");
                std::string ext = Utils::getAudioExtension(codec);
                std::string lang = a.value("Language", "und");

                std::string baseFilename;
                if (meta.isTv) {
                    std::ostringstream ss;
                    ss << meta.seriesName << " - S" << std::setw(2) << std::setfill('0') << meta.seasonIndex
                       << "E" << std::setw(2) << std::setfill('0') << meta.episodeIndex << " - "
                       << meta.episodeName << "." << lang << "." << ext;
                    baseFilename = ss.str();
                } else {
                    std::string yStr = meta.year.empty() ? "" : " (" + meta.year + ")";
                    baseFilename = meta.title + yStr + "." + lang + "." + ext;
                }

                QueueItem aTask;
                aTask.type = "audio";
                aTask.typeLabel = "Ses (" + lang + ")";
                aTask.name = baseFilename;
                aTask.outputPath = downloadDir + "/" + Utils::sanitizeFilename(baseFilename);
                aTask.itemId = itemId;
                aTask.sourceId = sourceId;
                aTask.container = container;
                aTask.expectedSize = 0;
                aTask.totalDurationSeconds = meta.runTimeSeconds;
                aTask.audioStream = a;
                tasks.push_back(aTask);
            }

            // 3. Tum Altyazilar
            for (const auto& s : subtitleStreams) {
                std::string codec = s.value("Codec", "srt");
                std::string ext = (codec == "subrip" || codec == "srt") ? "srt" : codec;
                std::string lang = s.value("Language", "und");

                std::string baseFilename;
                if (meta.isTv) {
                    std::ostringstream ss;
                    ss << meta.seriesName << " - S" << std::setw(2) << std::setfill('0') << meta.seasonIndex
                       << "E" << std::setw(2) << std::setfill('0') << meta.episodeIndex << " - "
                       << meta.episodeName << "." << lang << "." << ext;
                    baseFilename = ss.str();
                } else {
                    std::string yStr = meta.year.empty() ? "" : " (" + meta.year + ")";
                    baseFilename = meta.title + yStr + "." + lang + "." + ext;
                }

                QueueItem sTask;
                sTask.type = "subtitle";
                sTask.typeLabel = "Altyazi (" + lang + ")";
                sTask.name = baseFilename;
                sTask.outputPath = downloadDir + "/" + Utils::sanitizeFilename(baseFilename);
                sTask.itemId = itemId;
                sTask.sourceId = sourceId;
                sTask.expectedSize = 0;
                sTask.subtitleStream = s;
                tasks.push_back(sTask);
            }

            std::string res = promptExecutionChoice(tasks, queue, client);
            if (res == "DONE") return;
        }
    }
}

void handleMovieFlow(EmbyClient& client, const json& item, QueueManager& queue) {
    std::string itemId = item.value("Id", "");
    json fullItem = client.getItem(itemId);

    std::vector<json> mediaSources;
    if (fullItem.contains("MediaSources") && fullItem["MediaSources"].is_array()) {
        mediaSources = fullItem["MediaSources"].get<std::vector<json>>();
    }

    if (mediaSources.empty()) {
        std::cout << "\n" << Colors::Red << "[HATA] Bu film icin medya kaynagi bulunamadi." << Colors::Reset << "\n";
        Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
        return;
    }

    MediaMeta meta;
    meta.isTv = false;
    meta.title = fullItem.value("Name", "Film");
    meta.year = fullItem.contains("ProductionYear") ? std::to_string(fullItem["ProductionYear"].get<int>()) : "";
    if (fullItem.contains("RunTimeTicks")) {
        meta.runTimeSeconds = fullItem["RunTimeTicks"].get<double>() / 10000000.0;
    }

    if (mediaSources.size() == 1) {
        handleDownloadTypesFlow(client, itemId, mediaSources[0], fullItem, meta, queue);
        return;
    }

    while (true) {
        Utils::printHeader("SURUM SECIMI - " + meta.title);
        std::cout << "Mevcut Surumler (" << mediaSources.size() << " Surum):\n";
        for (size_t i = 0; i < mediaSources.size(); ++i) {
            const auto& s = mediaSources[i];
            std::string sName = s.value("Name", "Varsayilan");
            std::string container = s.value("Container", "mkv");
            uint64_t size = s.value("Size", static_cast<uint64_t>(0));
            std::string sizeStr = size > 0 ? " (" + Utils::formatBytes(size) + ")" : "";

            std::cout << "  [" << (i + 1) << "] " << Colors::Bold << sName << Colors::Reset
                      << " - " << container << sizeStr << "\n";
        }
        std::cout << "  " << Colors::Dim << "[0] Arama Sonuclarina Don" << Colors::Reset << "\n\n";

        std::string choice = Utils::ask(Colors::Bold + "Surum secin (1-" + std::to_string(mediaSources.size()) + ", 0: Geri): " + Colors::Reset);
        if (choice == "0" || choice.empty()) return;

        try {
            int idx = std::stoi(choice);
            if (idx >= 1 && static_cast<size_t>(idx) <= mediaSources.size()) {
                handleDownloadTypesFlow(client, itemId, mediaSources[idx - 1], fullItem, meta, queue);
            }
        } catch (...) {}
    }
}

void handleEpisodeFlow(
    EmbyClient& client,
    const json& seriesItem,
    const json& season,
    const json& episode,
    QueueManager& queue
) {
    std::string epId = episode.value("Id", "");
    json fullEp = client.getItem(epId);

    std::vector<json> mediaSources;
    if (fullEp.contains("MediaSources") && fullEp["MediaSources"].is_array()) {
        mediaSources = fullEp["MediaSources"].get<std::vector<json>>();
    }

    if (mediaSources.empty()) {
        std::cout << "\n" << Colors::Red << "[HATA] Bu bolum icin medya kaynagi bulunamadi." << Colors::Reset << "\n";
        Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
        return;
    }

    MediaMeta meta;
    meta.isTv = true;
    meta.seriesName = seriesItem.value("Name", "Dizi");
    meta.seasonIndex = season.value("IndexNumber", 1);
    meta.episodeIndex = fullEp.value("IndexNumber", 1);
    meta.episodeName = fullEp.value("Name", "Bolum");
    if (fullEp.contains("RunTimeTicks")) {
        meta.runTimeSeconds = fullEp["RunTimeTicks"].get<double>() / 10000000.0;
    }

    if (mediaSources.size() == 1) {
        handleDownloadTypesFlow(client, epId, mediaSources[0], fullEp, meta, queue);
        return;
    }

    while (true) {
        Utils::printHeader("SURUM SECIMI - " + meta.seriesName + " S" + std::to_string(meta.seasonIndex) + "E" + std::to_string(meta.episodeIndex));
        std::cout << "Mevcut Surumler (" << mediaSources.size() << " Surum):\n";
        for (size_t i = 0; i < mediaSources.size(); ++i) {
            const auto& s = mediaSources[i];
            std::string sName = s.value("Name", "Varsayilan");
            std::string container = s.value("Container", "mkv");
            uint64_t size = s.value("Size", static_cast<uint64_t>(0));
            std::string sizeStr = size > 0 ? " (" + Utils::formatBytes(size) + ")" : "";

            std::cout << "  [" << (i + 1) << "] " << Colors::Bold << sName << Colors::Reset
                      << " - " << container << sizeStr << "\n";
        }
        std::cout << "  " << Colors::Dim << "[0] Bolum Listesine Don" << Colors::Reset << "\n\n";

        std::string choice = Utils::ask(Colors::Bold + "Surum secin (1-" + std::to_string(mediaSources.size()) + ", 0: Geri): " + Colors::Reset);
        if (choice == "0" || choice.empty()) return;

        try {
            int idx = std::stoi(choice);
            if (idx >= 1 && static_cast<size_t>(idx) <= mediaSources.size()) {
                handleDownloadTypesFlow(client, epId, mediaSources[idx - 1], fullEp, meta, queue);
            }
        } catch (...) {}
    }
}

void handleBatchEpisodesFlow(
    EmbyClient& client,
    const json& seriesItem,
    const json& season,
    const std::vector<json>& selectedEpisodes,
    QueueManager& queue
) {
    std::string downloadDir = "./downloads";
    fs::create_directories(downloadDir);

    while (true) {
        std::string sName = seriesItem.value("Name", "Dizi");
        std::string seaName = season.value("Name", "Sezon");
        Utils::printHeader("TOPLU BOLUM INDIRME - " + sName + " (" + seaName + ")");
        std::cout << "Secilen Bolum Sayisi: " << Colors::Bold << selectedEpisodes.size() << Colors::Reset << "\n\n";

        std::cout << "  " << Colors::Green << "[1]" << Colors::Reset << " Orijinal Video Dosyalarini Indir "
                  << Colors::Dim << "(Donusturmesiz / Dogrudan Dosya Indirme)" << Colors::Reset << "\n";
        std::cout << "  " << Colors::Green << "[2]" << Colors::Reset << " Belirli Bir Dildeki Ses Dosyalarini Indir\n";
        std::cout << "  " << Colors::Green << "[3]" << Colors::Reset << " Belirli Bir Dildeki Altyazi Dosyalarini Indir\n";
        std::cout << "  " << Colors::Dim << "[0] Bolum Secimine Don" << Colors::Reset << "\n\n";

        std::string batchType = Utils::ask(Colors::Bold + "Seciminiz (1-3, 0: Geri): " + Colors::Reset);
        if (batchType == "0" || batchType.empty()) return;

        std::vector<QueueItem> tasks;

        if (batchType == "1") {
            // TOPLU ORIJINAL VIDEO
            for (const auto& ep : selectedEpisodes) {
                json fullEp = client.getItem(ep.value("Id", ""));
                if (!fullEp.contains("MediaSources") || fullEp["MediaSources"].empty()) continue;

                json source = fullEp["MediaSources"][0];
                std::string container = Utils::toLower(source.value("Container", "mkv"));
                int sNum = season.value("IndexNumber", 1);
                int eNum = fullEp.value("IndexNumber", 1);
                std::string epName = fullEp.value("Name", "Bolum");

                std::ostringstream ss;
                ss << sName << " - S" << std::setw(2) << std::setfill('0') << sNum
                   << "E" << std::setw(2) << std::setfill('0') << eNum << " - " << epName << "." << container;
                std::string defaultName = ss.str();
                std::string safeFilename = Utils::getOriginalMediaFilename(source, defaultName);

                QueueItem t;
                t.type = "video";
                t.typeLabel = "Video";
                t.name = safeFilename;
                t.outputPath = downloadDir + "/" + safeFilename;
                t.itemId = fullEp.value("Id", "");
                t.sourceId = source.value("Id", "");
                t.container = container;
                t.expectedSize = source.value("Size", static_cast<uint64_t>(0));
                tasks.push_back(t);
            }

            std::string res = promptExecutionChoice(tasks, queue, client);
            if (res == "DONE") return;

        } else if (batchType == "2") {
            // TOPLU SES
            std::set<std::string> uniqueLangs;
            for (const auto& ep : selectedEpisodes) {
                json fullEp = client.getItem(ep.value("Id", ""));
                if (!fullEp.contains("MediaSources") || fullEp["MediaSources"].empty()) continue;
                json source = fullEp["MediaSources"][0];
                if (source.contains("MediaStreams")) {
                    for (const auto& ms : source["MediaStreams"]) {
                        if (ms.value("Type", "") == "Audio") {
                            uniqueLangs.insert(ms.value("Language", "und"));
                        }
                    }
                }
            }

            std::vector<std::string> langList(uniqueLangs.begin(), uniqueLangs.end());
            if (langList.empty()) {
                std::cout << "\n" << Colors::Red << "[UYARI] Bu bolumlerde ses akisi tespit edilemedi." << Colors::Reset << "\n";
                Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
                continue;
            }

            Utils::printHeader("SES DILI SECIMI");
            std::cout << "Mevcut Diller:\n";
            for (size_t i = 0; i < langList.size(); ++i) {
                std::cout << "  [" << (i + 1) << "] " << Colors::Bold << langList[i] << Colors::Reset << "\n";
            }
            std::cout << "  " << Colors::Dim << "[0] Geri Don" << Colors::Reset << "\n\n";

            std::string lChoice = Utils::ask("Indirilecek dil numarasi: ");
            if (lChoice == "0" || lChoice.empty()) continue;

            try {
                int lIdx = std::stoi(lChoice);
                if (lIdx < 1 || static_cast<size_t>(lIdx) > langList.size()) continue;
                std::string targetLang = langList[lIdx - 1];

                for (const auto& ep : selectedEpisodes) {
                    json fullEp = client.getItem(ep.value("Id", ""));
                    if (!fullEp.contains("MediaSources") || fullEp["MediaSources"].empty()) continue;
                    json source = fullEp["MediaSources"][0];
                    if (!source.contains("MediaStreams")) continue;

                    for (const auto& ms : source["MediaStreams"]) {
                        if (ms.value("Type", "") == "Audio" && ms.value("Language", "und") == targetLang) {
                            std::string codec = ms.value("Codec", "mka");
                            std::string ext = Utils::getAudioExtension(codec);
                            int sNum = season.value("IndexNumber", 1);
                            int eNum = fullEp.value("IndexNumber", 1);
                            std::string epName = fullEp.value("Name", "Bolum");

                            std::ostringstream ss;
                            ss << sName << " - S" << std::setw(2) << std::setfill('0') << sNum
                               << "E" << std::setw(2) << std::setfill('0') << eNum << " - "
                               << epName << "." << targetLang << "." << ext;
                            std::string baseFilename = ss.str();

                            QueueItem t;
                            t.type = "audio";
                            t.typeLabel = "Ses (" + targetLang + ")";
                            t.name = baseFilename;
                            t.outputPath = downloadDir + "/" + Utils::sanitizeFilename(baseFilename);
                            t.itemId = fullEp.value("Id", "");
                            t.sourceId = source.value("Id", "");
                            t.container = source.value("Container", "mkv");
                            t.expectedSize = 0;
                            t.audioStream = ms;
                            tasks.push_back(t);
                            break;
                        }
                    }
                }

                std::string res = promptExecutionChoice(tasks, queue, client);
                if (res == "DONE") return;

            } catch (...) {}

        } else if (batchType == "3") {
            // TOPLU ALTYAZI
            std::set<std::string> uniqueLangs;
            for (const auto& ep : selectedEpisodes) {
                json fullEp = client.getItem(ep.value("Id", ""));
                if (!fullEp.contains("MediaSources") || fullEp["MediaSources"].empty()) continue;
                json source = fullEp["MediaSources"][0];
                if (source.contains("MediaStreams")) {
                    for (const auto& ms : source["MediaStreams"]) {
                        if (ms.value("Type", "") == "Subtitle") {
                            uniqueLangs.insert(ms.value("Language", "und"));
                        }
                    }
                }
            }

            std::vector<std::string> langList(uniqueLangs.begin(), uniqueLangs.end());
            if (langList.empty()) {
                std::cout << "\n" << Colors::Red << "[UYARI] Bu bolumlerde altyazi tespit edilemedi." << Colors::Reset << "\n";
                Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
                continue;
            }

            Utils::printHeader("ALTYAZI DILI SECIMI");
            std::cout << "Mevcut Diller:\n";
            for (size_t i = 0; i < langList.size(); ++i) {
                std::cout << "  [" << (i + 1) << "] " << Colors::Bold << langList[i] << Colors::Reset << "\n";
            }
            std::cout << "  " << Colors::Dim << "[0] Geri Don" << Colors::Reset << "\n\n";

            std::string lChoice = Utils::ask("Indirilecek dil numarasi: ");
            if (lChoice == "0" || lChoice.empty()) continue;

            try {
                int lIdx = std::stoi(lChoice);
                if (lIdx < 1 || static_cast<size_t>(lIdx) > langList.size()) continue;
                std::string targetLang = langList[lIdx - 1];

                for (const auto& ep : selectedEpisodes) {
                    json fullEp = client.getItem(ep.value("Id", ""));
                    if (!fullEp.contains("MediaSources") || fullEp["MediaSources"].empty()) continue;
                    json source = fullEp["MediaSources"][0];
                    if (!source.contains("MediaStreams")) continue;

                    for (const auto& ms : source["MediaStreams"]) {
                        if (ms.value("Type", "") == "Subtitle" && ms.value("Language", "und") == targetLang) {
                            std::string codec = ms.value("Codec", "srt");
                            std::string ext = (codec == "subrip" || codec == "srt") ? "srt" : codec;
                            int sNum = season.value("IndexNumber", 1);
                            int eNum = fullEp.value("IndexNumber", 1);
                            std::string epName = fullEp.value("Name", "Bolum");

                            std::ostringstream ss;
                            ss << sName << " - S" << std::setw(2) << std::setfill('0') << sNum
                               << "E" << std::setw(2) << std::setfill('0') << eNum << " - "
                               << epName << "." << targetLang << "." << ext;
                            std::string baseFilename = ss.str();

                            QueueItem t;
                            t.type = "subtitle";
                            t.typeLabel = "Altyazi (" + targetLang + ")";
                            t.name = baseFilename;
                            t.outputPath = downloadDir + "/" + Utils::sanitizeFilename(baseFilename);
                            t.itemId = fullEp.value("Id", "");
                            t.sourceId = source.value("Id", "");
                            t.expectedSize = 0;
                            t.subtitleStream = ms;
                            tasks.push_back(t);
                            break;
                        }
                    }
                }

                std::string res = promptExecutionChoice(tasks, queue, client);
                if (res == "DONE") return;

            } catch (...) {}
        }
    }
}

void handleSeasonFlow(EmbyClient& client, const json& seriesItem, const json& season, QueueManager& queue) {
    std::string seriesId = seriesItem.value("Id", "");
    std::string seasonId = season.value("Id", "");
    std::string sName = seriesItem.value("Name", "Dizi");
    std::string seaName = season.value("Name", "Sezon");

    json episodesJson = client.getEpisodes(seriesId, seasonId);
    std::vector<json> episodes;
    if (episodesJson.is_array()) {
        episodes = episodesJson.get<std::vector<json>>();
    }

    if (episodes.empty()) {
        std::cout << "\n" << Colors::Red << "[UYARI] Bu sezonda herhangi bir bolum bulunamadi." << Colors::Reset << "\n";
        Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
        return;
    }

    while (true) {
        Utils::printHeader("BOLUM SECIMI - " + sName + " (" + seaName + ")");
        std::cout << "Mevcut Bolumler (" << episodes.size() << " Bolum):\n";
        for (size_t i = 0; i < episodes.size(); ++i) {
            const auto& ep = episodes[i];
            int eNum = ep.value("IndexNumber", static_cast<int>(i + 1));
            std::string epName = ep.value("Name", "Bolum");
            std::cout << "  [" << (i + 1) << "] Bolum " << eNum << ": " << Colors::Bold << epName << Colors::Reset << "\n";
        }

        std::cout << "\n  " << Colors::Green << "[T]" << Colors::Reset << " Tum Sezonu Indir (" << episodes.size() << " Bolum)\n";
        std::cout << "  " << Colors::Cyan << "[B]" << Colors::Reset << " Bolum Araligi / Coklu Secim (ornek: 1-5, 1,3,4)\n";
        std::cout << "  " << Colors::Dim << "[0] Sezon Secimine Don" << Colors::Reset << "\n\n";

        std::string choice = Utils::ask(Colors::Bold + "Seciminiz (1-" + std::to_string(episodes.size()) + ", T: Tumu, B: Coklu, 0: Geri): " + Colors::Reset);
        if (choice == "0" || choice.empty()) return;

        std::string upperChoice = Utils::toLower(choice);
        if (upperChoice == "t") {
            handleBatchEpisodesFlow(client, seriesItem, season, episodes, queue);
        } else if (upperChoice == "b") {
            std::string rangeStr = Utils::ask("Indirilecek bolumleri girin (ornek: 1-5 veya 1,3,5): ");
            auto indices = Utils::parseSelectionIndices(rangeStr, episodes.size());
            if (!indices.empty()) {
                std::vector<json> selected;
                for (size_t idx : indices) selected.push_back(episodes[idx]);
                handleBatchEpisodesFlow(client, seriesItem, season, selected, queue);
            }
        } else {
            try {
                int idx = std::stoi(choice);
                if (idx >= 1 && static_cast<size_t>(idx) <= episodes.size()) {
                    handleEpisodeFlow(client, seriesItem, season, episodes[idx - 1], queue);
                }
            } catch (...) {}
        }
    }
}

void handleSeriesFlow(EmbyClient& client, const json& seriesItem, QueueManager& queue) {
    std::string seriesId = seriesItem.value("Id", "");
    std::string seriesName = seriesItem.value("Name", "Dizi");

    json seasonsJson = client.getSeasons(seriesId);
    std::vector<json> seasons;
    if (seasonsJson.is_array()) {
        seasons = seasonsJson.get<std::vector<json>>();
    }

    if (seasons.empty()) {
        std::cout << "\n" << Colors::Red << "[UYARI] Bu dizi icin herhangi bir sezon bulunamadi." << Colors::Reset << "\n";
        Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
        return;
    }

    while (true) {
        Utils::printHeader("SEZON SECIMI - " + seriesName);
        std::cout << "Mevcut Sezonlar (" << seasons.size() << " Sezon):\n";
        for (size_t i = 0; i < seasons.size(); ++i) {
            const auto& s = seasons[i];
            std::string sName = s.value("Name", "Sezon");
            std::cout << "  [" << (i + 1) << "] " << Colors::Bold << sName << Colors::Reset << "\n";
        }
        std::cout << "  " << Colors::Dim << "[0] Arama Sonuclarina Don" << Colors::Reset << "\n\n";

        std::string choice = Utils::ask(Colors::Bold + "Sezon secin (1-" + std::to_string(seasons.size()) + ", 0: Geri): " + Colors::Reset);
        if (choice == "0" || choice.empty()) return;

        try {
            int idx = std::stoi(choice);
            if (idx >= 1 && static_cast<size_t>(idx) <= seasons.size()) {
                handleSeasonFlow(client, seriesItem, seasons[idx - 1], queue);
            }
        } catch (...) {}
    }
}

void searchMenu(EmbyClient& client, QueueManager& queue) {
    while (true) {
        Utils::printHeader("EMBY MEDYA INDIRICI (CLI)");
        std::cout << "Bagli Sunucu: " << Colors::Green << client.getUsername() << Colors::Reset << " @ " 
                  << Colors::Cyan << client.getServerUrl() << Colors::Reset << "\n";
        size_t pending = queue.getPendingCount();
        std::cout << "Kuyruk Durumu: " << (pending > 0 ? Colors::BrightYellow : Colors::Dim) 
                  << pending << " bekleyen gorev" << Colors::Reset << "\n\n";

        std::cout << "  " << Colors::Green << "[1]" << Colors::Reset << " Isim ile Ara (Film / Dizi / Dosya Adi)\n";
        std::cout << "  " << Colors::Green << "[2]" << Colors::Reset << " ID ile Ara (IMDb / TMDB)\n";
        std::cout << "  " << Colors::Yellow << "[3]" << Colors::Reset << " Indirme Kuyrugunu Yonet (" << pending << " Gorev)\n";
        std::cout << "  " << Colors::Cyan << "[4]" << Colors::Reset << " Sunucu Degistir / Yeniden Giris Yap\n";
        std::cout << "  " << Colors::Dim << "[0] Cikis" << Colors::Reset << "\n\n";

        std::string choice = Utils::ask(Colors::Bold + "Seciminiz (0-4): " + Colors::Reset);
        if (choice == "0") {
            std::cout << "\nIyi gunler dileriz.\n";
            break;
        }

        if (choice == "3") {
            handleQueueFlow(client, queue);
            continue;
        }

        if (choice == "4") {
            ConfigManager cfg;
            client = setupAuthentication(cfg);
            continue;
        }

        if (choice != "1" && choice != "2") continue;

        std::string query;
        if (choice == "1") {
            query = Utils::ask("\nAranacak Film, Dizi veya Dosya Adi: ");
        } else {
            query = Utils::ask("\nIMDb ID (tt...) veya TMDB ID: ");
        }

        if (query.empty()) continue;

        std::cout << "\n" << Colors::Dim << "Sunucuda araniyor..." << Colors::Reset << "\n";
        json resultsJson = (choice == "1") ? client.searchByName(query) : client.searchById(query);

        std::vector<json> results;
        if (resultsJson.is_array()) {
            results = resultsJson.get<std::vector<json>>();
        }

        if (results.empty()) {
            std::cout << "\n" << Colors::Red << "[SONUC YOK] Belirtilen kriterde herhangi bir icerik bulunamadi." << Colors::Reset << "\n";
            Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
            continue;
        }

        while (true) {
            Utils::printHeader("ARAMA SONUCLARI - \"" + query + "\" (" + std::to_string(results.size()) + " Sonuc)");
            for (size_t i = 0; i < results.size(); ++i) {
                const auto& item = results[i];
                std::string type = item.value("Type", "Bilinmeyen");
                std::string typeLabel = (type == "Series") ? "Dizi" : ((type == "Movie") ? "Film" : type);
                std::string name = item.value("Name", "Isimsiz");
                std::string year = item.contains("ProductionYear") ? " (" + std::to_string(item["ProductionYear"].get<int>()) + ")" : "";

                std::cout << "  [" << (i + 1) << "] [" << Colors::Cyan << typeLabel << Colors::Reset << "] "
                          << Colors::Bold << name << Colors::Reset << Colors::Dim << year << Colors::Reset << "\n";
            }
            std::cout << "  " << Colors::Dim << "[0] Ana Menuye Don" << Colors::Reset << "\n\n";

            std::string itemChoice = Utils::ask(Colors::Bold + "Icerik secin (1-" + std::to_string(results.size()) + ", 0: Geri): " + Colors::Reset);
            if (itemChoice == "0" || itemChoice.empty()) break;

            try {
                int sIdx = std::stoi(itemChoice);
                if (sIdx >= 1 && static_cast<size_t>(sIdx) <= results.size()) {
                    const auto& selItem = results[sIdx - 1];
                    std::string t = selItem.value("Type", "");
                    if (t == "Series") {
                        handleSeriesFlow(client, selItem, queue);
                    } else if (t == "Movie") {
                        handleMovieFlow(client, selItem, queue);
                    }
                }
            } catch (...) {}
        }
    }
}

EmbyClient setupAuthentication(ConfigManager& config) {
    while (true) {
        auto& profiles = config.getProfiles();

        if (!profiles.empty()) {
            Utils::printHeader("KAYITLI PROFILLER");
            std::cout << "Giris yapilacak sunucu profilini secin:\n\n";
            for (size_t i = 0; i < profiles.size(); ++i) {
                const auto& p = profiles[i];
                std::cout << "  [" << (i + 1) << "] " << Colors::Bold << p.username << Colors::Reset
                          << " @ " << Colors::Cyan << p.serverUrl << Colors::Reset << "\n";
            }
            std::cout << "\n  [" << (profiles.size() + 1) << "] " << Colors::Yellow << "Yeni Sunucu Ekle" << Colors::Reset << "\n";
            std::cout << "  " << Colors::Dim << "[0] Cikis" << Colors::Reset << "\n\n";

            std::string pChoice = Utils::ask(Colors::Bold + "Seciminiz (1-" + std::to_string(profiles.size() + 1) + ", 0): " + Colors::Reset);
            if (pChoice == "0") {
                std::exit(0);
            }

            try {
                int idx = std::stoi(pChoice);
                if (idx >= 1 && static_cast<size_t>(idx) <= profiles.size()) {
                    auto& selected = profiles[idx - 1];
                    EmbyClient client(selected.serverUrl, selected.port, selected.username, selected.prefix);
                    client.setCredentials(selected.userId, selected.token);

                    std::cout << "\n" << Colors::Dim << "Oturum dogrulaniyor..." << Colors::Reset << "\n";
                    if (client.testConnection()) {
                        std::cout << Colors::Green << "[BASARILI] Baglanti kuruldu: " << selected.username << Colors::Reset << "\n";
                        config.setLastActiveProfileId(selected.id);
                        std::this_thread::sleep_for(std::chrono::milliseconds(800));
                        return client;
                    }

                    std::cout << "\n" << Colors::Yellow << "[UYARI] Kayitli oturum anahtari gecerliligini yitirmis. Lutfen parolayi tekrar girin." << Colors::Reset << "\n";
                    std::string pass = Utils::askPassword("Parola: ");
                    std::string err;
                    if (client.authenticate(pass, err)) {
                        selected.userId = client.getUserId();
                        selected.token = client.getToken();
                        config.save();
                        std::cout << Colors::Green << "[BASARILI] Oturum yenilendi." << Colors::Reset << "\n";
                        std::this_thread::sleep_for(std::chrono::milliseconds(800));
                        return client;
                    } else {
                        std::cout << Colors::Red << "[HATA] " << err << Colors::Reset << "\n";
                        Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
                        continue;
                    }
                }
            } catch (...) {}
        }

        // Yeni Sunucu Ekle
        Utils::printHeader("YENI EMBY SUNUCUSU BAGLANTISI");
        std::string serverUrl = Utils::ask("Sunucu Adresi (ornek: https://emby.ornek.com): ");
        if (serverUrl.empty()) continue;

        std::string port = Utils::ask("Port (Varsayilan 443 veya 8096, bos birakilabilir): ");
        if (port.empty()) {
            port = (serverUrl.rfind("https://", 0) == 0) ? "443" : "8096";
        }

        std::string username = Utils::ask("Kullanici Adi: ");
        if (username.empty()) continue;

        std::string password = Utils::askPassword("Parola: ");

        EmbyClient client(serverUrl, port, username);
        std::string err;
        std::cout << "\n" << Colors::Dim << "Sunucuya baglaniliyor..." << Colors::Reset << "\n";
        if (client.authenticate(password, err)) {
            ServerProfile prof;
            prof.id = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
            prof.name = username + " @ " + client.getServerUrl();
            prof.serverUrl = client.getServerUrl();
            prof.port = port;
            prof.prefix = "/emby";
            prof.username = username;
            prof.userId = client.getUserId();
            prof.token = client.getToken();

            config.addOrUpdateProfile(prof);
            config.setLastActiveProfileId(prof.id);

            std::cout << Colors::Green << "[BASARILI] Giris yapildi ve profil kaydedildi." << Colors::Reset << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            return client;
        } else {
            std::cout << Colors::Red << "[HATA] " << err << Colors::Reset << "\n";
            Utils::ask(Colors::Dim + "Devam etmek icin Enter'a basin..." + Colors::Reset);
        }
    }
}

int main() {
    Utils::clearScreen();

    // FFmpeg kontrolü (dogrudan video indirmede ffmpeg gerekmez, yalnizca ses stream-copy icin uyarilir)
    if (!Utils::checkFfmpegInstalled()) {
        std::cout << Colors::Yellow << "[UYARI] Sisteminizde 'ffmpeg' bulunamadi." << Colors::Reset << "\n";
        std::cout << Colors::Dim << "Video indirmeleri donusturmesiz ve dogrudan yapilacagindan ffmpeg gerektirmez; ancak sadece ses parcasi cikarma icin gereklidir." << Colors::Reset << "\n\n";
    }

    try {
        ConfigManager config;
        QueueManager queue;
        EmbyClient client = setupAuthentication(config);
        searchMenu(client, queue);
    } catch (const std::exception& e) {
        std::cerr << "\n" << Colors::Red << "[BEKLENMEYEN HATA] " << e.what() << Colors::Reset << "\n";
        return 1;
    }

    return 0;
}
