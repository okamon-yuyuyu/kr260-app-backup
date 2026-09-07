CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread
OPENCV_FLAGS := $(shell pkg-config --cflags --libs opencv4)

TARGET := main
TEST_TARGET := test_posture_mapping
SRCS := main.cpp command.cpp camera.cpp encoder.cpp logger.cpp safety_logger.cpp emergency_monitor.cpp aruco_detector.cpp wire_adjuster.cpp dead_band_setting.cpp
OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(OPENCV_FLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(OPENCV_FLAGS)

run: $(TARGET)
	sudo ./$(TARGET)

$(TEST_TARGET): test_posture_mapping.cpp command.cpp command.hpp
	$(CXX) $(CXXFLAGS) -o $@ test_posture_mapping.cpp command.cpp

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(OBJS)
