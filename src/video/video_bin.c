#include <gst/gst.h>
#include <stdio.h>
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

static void *
create_stream_pipeline(gpointer data) {
  GMainLoop *loop;

  GstBus *bus;
  guint bus_watch_id;
  GstElement *pipeline, *rtspsrc, *depay, *tee;

  loop = g_main_loop_new (NULL, FALSE);
  
  pipeline = gst_pipeline_new ("video-pipeline");
  rtspsrc   = gst_element_factory_make ("rtspsrc",      "rtspsrc");
  depay     = gst_element_factory_make ("rtph264depay", "rtph264depay");
  tee       = gst_element_factory_make ("tee",          "tee");

  if (!pipeline || !rtspsrc || !depay || !tee) {
    g_printerr ("One element could not be created. Exiting.\n");
    return NULL;
  }

  g_object_set (G_OBJECT (rtspsrc), "location", "rtsp://admin:Barco1984@172.17.13.216:554/cam/realmonitor?channel=1&subtype=0",
                                    "user-id", "admin",
                                    "user-pw", "Barco1984", NULL);

  gst_bin_add_many (GST_BIN (pipeline), rtspsrc, depay, tee, NULL);

  gst_element_link (depay, tee);
  g_signal_connect (rtspsrc, "pad-added", G_CALLBACK (on_pad_added), depay);

  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  /* Set the pipeline to "playing" state*/
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Iterate */
  g_print ("Running...\n");
  g_main_loop_run (loop);

  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return pipeline;
}

int main(int argc, char *argv[])
{
  GThread * pipelineThread;
  gchar * str = NULL;
  /* Initialisation */
  gst_init (&argc, &argv);

  do {
    scanf("%s", str);
    if(g_strcmp0 ("run", str) == 0) {
      pipelineThread = g_thread_new("stream", &create_stream_pipeline, NULL);
      g_print("%p\n", pipelineThread);
    }
  } while (g_strcmp0 ("end", str) != 0);

  return 0;
}

