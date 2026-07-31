CXX = g++
CXXFLAGS = -O2 -Wall -std=c++17

TARGET = camera_calibration

SRC = camera_calibration.cpp

OPENCV_FLAGS := $(shell pkg-config --cflags --libs opencv4)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(OPENCV_FLAGS)

clean:
	rm -f $(TARGET)