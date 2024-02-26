cmake_minimum_required(VERSION 3.5)

find_path(SDL2_ttf_INCLUDE_DIR
  NAMES SDL2/SDL_ttf.h
)

find_path(SDL2_ttf_LIBRARY
  NAMES lib/libSDL2_ttf.so
)

mark_as_advanced(SDL2_ttf_INCLUDE_DIR)
mark_as_advanced(SDL2_ttf_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2_ttf
  FOUND_VAR SDL2_ttf_FOUND
  REQUIRED_VARS SDL2_ttf_INCLUDE_DIR SDL2_ttf_LIBRARY
)


