CC     := gcc
LIBS   := $(shell pkg-config --libs --cflags glib-2.0 gstreamer-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 json-glib-1.0 libsoup-2.4)
CFLAGS := -O0 -g -ggdb -Wall -fno-omit-frame-pointer \
		$(shell pkg-config --cflags glib-2.0 gstreamer-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 json-glib-1.0 libsoup-2.4)
../../../bin/webrtc-sendrecv: webrtc-sendrecv.c
		"$(CC)" $(CFLAGS) $^ $(LIBS) -o $@
