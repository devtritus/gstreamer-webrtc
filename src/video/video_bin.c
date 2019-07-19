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

static const gchar *camera_login = NULL;
static const gchar *camera_password = NULL;
static const gchar *camera_location = NULL;

static GOptionEntry entries[] =
{
  { "camera-login", 0, 0, G_OPTION_ARG_STRING, &camera_login, "Camera user", NULL },
  { "camera-password", 0, 0, G_OPTION_ARG_STRING, &camera_password, "Camera password", NULL },
  { "camera-location", 0, 0, G_OPTION_ARG_STRING, &camera_location, "Camera location", NULL },
  { NULL },
};

typedef struct {
  GstElement * pipeline;
  GstElement * tee;
} StreamPipeline;


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

static StreamPipeline *
create_stream_pipeline() {
  GstElement *pipeline, *src, *depay, *tee, *queue, *sink;
  StreamPipeline *stream_pipeline;
  GstPadTemplate *sinkTemplate;
  
  pipeline = gst_pipeline_new ("video-pipeline");

  src   = gst_element_factory_make ("rtspsrc",      "src");
  depay = gst_element_factory_make ("rtph264depay", "depay");
  tee =   gst_element_factory_make ("tee",          "tee");
  queue = gst_element_factory_make ("queue",        "queue");
  sink  = gst_element_factory_make ("filesink", "sink");

  if (!pipeline || !src || !depay || !tee || !queue || !sink) {
    g_printerr ("Failed to create elements");
    return NULL;
  }

  g_object_set (G_OBJECT (src), "location", camera_location,
                                "user-id", camera_login,
                                "user-pw", camera_password, NULL);

  /* g_object_set (G_OBJECT (tee), "allow-not-linked", TRUE, NULL); */

  g_object_set (G_OBJECT(sink), "location", "archive/video.mp4", NULL);

  gst_bin_add_many (GST_BIN (pipeline),
                             src, depay, tee, queue, sink, NULL);

  if(!gst_element_link_many (depay, tee, queue, sink, NULL)) {
    g_printerr ("Failed to link elements");
    return NULL;
  }

  g_signal_connect (src, "pad-added", G_CALLBACK (on_pad_added), depay);

  stream_pipeline = (StreamPipeline *)g_malloc0 (sizeof (StreamPipeline));
  stream_pipeline->pipeline = pipeline;
  stream_pipeline->tee = tee;

  return stream_pipeline;
}

static void *
start_pipeline(gpointer data) {
  GMainLoop *loop;
  GstBus *bus;
  guint bus_watch_id;
  GstElement * pipeline;

  StreamPipeline * stream_pipeline = (StreamPipeline *)data;
  pipeline = stream_pipeline->pipeline;

  loop = g_main_loop_new (NULL, FALSE);
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

  return NULL;
}

int main(int argc, char *argv[])
{
  StreamPipeline * stream_pipeline;
  GThread * pipelineThread;
  gchar str[16];
  GOptionContext * context = NULL;
  GError * error = NULL;

  /* Initialisation */
  gst_init (&argc, &argv);

  context = g_option_context_new ("- gstreamer video");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("Error initializing: %s\n", error->message);
    return -1;
  }

  g_print ("%s %s %s\n", camera_location, camera_login, camera_password);
  g_print ("Welcome to camera viewer.\nPlease, enter a command:\n");
  do {
    scanf("%s", str);
    if(g_strcmp0 ("run", str) == 0) {
      stream_pipeline = create_stream_pipeline();
      pipelineThread = g_thread_new("stream", &start_pipeline, stream_pipeline);
      g_print("Create rtsp stream %p\n", pipelineThread);
    } else if (g_strcmp0 ("file", str) == 0) {
      /*add_splitmuxsink(stream_pipeline);*/
    } else if (g_strcmp0 ("webrtc", str) == 0) {
    } else {
      g_print ("Unknown command\n");
    }
  } while (g_strcmp0 ("end", str) != 0);

  return 0;
}

