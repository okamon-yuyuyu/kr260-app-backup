CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
OPENCV_FLAGS := $(shell pkg-config --cflags --libs opencv4)
TARGET := peri_collect
SRCS := main.cpp axi_launch.cpp command.cpp feedback_tcp.cpp camera.cpp aruco_detector.cpp logger.cpp wire_adjuster.cpp async_jpeg.cpp encoder.cpp safety_logger.cpp
OBJS := $(SRCS:.cpp=.o)
.PHONY: all clean test
all: $(TARGET)
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(OPENCV_FLAGS)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(OPENCV_FLAGS)
test: test_protocol test_async_jpeg test_logger test_safety_logger test_axi_launch
	./test_protocol
	./test_async_jpeg
	./test_logger
	./test_safety_logger
	./test_axi_launch
test_axi_launch: test_axi_launch.cpp axi_launch.cpp axi_launch.hpp
	$(CXX) $(CXXFLAGS) test_axi_launch.cpp axi_launch.cpp -o $@
test_async_jpeg: test_async_jpeg.cpp async_jpeg.cpp async_jpeg.hpp
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags opencv4) test_async_jpeg.cpp async_jpeg.cpp -o $@ $(OPENCV_FLAGS)
test_safety_logger: test_safety_logger.cpp safety_logger.cpp safety_logger.hpp encoder.hpp
	$(CXX) $(CXXFLAGS) test_safety_logger.cpp safety_logger.cpp -o $@
test_logger: test_logger.cpp logger.cpp logger.hpp encoder.hpp
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags opencv4) test_logger.cpp logger.cpp -o $@ $(OPENCV_FLAGS)
test_protocol: test_protocol.cpp protocol.hpp
	$(CXX) $(CXXFLAGS) test_protocol.cpp -o $@
clean:
	rm -f $(TARGET) $(OBJS) test_protocol test_async_jpeg test_logger test_safety_logger test_axi_launch

