CXX := g++
TARGET := aruco_auto_go

CXXFLAGS := -std=c++17 -Wall -Wextra -O2 $(shell pkg-config --cflags opencv4)
LDFLAGS  := $(shell pkg-config --libs opencv4)

SRCS := main.cpp aruco_detector.cpp command.cpp encoder.cpp logger.cpp camera.cpp
OBJS := $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
