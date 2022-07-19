cmake_minimum_required(VERSION 3.22)
project(loggerinio)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(loggerinio
        devlog.c
        devlog.h)
