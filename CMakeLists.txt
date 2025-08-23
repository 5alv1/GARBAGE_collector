cmake_minimum_required(VERSION 3.22)
project(GARBAGE_collector C)

set(CMAKE_C_STANDARD 23)

add_library(GARBAGE_collector STATIC library.c)
