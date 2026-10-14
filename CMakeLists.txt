cmake_minimum_required(VERSION 3.15)
project(emby-downloader-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

include_directories(include)

set(SOURCES
    src/main.cpp
    src/utils.cpp
    src/http_client.cpp
    src/config_manager.cpp
    src/emby_client.cpp
    src/media_downloader.cpp
    src/audio_extractor.cpp
    src/queue_manager.cpp
)

add_executable(emby-downloader ${SOURCES})
target_link_libraries(emby-downloader PRIVATE CURL::libcurl Threads::Threads)
