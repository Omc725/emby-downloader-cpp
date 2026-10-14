#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace Utils {
    std::string formatBytes(uint64_t bytes);
    std::string sanitizeFilename(const std::string& name);
    std::string getOriginalMediaFilename(const nlohmann::json& source, const std::string& defaultName);
    std::string cleanFilenameToTitle(const std::string& filename);
    std::string getAudioExtension(const std::string& codec);
    std::vector<size_t> parseSelectionIndices(const std::string& input, size_t maxCount);
    
    void clearScreen();
    void printHeader(const std::string& title);
    std::string ask(const std::string& prompt);
    std::string askPassword(const std::string& prompt);
    
    std::string trim(const std::string& s);
    std::string toLower(const std::string& s);
    std::string urlEncode(const std::string& value);
    bool checkFfmpegInstalled();
}
