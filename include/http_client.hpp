#pragma once

#include <string>
#include <map>
#include <functional>
#include <cstdint>
#include <curl/curl.h>

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;

    bool isOk() const {
        return statusCode >= 200 && statusCode < 300;
    }
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers = {});
    HttpResponse postJson(const std::string& url, const std::string& jsonBody, const std::map<std::string, std::string>& headers = {});
    HttpResponse head(const std::string& url, const std::map<std::string, std::string>& headers = {});
    
    // Range destekli ve canlı callback'li indirme
    bool downloadToFile(
        const std::string& url,
        const std::string& outputPath,
        uint64_t resumeFromByte,
        uint64_t expectedTotalBytes,
        const std::map<std::string, std::string>& headers,
        std::function<bool(uint64_t currentBytes, uint64_t totalBytes, double speedBps)> progressCb
    );

private:
    static size_t writeStringCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata);
    static size_t writeFileCallback(void* contents, size_t size, size_t nmemb, void* userp);
    static int xferInfoCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
};
