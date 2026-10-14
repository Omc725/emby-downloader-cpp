CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude
LDFLAGS = -lcurl -pthread

SRCS = src/main.cpp \
       src/utils.cpp \
       src/http_client.cpp \
       src/config_manager.cpp \
       src/emby_client.cpp \
       src/media_downloader.cpp \
       src/audio_extractor.cpp \
       src/queue_manager.cpp

OBJS = $(SRCS:.cpp=.o)
TARGET = emby-downloader

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
