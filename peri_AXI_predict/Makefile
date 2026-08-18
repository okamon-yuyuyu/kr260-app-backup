CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
OPENCV_FLAGS := $(shell pkg-config --cflags --libs opencv4)

TARGET := peri_AXI_predict
SRCS := main.cpp command.cpp camera.cpp encoder.cpp aruco_detector.cpp \
        wire_adjuster.cpp dead_band_setting.cpp model_predictor.cpp \
        prediction_logger.cpp
OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean run preview

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(OPENCV_FLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(OPENCV_FLAGS)

run: $(TARGET)
	sudo ./$(TARGET)

preview: $(TARGET)
	./$(TARGET) --preview

clean:
	rm -f $(TARGET) $(OBJS)
