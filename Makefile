CXX ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -Iinclude -pthread

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/core/*.cpp) \
       $(wildcard $(SRC_DIR)/peers/*.cpp) \
       $(wildcard $(SRC_DIR)/tasks/*.cpp) \
       $(SRC_DIR)/main.cpp

OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
TARGET = $(BIN_DIR)/lan_drop

.PHONY: all clean run

all: $(TARGET)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@

run: $(TARGET)
	@./$(TARGET) demo

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
