cmake_minimum_required(VERSION 2.8.12)

find_path(SDL2_INCLUDE_DIR
  NAMES SDL2/SDL.h
)

find_path(SDL2_LIBRARY
  NAMES lib/libSDL2.so
)

mark_as_advanced(SDL2_INCLUDE_DIR)
mark_as_advanced(SDL2_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2
  FOUND_VAR SDL2_FOUND
  REQUIRED_VARS SDL2_INCLUDE_DIR SDL2_LIBRARY
)


