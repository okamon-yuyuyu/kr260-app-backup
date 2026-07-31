CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
OPENCV_FLAGS := $(shell pkg-config --cflags --libs opencv4)

TARGET := main
SRCS := main.cpp command.cpp camera.cpp encoder.cpp logger.cpp aruco_detector.cpp
OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(OPENCV_FLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(OPENCV_FLAGS)

run: $(TARGET)
	sudo ./$(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)
