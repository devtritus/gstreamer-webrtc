#!/bin/sh

login=`cat ../../camera_login`
password=`cat ../../camera_password`
location=`cat ../../camera_location`

gst-launch-1.0 rtspsrc user-id=$login user-pw=$password location=$location ! rtph264depay ! decodebin ! x264enc ! rtph264pay ! rtph264depay ! filesink location=../../xz.h264
