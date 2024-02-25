cmake_minimum_required(VERSION 2.8.12)

find_path(ffmpeg_INCLUDE_DIR
  NAMES libavformat/avformat.h
  NAMES libavcodec/avcodec.h
  NAMES libavutil/avutil.h
  NAMES libavutil/pixdesc.h
  NAMES libswscale/swscale.h
)

find_path(ffmpeg_LIBRARY
  NAMES lib/libavcodec.so
  NAMES lib/libavformat.so
  NAMES lib/ibswscale.so
  NAMES lib/libavutil.so
)

mark_as_advanced(ffmpeg_INCLUDE_DIR)
mark_as_advanced(ffmpeg_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ffmpeg
  FOUND_VAR ffmpeg_FOUND
  REQUIRED_VARS ffmpeg_INCLUDE_DIR ffmpeg_LIBRARY
)


