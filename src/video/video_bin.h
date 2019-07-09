#ifndef VIDEO_BIN_H_
#define VIDEO_BIN_H_

#include <gst/gst.h>

GstElement * create_video_bin (gchar *location, gchar *login, gchar *password);

#endif
