#include <gst/gst.h>
#include <glib.h>

#define STUN_SERVER "stun://stun.l.google.com:19302"

/*g_snprintf(command, 512,
            "rtspsrc user-id=%s user-pw=%s location=%s"
            " ! rtph264depay ! rtph264pay config-interval=10"
            " ! application/x-rtp,media=video,encoding-name=H264,payload=96 ! webrtcbin bundle-policy=max-bundle name=webrtc-data-channel stun-server=" STUN_SERVER,
            video_channel.login, video_channel.password, video_channel.location);
            */

static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}


static void
on_pad_added (GstElement *element,
              GstPad     *pad,
              gpointer    data)
{
  GstPad *sinkpad;
  GstElement *decoder = (GstElement *) data;

  /* We can now link this pad with the vorbis-decoder sink pad */
  g_print ("Dynamic pad created, linking demuxer/decoder\n");

  sinkpad = gst_element_get_static_pad (decoder, "sink");

  gst_pad_link (pad, sinkpad);

  gst_object_unref (sinkpad);
}

GstElement * create_video_bin (char *location, char *login, char *password)
{
  GstElement *bin, *rtspsrc, *depay, *tee;
  GstBus *bus;

  bin = gst_pipeline_new ("video-bin");
  rtspsrc   = gst_element_factory_make ("rtspsrc",      "rtspsrc");
  depay     = gst_element_factory_make ("rtph264depay", "rtph264depay");
  tee       = gst_element_factory_make ("tee",          "tee");

  if (!bin || !rtspsrc || !depay || !tee) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  g_object_set (G_OBJECT (rtspsrc), "location",location,
                                    "user-id", login,
                                    "user-pw", password, NULL);

  gst_bin_add_many (GST_BIN (bin), rtspsrc, depay, tee, NULL);

  gst_element_link (depay, tee);
  g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added), depay);

  return bin;
}

