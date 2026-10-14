#pragma once

#include "emby_client.hpp"

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

struct QueueItem {
    std::string id;
    std::string type;       // "video", "audio", "subtitle"
    std::string typeLabel;  // "Video", "Ses (Turkce)", "Altyazi (Turkce)"
    std::string name;
    std::string outputPath;
    std::string itemId;
    std::string sourceId;
    std::string container = "mkv";
    uint64_t expectedSize = 0;
    double totalDurationSeconds = 0.0;
    nlohmann::json audioStream = nlohmann::json::object();
    nlohmann::json subtitleStream = nlohmann::json::object();
    std::string status = "pending"; // "pending", "downloading", "completed", "error"
    std::string error;
};

class QueueManager {
public:
    QueueManager();

    bool load();
    bool save();

    void add(QueueItem item);
    void addAll(const std::vector<QueueItem>& items);
    bool remove(size_t index);
    void clear();

    size_t getPendingCount() const;
    std::vector<QueueItem>& getItems();
    const std::vector<QueueItem>& getItems() const;

    void processQueue(EmbyClient& client);

    static std::string getQueueFilePath();

private:
    std::vector<QueueItem> m_items;
};
