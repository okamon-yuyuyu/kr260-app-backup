CXX ?= g++
PYTHON ?= python3

TORCH_ROOT := $(shell $(PYTHON) -c "import pathlib, torch; print(pathlib.Path(torch.__file__).resolve().parent)" 2>/dev/null)
TORCH_ABI := $(shell $(PYTHON) -c "import torch; print(int(torch._C._GLIBCXX_USE_CXX11_ABI))" 2>/dev/null)
TORCH_INCLUDES := -I$(TORCH_ROOT)/include -I$(TORCH_ROOT)/include/torch/csrc/api/include
TORCH_LIBS := -L$(TORCH_ROOT)/lib -Wl,-rpath,$(TORCH_ROOT)/lib -ltorch -ltorch_cpu -lc10

OPENCV_CFLAGS := $(shell pkg-config --cflags opencv4)
OPENCV_LIBS := $(shell pkg-config --libs opencv4)

CXXFLAGS := -std=c++17 -Wall -Wextra -O2 \
            -D_GLIBCXX_USE_CXX11_ABI=$(TORCH_ABI) \
            $(TORCH_INCLUDES) $(OPENCV_CFLAGS)
LDLIBS := $(OPENCV_LIBS) $(TORCH_LIBS) -pthread

TARGET := peri_AXI_predict_torchscript
BENCHMARK_TARGET := benchmark_torchscript

APP_SRCS := main.cpp command.cpp camera.cpp encoder.cpp aruco_detector.cpp \
            wire_adjuster.cpp dead_band_setting.cpp torchscript_predictor.cpp \
            prediction_logger.cpp
APP_OBJS := $(APP_SRCS:.cpp=.o)

BENCHMARK_SRCS := benchmark_torchscript.cpp torchscript_predictor.cpp
BENCHMARK_OBJS := $(BENCHMARK_SRCS:.cpp=.o)

.PHONY: all clean run preview check-env

all: $(TARGET) $(BENCHMARK_TARGET)

check-env:
	@test -n "$(TORCH_ROOT)" || (echo "Python package torch was not found" && false)
	@test -n "$(TORCH_ABI)" || (echo "Could not read PyTorch C++ ABI setting" && false)
	@pkg-config --exists opencv4 || (echo "opencv4 development package was not found" && false)
	@echo "PyTorch root: $(TORCH_ROOT)"
	@echo "PyTorch C++11 ABI: $(TORCH_ABI)"

$(TARGET): $(APP_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

$(BENCHMARK_TARGET): $(BENCHMARK_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

$(TARGET) $(BENCHMARK_TARGET): | check-env
$(APP_OBJS) $(BENCHMARK_OBJS): | check-env

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	sudo ./$(TARGET)

preview: $(TARGET)
	./$(TARGET) --preview

clean:
	rm -f $(TARGET) $(BENCHMARK_TARGET) $(APP_OBJS) benchmark_torchscript.o
