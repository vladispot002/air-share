CXX ?= g++
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS = $(shell pkg-config --libs gtk+-3.0)
SSL_CFLAGS ?= $(shell pkg-config --cflags openssl 2>/dev/null)
SSL_FLAGS ?= $(shell pkg-config --libs openssl 2>/dev/null || ([ -f lib/libssl.so ] && echo "-Llib -lssl -lcrypto") || ([ -f /lib64/libssl.so.3 ] && echo "-L/lib64 -lssl -lcrypto") || echo "-lssl -lcrypto")
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2 -Iinclude -pthread $(GTK_CFLAGS) $(SSL_CFLAGS)
LDFLAGS ?= -pthread $(SSL_FLAGS) -lX11 $(GTK_LIBS) -ldl

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

.PHONY: all clean run gui mobile-gui mobile-test web desktop test

TEST_TARGET = $(BIN_DIR)/lan_drop_tests
TEST_OBJS = $(filter-out $(OBJ_DIR)/main.o, $(OBJS)) $(OBJ_DIR)/tests/unit_tests.o

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

mobile-gui: $(TARGET)
	@./$(TARGET) mobile-gui

mobile-test: $(TARGET)
	@./$(TARGET) mobile-test

$(OBJ_DIR)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_TARGET): $(TEST_OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@

test: $(TEST_TARGET)
	@./$(TEST_TARGET)

web:
	@cd "Design LAN-Drop macOS UI" && npm run dev -- --host 0.0.0.0 --port 5173

desktop:
	@mkdir -p ~/.local/share/applications ~/.local/share/icons/hicolor/scalable/apps
	@cp -f resources/lan-drop.svg ~/.local/share/icons/hicolor/scalable/apps/lan-drop.svg 2>/dev/null || true
	@mkdir -p ~/Стільниця ~/Desktop 2>/dev/null || true
	@[ -d ~/Стільниця ] && cp -f resources/lan-drop.desktop ~/Стільниця/lan-drop.desktop && chmod +x ~/Стільниця/lan-drop.desktop && gio set ~/Стільниця/lan-drop.desktop metadata::trusted true 2>/dev/null || true
	@[ -d ~/Desktop ] && cp -f resources/lan-drop.desktop ~/Desktop/lan-drop.desktop && chmod +x ~/Desktop/lan-drop.desktop && gio set ~/Desktop/lan-drop.desktop metadata::trusted true 2>/dev/null || true
	@cp -f resources/lan-drop.desktop ~/.local/share/applications/lan-drop.desktop
	@chmod +x ~/.local/share/applications/lan-drop.desktop
	@update-desktop-database ~/.local/share/applications 2>/dev/null || true

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
