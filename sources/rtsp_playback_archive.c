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

  GstElement *pipeline, *splitmuxsrc, *decodebin, *autovideosink;
  GstBus *bus;

  GstPad *sinkpad;
  GstPad *parsesrc;

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_pipeline_new ("rtsp-arhive-player"); 
  splitmuxsrc = gst_element_factory_make ("splitmuxsrc", "splitmuxsrc");
  decodebin = gst_element_factory_make ("decodebin", "decodebin");
  autovideosink = gst_element_factory_make ("autovideosink", "autovideosink");

  if (!pipeline || !splitmuxsrc || !decodebin || !autovideosink) {
    g_printerr ("One of element is not initialized");
    return -1;
  }

  g_object_set (G_OBJECT (splitmuxsrc), "location", "../archive/video*.mov", NULL);

  gst_bin_add_many (GST_BIN (pipeline), splitmuxsrc, decodebin, autovideosink, NULL);

  if (!g_signal_connect (splitmuxsrc, "pad-added", G_CALLBACK (on_pad_added), decodebin)) {
    g_error ("Linked failed #2");
    return -1;
  }


  if (!g_signal_connect (decodebin, "pad-added", G_CALLBACK (on_pad_added), autovideosink)) {
    g_error ("Linked failed #2");
    return -1;
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  g_print ("Running...");

  g_main_loop_run (loop);

  g_print ("Stopped");
}

