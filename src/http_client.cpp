#include "http_client.hpp"
#include "utils.hpp"

#include <iostream>
#include <cstdio>
#include <chrono>

static bool s_curl_inited = false;

struct DownloadContext {
    FILE* file = nullptr;
    uint64_t resumeOffset = 0;
    uint64_t expectedTotal = 0;
    std::chrono::steady_clock::time_point startTime;
    std::function<bool(uint64_t, uint64_t, double)> progressCallback;
};

HttpClient::HttpClient() {
    if (!s_curl_inited) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        s_curl_inited = true;
    }
}

HttpClient::~HttpClient() {
    // curl_global_cleanup process sonlandığında çağrılabilir
}

size_t HttpClient::writeStringCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    s->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

size_t HttpClient::headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    std::string line(buffer, total);
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = Utils::toLower(Utils::trim(line.substr(0, colon)));
        std::string val = Utils::trim(line.substr(colon + 1));
        (*headers)[key] = val;
    }
    return total;
}

size_t HttpClient::writeFileCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    FILE* f = static_cast<FILE*>(userp);
    return std::fwrite(contents, size, nmemb, f);
}

int HttpClient::xferInfoCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    DownloadContext* ctx = static_cast<DownloadContext*>(clientp);
    if (!ctx || !ctx->progressCallback) return 0;

    uint64_t currentBytes = ctx->resumeOffset + static_cast<uint64_t>(dlnow);
    uint64_t totalBytes = (dltotal > 0) ? (ctx->resumeOffset + static_cast<uint64_t>(dltotal)) : ctx->expectedTotal;

    auto now = std::chrono::steady_clock::now();
    double elapsedSec = std::chrono::duration<double>(now - ctx->startTime).count();
    double speedBps = (elapsedSec > 0.1) ? (static_cast<double>(dlnow) / elapsedSec) : 0.0;

    bool continueDownload = ctx->progressCallback(currentBytes, totalBytes, speedBps);
    return continueDownload ? 0 : 1; // 1 dönerse curl abort eder
}

HttpResponse HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error = "CURL başlatılamadı";
        return resp;
    }

    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        chunk = curl_slist_append(chunk, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L); // Güvenli olmayan yerel sertifikalar için tolerans
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        resp.error = curl_easy_strerror(res);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    }

    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse HttpClient::postJson(const std::string& url, const std::string& jsonBody, const std::map<std::string, std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error = "CURL başlatılamadı";
        return resp;
    }

    struct curl_slist* chunk = nullptr;
    chunk = curl_slist_append(chunk, "Content-Type: application/json");
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        chunk = curl_slist_append(chunk, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeStringCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        resp.error = curl_easy_strerror(res);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    }

    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse HttpClient::head(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.error = "CURL başlatılamadı";
        return resp;
    }

    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        chunk = curl_slist_append(chunk, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        resp.error = curl_easy_strerror(res);
    } else {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        resp.statusCode = static_cast<int>(code);
    }

    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);
    return resp;
}

bool HttpClient::downloadToFile(
    const std::string& url,
    const std::string& outputPath,
    uint64_t resumeFromByte,
    uint64_t expectedTotalBytes,
    const std::map<std::string, std::string>& headers,
    std::function<bool(uint64_t currentBytes, uint64_t totalBytes, double speedBps)> progressCb
) {
    FILE* fp = std::fopen(outputPath.c_str(), (resumeFromByte > 0) ? "ab" : "wb");
    if (!fp) {
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(fp);
        return false;
    }

    struct curl_slist* chunk = nullptr;
    for (const auto& [k, v] : headers) {
        std::string h = k + ": " + v;
        chunk = curl_slist_append(chunk, h.c_str());
    }

    DownloadContext ctx;
    ctx.file = fp;
    ctx.resumeOffset = resumeFromByte;
    ctx.expectedTotal = expectedTotalBytes;
    ctx.startTime = std::chrono::steady_clock::now();
    ctx.progressCallback = progressCb;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    if (resumeFromByte > 0) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resumeFromByte));
    }

    if (progressCb) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
    } else {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    }

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    std::fclose(fp);
    curl_slist_free_all(chunk);
    curl_easy_cleanup(curl);

    bool ok = (res == CURLE_OK) && (httpCode >= 200 && httpCode < 300);
    return ok;
}
