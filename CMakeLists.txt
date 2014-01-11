cmake_minimum_required (VERSION 2.6)
project(m3dsync)

add_definitions(-Wall -O2 -std=c++0x)

add_executable( m3dsync m3dsync.cpp )

target_link_libraries( m3dsync crypto++ )
