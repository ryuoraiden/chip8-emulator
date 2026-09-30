CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
# -MMD writes a .d file listing the headers each .cpp includes, so editing a header rebuilds its users
DEPFLAGS = -MMD -MP
TARGET = chip8
SOURCES = src/main.cpp src/chip8.cpp
OBJECTS = $(SOURCES:.cpp=.o)
DEPS = $(OBJECTS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJECTS) -lSDL2

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)

-include $(DEPS)

.PHONY: all clean