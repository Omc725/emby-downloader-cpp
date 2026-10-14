#pragma once

#include "http_client.hpp"

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

class EmbyClient {
public:
    EmbyClient(
        const std::string& serverUrl = "",
        const std::string& port = "443",
        const std::string& username = "",
        const std::string& prefix = "/emby"
    );

    void setCredentials(const std::string& userId, const std::string& token);
    std::string getServerUrl() const { return m_serverUrl; }
    std::string getPort() const { return m_port; }
    std::string getPrefix() const { return m_prefix; }
    std::string getUsername() const { return m_username; }
    std::string getUserId() const { return m_userId; }
    std::string getToken() const { return m_token; }

    std::map<std::string, std::string> getHeaders() const;

    bool authenticate(const std::string& password, std::string& errorMsg);
    bool testConnection();

    nlohmann::json searchByName(const std::string& query);
    nlohmann::json searchById(const std::string& idInput);

    nlohmann::json getItem(const std::string& itemId);
    nlohmann::json getSeasons(const std::string& seriesId);
    nlohmann::json getEpisodes(const std::string& seriesId, const std::string& seasonId = "");

    std::string buildDirectVideoUrl(const std::string& itemId, const nlohmann::json& source) const;
    std::string buildDirectAudioStreamUrl(const std::string& itemId, const nlohmann::json& source, const nlohmann::json& audioStream) const;
    std::string getSubtitleUrl(const std::string& itemId, const std::string& sourceId, int subIndex, const std::string& ext) const;

    HttpClient& getHttpClient() { return m_http; }

private:
    std::string m_serverUrl;
    std::string m_port;
    std::string m_prefix;
    std::string m_username;
    std::string m_userId;
    std::string m_token;
    HttpClient m_http;

    static std::string normalizeUrl(const std::string& url, const std::string& port);
    nlohmann::json fetchTmdbMetadata(const std::string& cleanId, bool isImdb);
};
