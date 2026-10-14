cmake_minimum_required(VERSION 3.22.1)
project("nexusvpn")
add_library(aimbot SHARED aimbot.cpp)
target_link_libraries(aimbot android log)
