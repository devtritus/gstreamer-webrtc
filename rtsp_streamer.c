#include <gst/gst.h>

static void on_pad_added (GstElement *element, GstPad *pad, gpointer data) {
  GstPad *sinkpad;
  GstPadLinkReturn ret;
  GstElement *sinkelement = (GstElement *) data;
  g_print ("Dynamic pad created, linking...\n");
  sinkpad = gst_element_get_static_pad (sinkelement, "sink");

  if (gst_pad_is_linked (sinkpad)) {
    g_print ("*** We are already linked ***\n");
    gst_object_unref (sinkpad);
    return;
  } else {
    g_print ("processing to linking...\n");
  }
  ret = gst_pad_link (pad, sinkpad);
  g_print ("%s -> %s\n", gst_pad_get_name(pad), gst_pad_get_name(sinkpad));

  if(GST_PAD_LINK_FAILED (ret)) {
    g_print ("failed to link dynamically\n");
  } else {
    g_print ("dynamically link successful\n");
  }

  gst_object_unref (sinkpad);
}

int main (int argc, char *argv[]) 
{
  GMainLoop *loop;

  GstElement *pipeline, *rtspsrc, *rtph264depay, *h264parse, *decodebin, *autovideosink;
  GstBus *bus;

  GstPad *sinkpad;
  GstPad *parsesrc;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /*
  if (argc != 2) {
    g_printerr ("Rtsp url as argument is required");
    return -1;
  }
  */

  pipeline = gst_pipeline_new ("rtsp-arhive-writer"); 
  rtspsrc = gst_element_factory_make ("rtspsrc", "rtspsrc");
  rtph264depay = gst_element_factory_make ("rtph264depay", "rtph264depay");
  h264parse = gst_element_factory_make ("h264parse", "h264parse");
  decodebin = gst_element_factory_make ("decodebin", "decodebin");
  autovideosink = gst_element_factory_make ("autovideosink", "autovideosink");

  if (!pipeline || !rtspsrc || !rtph264depay || !h264parse || !decodebin || !autovideosink) {
    g_printerr ("One of element is not initialized");
    return -1;
  }

  g_object_set (G_OBJECT (rtspsrc),
      "location", "rtsp://172.17.13.213:554/cam/realmonitor?channel=1&subtype=0",
      "user-id", "admin",
      "user-pw", "Barco1984",
      NULL);

  gst_bin_add_many (GST_BIN (pipeline), rtspsrc, rtph264depay, h264parse, decodebin, autovideosink, NULL);

  if (!gst_element_link_many (rtph264depay, h264parse, decodebin, NULL)) {
    g_error ("Linked failed");
    return -1;
  }

  if (!g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added), rtph264depay)) {
    g_error ("Linked failed #2");
    return -1;
  }

  if (!g_signal_connect (decodebin, "pad-added", G_CALLBACK (on_pad_added), autovideosink)) {
    g_error ("Linked failed #3");
    return -1;
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_print ("Running...");

  g_main_loop_run (loop);

  g_print ("Stopped");
}

