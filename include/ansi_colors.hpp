#pragma once

#include <string>

namespace Colors {
    inline const std::string Reset        = "\033[0m";
    inline const std::string Bold         = "\033[1m";
    inline const std::string Dim          = "\033[2m";
    inline const std::string Red          = "\033[31m";
    inline const std::string Green        = "\033[32m";
    inline const std::string Yellow       = "\033[33m";
    inline const std::string Blue         = "\033[34m";
    inline const std::string Magenta      = "\033[35m";
    inline const std::string Cyan         = "\033[36m";
    inline const std::string White        = "\033[37m";
    inline const std::string BrightGreen  = "\033[92m";
    inline const std::string BrightYellow = "\033[93m";
    inline const std::string BrightCyan   = "\033[96m";
    inline const std::string BrightWhite  = "\033[97m";
}
