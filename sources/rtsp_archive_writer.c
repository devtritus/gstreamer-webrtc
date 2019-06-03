#include <gst/gst.h>

static void on_pad_added (GstElement *element, GstPad *pad, gpointer data) {
  GstPad *sinkpad;
  GstPadLinkReturn ret;
  GstElement *depay = (GstElement *) data;
  g_print ("Dynamic pad created, linking...\n");
  sinkpad = gst_element_get_static_pad (depay, "sink");

  if (gst_pad_is_linked (sinkpad)) {
    g_print ("*** We are already linked ***\n");
    gst_object_unref (sinkpad);
    return;
  } else {
    g_print ("Processing to linking...\n");
  }
  ret = gst_pad_link (pad, sinkpad);

  if(GST_PAD_LINK_FAILED (ret)) {
    g_print ("Failed to link dynamically\n");
  } else {
    g_print ("Dynamically link successful\n");
  }

  gst_object_unref (sinkpad);
}

int main (int argc, char *argv[]) 
{
  GMainLoop *loop;

  GstElement *pipeline, *rtspsrc, *rtph264depay, *h264parse, *splitmuxsink;
  GstBus *bus;

  GstPad *sinkpad;
  GstPad *parsesrc;

  gchar *rtspUrl, *login, *password;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  if (argc != 4) {
    g_printerr ("Rtsp url, login, password are required");
    return -1;
  }

  rtspUrl = argv[1];
  login = argv[2];
  password = argv[3];

  g_print("%s, %s, %s\n", rtspUrl, login, password);

  pipeline = gst_pipeline_new ("rtsp-arhive-writer"); 
  rtspsrc = gst_element_factory_make ("rtspsrc", "rtspsrc");
  rtph264depay = gst_element_factory_make ("rtph264depay", "rtph264depay");
  h264parse = gst_element_factory_make ("h264parse", "h264parse");
  splitmuxsink = gst_element_factory_make ("splitmuxsink", "splitmuxsink");

  if (!pipeline || !rtspsrc || !rtph264depay || !h264parse || !splitmuxsink) {
    g_printerr ("One of element is not initialized\n");
    return -1;
  }

  g_object_set (G_OBJECT (rtspsrc),
      "location", rtspUrl,
      "user-id", login,
      "user-pw", password,
      NULL);

  g_object_set (G_OBJECT (splitmuxsink),
     "location", "../archive/video%02d.mov",
     "max-size-time", 0,
     "max-size-bytes", 33554432,
     NULL);

  gst_bin_add_many (GST_BIN (pipeline), rtspsrc, rtph264depay, h264parse, splitmuxsink, NULL);

  if (!gst_element_link_many (rtph264depay, h264parse, splitmuxsink, NULL)) {
    g_error ("Linked failed\n");
    return -1;
  }

  if (!g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added), rtph264depay)) {
    g_error ("Linked failed #2\n");
    return -1;
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_print ("Running...\n");

  g_main_loop_run (loop);

  g_print ("Stopped\n");
}

