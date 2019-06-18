#!/bin/sh

login=`cat ../../camera_login`
password=`cat ../../camera_password`
location=`cat ../../camera_location`

gst-launch-1.0 rtspsrc user-id=$login user-pw=$password location=$location ! tee name=t \
      t. ! queue ! filesink location=../../data/rtp.h264 async=false \
      t. ! queue ! rtph264depay ! filesink location=../../data/depay.h264 async=false \
      t. ! queue ! rtph264depay ! h264parse ! "video/x-h264, stream-format=byte-stream, alignment=nal" ! filesink location=../../data/depay_parse.h264 async=false \
      t. ! queue ! rtph264depay ! decodebin ! x264enc ! filesink location=../../data/enc.h264
