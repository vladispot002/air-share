cmake_minimum_required(VERSION 3.20)
project(lan_drop CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    src/main.cpp
)

add_executable(lan_drop ${SOURCES})
