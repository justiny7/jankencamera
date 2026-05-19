CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

BUILD_DIR = build

TARGET = $(BUILD_DIR)/image_processor

OBJS = \
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/image.o \
	$(BUILD_DIR)/mertens.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/main.o: main.cpp image.h mertens.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/image.o: image.cpp image.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/mertens.o: mertens.cpp mertens.h image.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
