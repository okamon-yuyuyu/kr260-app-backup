TARGET = aruco_test
SRC = aruco_test.cpp

CXX = g++
CXXFLAGS = -std=c++17 -O2
LDFLAGS = `pkg-config --cflags --libs opencv4`

all:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)