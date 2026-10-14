#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

struct ServerProfile {
    std::string id;
    std::string name;
    std::string serverUrl;
    std::string port;
    std::string prefix = "/emby";
    std::string username;
    std::string userId;
    std::string token;
};

class ConfigManager {
public:
    ConfigManager();

    bool load();
    bool save();

    std::vector<ServerProfile>& getProfiles();
    const std::vector<ServerProfile>& getProfiles() const;
    std::optional<ServerProfile> getProfile(const std::string& id) const;
    void addOrUpdateProfile(const ServerProfile& profile);
    
    std::string getLastActiveProfileId() const;
    void setLastActiveProfileId(const std::string& id);

    static std::string getConfigDir();
    static std::string getConfigFilePath();

private:
    std::vector<ServerProfile> m_profiles;
    std::string m_lastActiveProfileId;
};
