# change if de1-soc linux doesn't support this version
cmake_minimum_required(VERSION 3.1) 
project(IO)

add_library(io "io.c" "io.h" "stb_image_write.h")
target_include_directories(io PUBLIC ./)

set_property(TARGET io PROPERTY C_STANDARD 99)
set_property(TARGET io PROPERTY C_STANDARD_REQUIRED)