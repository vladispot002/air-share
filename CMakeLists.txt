cmake_minimum_required(VERSION 3.20)
project(lan_drop CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB_RECURSE SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(lan_drop ${SOURCES})
target_compile_options(lan_drop PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(lan_drop PRIVATE pthread)
