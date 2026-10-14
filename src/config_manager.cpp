#include "config_manager.hpp"

#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;

ConfigManager::ConfigManager() {
    load();
}

std::string ConfigManager::getConfigDir() {
    const char* home = std::getenv("HOME");
    if (!home) return "./.config/emby-downloader";
    return std::string(home) + "/.config/emby-downloader";
}

std::string ConfigManager::getConfigFilePath() {
    return getConfigDir() + "/config.json";
}

bool ConfigManager::load() {
    m_profiles.clear();
    m_lastActiveProfileId.clear();

    std::string path = getConfigFilePath();
    if (!fs::exists(path)) {
        // Geriye dönük uyumluluk: eski dizini kontrol et
        const char* home = std::getenv("HOME");
        if (home) {
            std::string legacyPath = std::string(home) + "/.config/emby-audio-downloader/config.json";
            if (fs::exists(legacyPath)) {
                path = legacyPath;
            }
        }
    }

    if (!fs::exists(path)) {
        return false;
    }

    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        json j;
        file >> j;

        if (j.contains("lastActiveProfile") && j["lastActiveProfile"].is_string()) {
            m_lastActiveProfileId = j["lastActiveProfile"].get<std::string>();
        }

        if (j.contains("profiles") && j["profiles"].is_array()) {
            for (const auto& item : j["profiles"]) {
                ServerProfile p;
                if (item.contains("id")) p.id = item["id"].get<std::string>();
                if (item.contains("name")) p.name = item["name"].get<std::string>();
                if (item.contains("serverUrl")) p.serverUrl = item["serverUrl"].get<std::string>();
                if (item.contains("port")) p.port = item["port"].get<std::string>();
                if (item.contains("prefix")) p.prefix = item["prefix"].get<std::string>();
                if (item.contains("username")) p.username = item["username"].get<std::string>();
                if (item.contains("userId")) p.userId = item["userId"].get<std::string>();
                if (item.contains("token")) p.token = item["token"].get<std::string>();
                m_profiles.push_back(p);
            }
        } else if (j.contains("savedServers") && j["savedServers"].is_array()) {
            for (const auto& item : j["savedServers"]) {
                ServerProfile p;
                p.id = item.value("id", std::to_string(m_profiles.size() + 1));
                p.name = item.value("serverName", item.value("name", ""));
                p.serverUrl = item.value("serverUrl", "");
                p.port = item.value("port", "443");
                p.prefix = item.value("prefix", "/emby");
                p.username = item.value("username", "");
                p.userId = item.value("userId", "");
                p.token = item.value("token", "");
                if (p.name.empty()) {
                    p.name = p.username + " @ " + p.serverUrl;
                }
                m_profiles.push_back(p);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ConfigManager::save() {
    try {
        fs::create_directories(getConfigDir());
        std::ofstream file(getConfigFilePath());
        if (!file.is_open()) return false;

        json j;
        j["lastActiveProfile"] = m_lastActiveProfileId;
        j["profiles"] = json::array();

        for (const auto& p : m_profiles) {
            json item;
            item["id"] = p.id;
            item["name"] = p.name;
            item["serverUrl"] = p.serverUrl;
            item["port"] = p.port;
            item["prefix"] = p.prefix;
            item["username"] = p.username;
            item["userId"] = p.userId;
            item["token"] = p.token;
            j["profiles"].push_back(item);
        }

        file << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<ServerProfile>& ConfigManager::getProfiles() {
    return m_profiles;
}

const std::vector<ServerProfile>& ConfigManager::getProfiles() const {
    return m_profiles;
}

std::optional<ServerProfile> ConfigManager::getProfile(const std::string& id) const {
    for (const auto& p : m_profiles) {
        if (p.id == id) return p;
    }
    return std::nullopt;
}

void ConfigManager::addOrUpdateProfile(const ServerProfile& profile) {
    for (auto& p : m_profiles) {
        if (p.id == profile.id || (p.serverUrl == profile.serverUrl && p.username == profile.username)) {
            p = profile;
            save();
            return;
        }
    }
    m_profiles.push_back(profile);
    save();
}

std::string ConfigManager::getLastActiveProfileId() const {
    return m_lastActiveProfileId;
}

void ConfigManager::setLastActiveProfileId(const std::string& id) {
    m_lastActiveProfileId = id;
    save();
}
