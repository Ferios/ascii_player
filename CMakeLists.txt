cmake_minimum_required (VERSION 3.5)
project("ascii_player")

include(CheckIncludeFile)

include_directories(
  include/
  "${PROJECT_BINARY_DIR}")


#######################
# CMAKE Dependencies  #
#######################
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
    "${CMAKE_SOURCE_DIR}/cmake/Modules/")

# Search for ffmpeg
find_package(ffmpeg REQUIRED)
include_directories("${ffmpeg_INCLUDE_DIR}" SYSTEM)
		    
# Search for SDL
find_package(SDL2 REQUIRED)
include_directories("${SDL2_INCLUDE_DIR}/SDL2" SYSTEM)

# Search for SDL_ttf
find_package(SDL2_ttf REQUIRED)
include_directories("${SDL2_ttf_INCLUDE_DIR}/SDL2" SYSTEM)

################
# Source files #
################

file(GLOB chord_SRC
    "./src/main.cpp")

# Executables
add_executable(ascii_player ${chord_SRC})

# Libraries to link
link_directories(${ffmpeg_LIBRARY}/lib)
target_link_libraries(ascii_player
     avformat 
     avcodec 
     avutil
     swscale
     SDL2
     SDL2_ttf
)


################
# Installation #
################
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/ascii_player
    DESTINATION bin)


