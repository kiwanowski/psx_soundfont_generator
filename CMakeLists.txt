cmake_minimum_required(VERSION 3.0)
project(SoundfontPSX)

# Set the directories
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/source)
set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/output)

# Find all source files in the source directory
file(GLOB_RECURSE SOURCES ${SOURCE_DIR}/*.c)

# Add the executable target
add_executable(psx_soundfont_generator ${SOURCES})

# Set the output directory for the executable
set_target_properties(psx_soundfont_generator PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set_property(TARGET psx_soundfont_generator PROPERTY C_STANDARD 23)
