CXX ?= g++
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS = $(shell pkg-config --libs gtk+-3.0)
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -Iinclude -pthread $(GTK_CFLAGS)
LDFLAGS ?= -pthread -lX11 $(GTK_LIBS)

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR)/core/*.cpp) \
       $(wildcard $(SRC_DIR)/peers/*.cpp) \
       $(wildcard $(SRC_DIR)/tasks/*.cpp) \
       $(wildcard $(SRC_DIR)/ui/*.cpp) \
       $(wildcard $(SRC_DIR)/mobile/*.cpp) \
       $(SRC_DIR)/main.cpp

OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))
TARGET = $(BIN_DIR)/lan_drop

.PHONY: all clean run gui mobile-test web desktop

all: $(TARGET)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

run: $(TARGET)
	@./$(TARGET) demo

gui: $(TARGET)
	@./$(TARGET) gui

mobile-test: $(TARGET)
	@./$(TARGET) mobile-test

web:
	@cd "Design LAN-Drop macOS UI" && npm run dev -- --host 0.0.0.0 --port 5173

desktop:
	@cp -f resources/lan-drop.desktop ~/Стільниця/lan-drop.desktop
	@cp -f resources/lan-drop.desktop ~/.local/share/applications/lan-drop.desktop
	@chmod +x ~/Стільниця/lan-drop.desktop ~/.local/share/applications/lan-drop.desktop
	@gio set ~/Стільниця/lan-drop.desktop metadata::trusted true 2>/dev/null || true
	@update-desktop-database ~/.local/share/applications 2>/dev/null || true

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
