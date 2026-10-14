#include "emby_client.hpp"
#include "utils.hpp"

#include <iostream>
#include <set>
#include <algorithm>

using json = nlohmann::json;

static const std::string TMDB_API_KEY = "c153e7a274fbc502f641a2e88a38c20e";

EmbyClient::EmbyClient(
    const std::string& serverUrl,
    const std::string& port,
    const std::string& username,
    const std::string& prefix
) : m_serverUrl(normalizeUrl(serverUrl, port)),
    m_port(port),
    m_prefix(prefix.empty() ? "/emby" : (prefix[0] == '/' ? prefix : "/" + prefix)),
    m_username(username)
{
}

void EmbyClient::setCredentials(const std::string& userId, const std::string& token) {
    m_userId = userId;
    m_token = token;
}

std::string EmbyClient::normalizeUrl(const std::string& url, const std::string& port) {
    if (url.empty()) return "";
    std::string u = Utils::trim(url);
    while (!u.empty() && u.back() == '/') {
        u.pop_back();
    }
    if (u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0) {
        u = "http://" + u;
    }
    if (!port.empty() && port != "80" && port != "443") {
        size_t colon = u.find(':', u.find("//") + 2);
        if (colon == std::string::npos) {
            u += ":" + port;
        }
    }
    return u;
}

std::map<std::string, std::string> EmbyClient::getHeaders() const {
    std::map<std::string, std::string> headers;
    headers["X-Emby-Authorization"] = "MediaBrowser Client=\"EmbyDownloaderCLI\", Device=\"LinuxTerminal\", DeviceId=\"EmbyDownloaderCLI-01\", Version=\"1.0.0\"";
    if (!m_token.empty()) {
        headers["X-Emby-Token"] = m_token;
    }
    return headers;
}

