cmake_minimum_required(VERSION 3.21)
project(YukiSend VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Gui Widgets Network Sql Svg)

add_subdirectory(src)
add_subdirectory(tests)
