#include "utils.hpp"
#include "ansi_colors.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <regex>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <termios.h>
#include <unistd.h>

namespace Utils {

std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) {
        start++;
    }
    auto end = s.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(static_cast<unsigned char>(*end)));
    return std::string(start, end + 1);
}

std::string toLower(const std::string& s) {
    std::string res = s;
    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    return res;
}

std::string urlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return escaped.str();
}

std::string formatBytes(uint64_t bytes) {
    if (bytes == 0) return "0 B";
    static const char* sizes[] = {"B", "KB", "MB", "GB", "TB"};
    double b = static_cast<double>(bytes);
    int i = 0;
    while (b >= 1024.0 && i < 4) {
        b /= 1024.0;
        i++;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << b << " " << sizes[i];
    return ss.str();
}

std::string sanitizeFilename(const std::string& name) {
    std::string sanitized = name;
    for (char& c : sanitized) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            c = '_';
        }
    }
    // Birden fazla boşluğu teke indir
    std::regex spaces("\\s+");
    sanitized = std::regex_replace(sanitized, spaces, " ");
    return trim(sanitized);
}

std::string getOriginalMediaFilename(const nlohmann::json& source, const std::string& defaultName) {
    if (source.is_object() && source.contains("Path") && source["Path"].is_string()) {
        std::string rawPath = source["Path"].get<std::string>();
        size_t lastSlash = rawPath.find_last_of("/\\");
        std::string last = (lastSlash != std::string::npos) ? rawPath.substr(lastSlash + 1) : rawPath;
        last = trim(last);
        if (!last.empty()) {
            return sanitizeFilename(last);
        }
    }
    return sanitizeFilename(defaultName);
}

std::string cleanFilenameToTitle(const std::string& filename) {
    std::string s = filename;
    size_t lastSlash = s.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        s = s.substr(lastSlash + 1);
    }

    // Uzantıyı kaldır (.mkv, .mp4 vb.)
    s = std::regex_replace(s, std::regex("\\.[a-zA-Z0-9]{2,4}$"), "");
    // Köşeli ve süslü parantezleri kaldır
    s = std::regex_replace(s, std::regex("\\[[^\\]]*\\]"), " ");
    s = std::regex_replace(s, std::regex("\\{[^}]*\\}"), " ");

    static const std::vector<std::string> patterns = {
        "\\b(2160p|1080p|1080i|720p|576p|480p|360p|4k|uhd|remux|bluray|blu-ray|web-dl|webrip|hdtv|dvdrip|brrip)\\b",
        "\\b(x264|x265|h264|h265|hevc|avc|hdr|hdr10|dv|dolby\\s*vision|truehd|atmos|dts|dts-hd|ac3|eac3|aac|mp3|flac)\\b",
        "\\b(s\\d{1,2}e\\d{1,2}|\\d{1,2}x\\d{1,2})\\b",
        "\\b(netflix|amazon|disney|apple|hbo)\\b",
        "#\\d+"
    };

    for (const auto& pat : patterns) {
        s = std::regex_replace(s, std::regex(pat, std::regex_constants::icase), " ");
    }

    s = std::regex_replace(s, std::regex("[._\\-+]"), " ");
    s = std::regex_replace(s, std::regex("\\s+"), " ");
    return trim(s);
}

std::string getAudioExtension(const std::string& codec) {
    if (codec.empty()) return "mka";
    std::string c = toLower(codec);
    if (c.find("eac3") != std::string::npos || c.find("ddp") != std::string::npos || c.find("ec-3") != std::string::npos) return "eac3";
    if (c.find("ac3") != std::string::npos || c.find("dolby digital") != std::string::npos) return "ac3";
    if (c.find("dts") != std::string::npos) return "dts";
    if (c.find("aac") != std::string::npos) return "m4a";
    if (c.find("mp3") != std::string::npos) return "mp3";
    if (c.find("flac") != std::string::npos) return "flac";
    if (c.find("opus") != std::string::npos) return "opus";
    if (c.find("vorbis") != std::string::npos || c.find("ogg") != std::string::npos) return "ogg";
    if (c.rfind("pcm", 0) == 0) return "wav";
    if (c == "truehd" || c == "thd") return "mka";
    return "mka";
}

std::vector<size_t> parseSelectionIndices(const std::string& input, size_t maxCount) {
    std::string str = trim(input);
    std::string upperStr = str;
    std::transform(upperStr.begin(), upperStr.end(), upperStr.begin(), [](unsigned char c) {
        return std::toupper(c);
    });

    if (upperStr == "T" || upperStr == "TUM" || upperStr == "TÜM" || upperStr == "HEPSI" || upperStr == "H") {
        std::vector<size_t> all(maxCount);
        for (size_t i = 0; i < maxCount; ++i) all[i] = i;
        return all;
    }

    std::vector<size_t> result;
    std::stringstream ss(str);
    std::string part;

    while (std::getline(ss, part, ',')) {
        part = trim(part);
        if (part.empty()) continue;

        size_t dash = part.find('-');
        if (dash != std::string::npos) {
            std::string s1 = trim(part.substr(0, dash));
            std::string s2 = trim(part.substr(dash + 1));
            try {
                int start = std::stoi(s1);
                int end = std::stoi(s2);
                if (start > end) std::swap(start, end);
                for (int v = start; v <= end; ++v) {
                    if (v >= 1 && static_cast<size_t>(v) <= maxCount) {
                        size_t idx = static_cast<size_t>(v - 1);
                        if (std::find(result.begin(), result.end(), idx) == result.end()) {
                            result.push_back(idx);
                        }
                    }
                }
            } catch (...) {}
        } else {
            try {
                int val = std::stoi(part);
                if (val >= 1 && static_cast<size_t>(val) <= maxCount) {
                    size_t idx = static_cast<size_t>(val - 1);
                    if (std::find(result.begin(), result.end(), idx) == result.end()) {
                        result.push_back(idx);
                    }
                }
            } catch (...) {}
        }
    }

    std::sort(result.begin(), result.end());
    return result;
}

void clearScreen() {
    std::cout << "\033[2J\033[H" << std::flush;
}

void printHeader(const std::string& title) {
    clearScreen();
    std::cout << Colors::Bold << Colors::Cyan;
    std::cout << "================================================================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "================================================================================\n";
    std::cout << Colors::Reset << "\n";
}

std::string ask(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        if (std::cin.eof()) {
            std::exit(0);
        }
        return "0";
    }
    return trim(line);
}

std::string askPassword(const std::string& prompt) {
    if (!isatty(STDIN_FILENO)) {
        return ask(prompt);
    }
    std::cout << prompt << std::flush;
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    std::string password;
    if (!std::getline(std::cin, password)) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        if (std::cin.eof()) {
            std::exit(0);
        }
        return "";
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << "\n";
    return trim(password);
}

bool checkFfmpegInstalled() {
    return (std::system("ffmpeg -version > /dev/null 2>&1") == 0);
}

}