bool EmbyClient::authenticate(const std::string& password, std::string& errorMsg) {
    std::string authUrl = m_serverUrl + m_prefix + "/Users/AuthenticateByName";
    json body;
    body["Username"] = m_username;
    body["Pw"] = password;

    HttpResponse res = m_http.postJson(authUrl, body.dump(), getHeaders());
    if (!res.isOk()) {
        errorMsg = "Kimlik dogrulama basarisiz (HTTP " + std::to_string(res.statusCode) + "): " + res.error;
        return false;
    }

    try {
        json j = json::parse(res.body);
        if (j.contains("AccessToken") && j["AccessToken"].is_string()) {
            m_token = j["AccessToken"].get<std::string>();
        }
        if (j.contains("User") && j["User"].contains("Id") && j["User"]["Id"].is_string()) {
            m_userId = j["User"]["Id"].get<std::string>();
        }

        if (m_token.empty() || m_userId.empty()) {
            errorMsg = "Sunucu gecersiz oturum yaniti dondurdu.";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        errorMsg = std::string("Yanit ayristirma hatasi: ") + e.what();
        return false;
    }
}

bool EmbyClient::testConnection() {
    if (m_token.empty()) return false;
    std::string url = m_serverUrl + m_prefix + "/System/Info?api_key=" + Utils::urlEncode(m_token);
    HttpResponse res = m_http.get(url, getHeaders());
    return res.isOk();
}

json EmbyClient::searchByName(const std::string& query) {
    json results = json::array();
    std::set<std::string> seenIds;

    // 1. Dogrudan arama
    std::string url1 = m_serverUrl + m_prefix + "/Users/" + m_userId + 
                       "/Items?SearchTerm=" + Utils::urlEncode(query) + 
                       "&IncludeItemTypes=Movie,Series&Recursive=true&api_key=" + Utils::urlEncode(m_token);
    HttpResponse res1 = m_http.get(url1, getHeaders());
    if (res1.isOk()) {
        try {
            json data = json::parse(res1.body);
            if (data.contains("Items") && data["Items"].is_array()) {
                for (const auto& item : data["Items"]) {
                    std::string id = item.value("Id", "");
                    if (!id.empty() && seenIds.find(id) == seenIds.end()) {
                        seenIds.insert(id);
                        results.push_back(item);
                    }
                }
            }
        } catch (...) {}
    }

    // 2. Dosya adi temizleme ile arama
    std::string cleanedTitle = Utils::cleanFilenameToTitle(query);
    if (!cleanedTitle.empty() && Utils::toLower(cleanedTitle) != Utils::toLower(query)) {
        std::string url2 = m_serverUrl + m_prefix + "/Users/" + m_userId + 
                           "/Items?SearchTerm=" + Utils::urlEncode(cleanedTitle) + 
                           "&IncludeItemTypes=Movie,Series&Recursive=true&api_key=" + Utils::urlEncode(m_token);
        HttpResponse res2 = m_http.get(url2, getHeaders());
        if (res2.isOk()) {
            try {
                json data = json::parse(res2.body);
                if (data.contains("Items") && data["Items"].is_array()) {
                    for (const auto& item : data["Items"]) {
                        std::string id = item.value("Id", "");
                        if (!id.empty() && seenIds.find(id) == seenIds.end()) {
                            seenIds.insert(id);
                            results.push_back(item);
                        }
                    }
                }
            } catch (...) {}
        }
    }

    // 3. Eslenmeyen veya bolum adlari icin bolum uzerinden ust dizi bulma
    if (results.empty()) {
        std::string url3 = m_serverUrl + m_prefix + "/Users/" + m_userId + 
                           "/Items?SearchTerm=" + Utils::urlEncode(query) + 
                           "&Recursive=true&Limit=10&api_key=" + Utils::urlEncode(m_token);
        HttpResponse res3 = m_http.get(url3, getHeaders());
        if (res3.isOk()) {
            try {
                json data = json::parse(res3.body);
                if (data.contains("Items") && data["Items"].is_array()) {
                    for (const auto& item : data["Items"]) {
                        std::string type = item.value("Type", "");
                        std::string sId = item.value("SeriesId", "");
                        std::string id = item.value("Id", "");

                        if (type == "Episode" && !sId.empty() && seenIds.find(sId) == seenIds.end()) {
                            std::string sUrl = m_serverUrl + m_prefix + "/Users/" + m_userId + "/Items/" + sId + "?api_key=" + Utils::urlEncode(m_token);
                            HttpResponse sRes = m_http.get(sUrl, getHeaders());
                            if (sRes.isOk()) {
                                json sData = json::parse(sRes.body);
                                seenIds.insert(sId);
                                results.push_back(sData);
                            }
                        } else if ((type == "Movie" || type == "Series") && !id.empty() && seenIds.find(id) == seenIds.end()) {
                            seenIds.insert(id);
                            results.push_back(item);
                        }
                    }
                }
            } catch (...) {}
        }
    }

    return results;
}

json EmbyClient::searchById(const std::string& idInput) {
    std::string raw = Utils::trim(idInput);
    bool isImdb = false;
    std::string cleanId = raw;

    if (raw.rfind("tt", 0) == 0 || raw.rfind("TT", 0) == 0) {
        isImdb = true;
        cleanId = Utils::toLower(raw);
    } else if (Utils::toLower(raw).rfind("imdb:", 0) == 0) {
        isImdb = true;
        cleanId = Utils::toLower(Utils::trim(raw.substr(5)));
    } else if (Utils::toLower(raw).rfind("tmdb:", 0) == 0) {
        isImdb = false;
        cleanId = Utils::trim(raw.substr(5));
    } else {
        bool allDigits = !raw.empty() && std::all_of(raw.begin(), raw.end(), ::isdigit);
        if (allDigits) {
            isImdb = false;
            cleanId = raw;
        }
    }

    std::vector<std::string> providerQueries;
    if (isImdb) {
        providerQueries.push_back("imdb." + cleanId + ",Imdb." + cleanId);
        providerQueries.push_back("tmdb." + cleanId + ",Tmdb." + cleanId);
    } else {
        providerQueries.push_back("tmdb." + cleanId + ",Tmdb." + cleanId);
        providerQueries.push_back("TheMovieDb." + cleanId);
    }

    json results = json::array();
    std::set<std::string> seenIds;

    for (const auto& q : providerQueries) {
        std::string url = m_serverUrl + m_prefix + "/Users/" + m_userId + 
                          "/Items?AnyProviderIdEquals=" + Utils::urlEncode(q) + 
                          "&IncludeItemTypes=Movie,Series&Recursive=true&api_key=" + Utils::urlEncode(m_token);
        HttpResponse res = m_http.get(url, getHeaders());
        if (res.isOk()) {
            try {
                json data = json::parse(res.body);
                if (data.contains("Items") && data["Items"].is_array()) {
                    for (const auto& item : data["Items"]) {
                        std::string id = item.value("Id", "");
                        if (!id.empty() && seenIds.find(id) == seenIds.end()) {
                            seenIds.insert(id);
                            results.push_back(item);
                        }
                    }
                }
            } catch (...) {}
        }
    }

    // TMDb API Fallback
    if (results.empty()) {
        json meta = fetchTmdbMetadata(cleanId, isImdb);
        if (meta.contains("title") && meta["title"].is_string()) {
            std::string title = meta["title"].get<std::string>();
            return searchByName(title);
        }
    }

    return results;
}

json EmbyClient::fetchTmdbMetadata(const std::string& cleanId, bool isImdb) {
    json meta = json::object();
    try {
        if (isImdb) {
            std::string url = "https://api.themoviedb.org/3/find/" + cleanId + "?external_source=imdb_id&api_key=" + TMDB_API_KEY;
            HttpResponse res = m_http.get(url);
            if (res.isOk()) {
                json data = json::parse(res.body);
                json match = nullptr;
                if (data.contains("movie_results") && !data["movie_results"].empty()) {
                    match = data["movie_results"][0];
                } else if (data.contains("tv_results") && !data["tv_results"].empty()) {
                    match = data["tv_results"][0];
                }
                if (match != nullptr && match.is_object()) {
                    meta["title"] = match.value("title", match.value("name", ""));
                }
            }
        } else {
            std::string mUrl = "https://api.themoviedb.org/3/movie/" + cleanId + "?api_key=" + TMDB_API_KEY;
            HttpResponse mRes = m_http.get(mUrl);
            if (mRes.isOk()) {
                json data = json::parse(mRes.body);
                meta["title"] = data.value("title", data.value("name", ""));
            } else {
                std::string tUrl = "https://api.themoviedb.org/3/tv/" + cleanId + "?api_key=" + TMDB_API_KEY;
                HttpResponse tRes = m_http.get(tUrl);
                if (tRes.isOk()) {
                    json data = json::parse(tRes.body);
                    meta["title"] = data.value("name", "");
                }
            }
        }
    } catch (...) {}
    return meta;
}

json EmbyClient::getItem(const std::string& itemId) {
    std::string url = m_serverUrl + m_prefix + "/Users/" + m_userId + "/Items/" + itemId + "?Fields=MediaSources&api_key=" + Utils::urlEncode(m_token);
    HttpResponse res = m_http.get(url, getHeaders());
    if (res.isOk()) {
        try {
            return json::parse(res.body);
        } catch (...) {}
    }
    return json::object();
}

json EmbyClient::getSeasons(const std::string& seriesId) {
    std::string url = m_serverUrl + m_prefix + "/Shows/" + seriesId + "/Seasons?UserId=" + m_userId + "&api_key=" + Utils::urlEncode(m_token);
    HttpResponse res = m_http.get(url, getHeaders());
    if (res.isOk()) {
        try {
            json data = json::parse(res.body);
            return data.value("Items", json::array());
        } catch (...) {}
    }
    return json::array();
}

json EmbyClient::getEpisodes(const std::string& seriesId, const std::string& seasonId) {
    std::string sQuery = seasonId.empty() ? "" : ("SeasonId=" + Utils::urlEncode(seasonId) + "&");
    std::string url = m_serverUrl + m_prefix + "/Shows/" + seriesId + "/Episodes?" + sQuery + "UserId=" + m_userId + "&Fields=MediaSources&api_key=" + Utils::urlEncode(m_token);
    HttpResponse res = m_http.get(url, getHeaders());
    if (res.isOk()) {
        try {
            json data = json::parse(res.body);
            return data.value("Items", json::array());
        } catch (...) {}
    }
    return json::array();
}

std::string EmbyClient::buildDirectVideoUrl(const std::string& itemId, const json& source) const {
    std::string container = "mkv";
    if (source.is_object() && source.contains("Container") && source["Container"].is_string()) {
        container = Utils::toLower(source["Container"].get<std::string>());
    }
    std::string sourceId = source.is_object() ? source.value("Id", "") : "";
    return m_serverUrl + m_prefix + "/Videos/" + itemId + "/stream." + container + 
           "?static=true&MediaSourceId=" + Utils::urlEncode(sourceId) + 
           "&EnableDirectPlay=true&EnableDirectStream=true&AllowVideoStreamCopy=true&AllowAudioStreamCopy=true&api_key=" + Utils::urlEncode(m_token);
}

std::string EmbyClient::buildDirectAudioStreamUrl(const std::string& itemId, const json& source, const json& audioStream) const {
    std::string sourceId = source.is_object() ? source.value("Id", "") : "";
    int index = audioStream.is_object() ? audioStream.value("Index", 0) : 0;
    return m_serverUrl + m_prefix + "/Audio/" + itemId + "/stream.mka?AudioStreamIndex=" + std::to_string(index) + 
           "&MediaSourceId=" + Utils::urlEncode(sourceId) + "&api_key=" + Utils::urlEncode(m_token);
}

std::string EmbyClient::getSubtitleUrl(const std::string& itemId, const std::string& sourceId, int subIndex, const std::string& ext) const {
    return m_serverUrl + m_prefix + "/Videos/" + itemId + "/" + sourceId + "/Subtitles/" + std::to_string(subIndex) + 
           "/Stream." + ext + "?api_key=" + Utils::urlEncode(m_token);
}
