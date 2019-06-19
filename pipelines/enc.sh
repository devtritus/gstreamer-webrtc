#!/bin/sh

login=`cat ../../camera_login`
password=`cat ../../camera_password`
location=`cat ../../camera_location`

gst-launch-1.0 -v rtspsrc user-id=$login user-pw=$password location=$location ! rtph264depay ! decodebin ! x264enc ! rtph264pay ! filesink location=../../enc.h264
