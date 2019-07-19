cmake_minimum_required(VERSION 3.8)
project (station_core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTLIBS REQUIRED
        gobject-2.0
        glib-2.0
        gstreamer-1.0
        gstreamer-sdp-1.0
        gstreamer-webrtc-1.0
        libsoup-2.4
        json-glib-1.0)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g -ggdb -Werror -fno-omit-frame-pointer")

# Following example at https://stackoverflow.com/questions/38959678/is-there-a-simple-gstreamer-example-for-c
# https://gstreamer.freedesktop.org/documentation/installing/on-linux.html

# add extra include directories
# gst-config.h moved to /usr/lib/x86_64-linux-gnu/gstreamer-1.0/include
include_directories(${GSTLIBS_INCLUDE_DIRS})

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../../bin)

set(SOURCE_FILES
  video/video_bin.h
  video/video_bin.c)

add_executable(station_core ${SOURCE_FILES})

target_link_libraries(station_core ${GSTLIBS_LIBRARIES})

#old Makefile
#set (CC $("gcc"))
#set (LIBS   $(shell pkg-config --libs --cflags glib-2.0 gstreamer-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 json-glib-1.0 libsoup-2.4))
#set (CFLAGS "-O0 -g -ggdb -Wall -fno-omit-frame-pointer \
#		$(shell pkg-config --cflags glib-2.0 gstreamer-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 json-glib-1.0 libsoup-2.4)
#../../../bin/webrtc-sendrecv: webrtc-sendrecv.c
#		"$(CC)" $(CFLAGS) $^ $(LIBS) -o $@

