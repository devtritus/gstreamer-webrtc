/*
 * Demo gstreamer app for negotiating and streaming a sendrecv webrtc stream
 * with a browser JS app.
 *
 * gcc webrtc-sendrecv.c $(pkg-config --cflags --libs gstreamer-webrtc-1.0 gstreamer-sdp-1.0 libsoup-2.4 json-glib-1.0) -o webrtc-sendrecv
 *
 * Author: Nirbheek Chauhan <nirbheek@centricular.com>
 */
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#include <stdio.h>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>

/* For signalling */
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include <string.h>

enum ChannelState {
  APP_STATE_UNKNOWN = 0,
  APP_STATE_ERROR = 1, /* generic error */
  SERVER_CONNECTING = 1000,
  SERVER_CONNECTION_ERROR,
  SERVER_CONNECTED, /* Ready to register */
  SERVER_REGISTERING = 2000,
  SERVER_REGISTRATION_ERROR,
  SERVER_REGISTERED, /* Ready to call a peer */
  SERVER_CLOSED, /* server connection closed by us or the server */
  PEER_CONNECTING = 3000,
  PEER_CONNECTION_ERROR,
  PEER_CONNECTED,
  PEER_CALL_NEGOTIATING = 4000,
  PEER_CALL_STARTED,
  PEER_CALL_STOPPING,
  PEER_CALL_STOPPED,
  PEER_CALL_ERROR,
};

static GMainLoop *loop;
static GstElement *pipe1, *webrtc1;
static GObject *send_channel, *receive_channel;

static const gchar *peer_id = NULL;
static const gchar *server_url = "wss://localhost:8443";
static gboolean disable_ssl = FALSE;
static const gchar *camera_login = NULL;
static const gchar *camera_password = NULL;
static const gchar *camera_location = NULL;

static int next_id;

typedef struct {
  gchar *login;
  gchar *password;
  gchar *location;
} VideoChannel;  

typedef struct {
  gchar *peer_id;
  enum ChannelState state;
  SoupWebsocketConnection *conn;
  VideoChannel *video;
} Channel;

static GOptionEntry entries[] =
{
  { "peer-id", 0, 0, G_OPTION_ARG_STRING, &peer_id, "String ID of the peer to connect to", "ID" },
  { "server", 0, 0, G_OPTION_ARG_STRING, &server_url, "Signalling server to connect to", "URL" },
  { "disable-ssl", 0, 0, G_OPTION_ARG_NONE, &disable_ssl, "Disable ssl", NULL },
  { "camera-login", 0, 0, G_OPTION_ARG_STRING, &camera_login, "Camera user", NULL },
  { "camera-password", 0, 0, G_OPTION_ARG_STRING, &camera_password, "Camera password", NULL },
  { "camera-location", 0, 0, G_OPTION_ARG_STRING, &camera_location, "Camera location", NULL },
  { NULL },
};

static void connect_to_websocket_server_async (Channel *channel);

static gboolean
cleanup_and_quit_loop (const gchar * msg, Channel *channel, enum ChannelState state)
{
  if (msg)
    g_printerr ("%s\n", msg);
  if (state > 0)
    channel->state = state;

  if (channel->conn) {
    if (soup_websocket_connection_get_state (channel->conn) ==
        SOUP_WEBSOCKET_STATE_OPEN)
      /* This will call us again */
      soup_websocket_connection_close (channel->conn, 1000, "");
    else
      g_object_unref (channel->conn);
  }

  if (loop) {
    g_main_loop_quit (loop);
    loop = NULL;
  }

  /* To allow usage as a GSourceFunc */
  return G_SOURCE_REMOVE;
}

static gchar*
get_string_from_json_object (JsonObject * object)
{
  JsonNode *root;
  JsonGenerator *generator;
  gchar *text;

  /* Make it the root node */
  root = json_node_init_object (json_node_alloc (), object);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  text = json_generator_to_data (generator, NULL);

  /* Release everything */
  g_object_unref (generator);
  json_node_free (root);
  return text;
}

static void
send_ice_candidate_message (GstElement * webrtc G_GNUC_UNUSED, guint mlineindex,
    gchar * candidate, gpointer user_data G_GNUC_UNUSED)
{
  gchar *text;
  JsonObject *ice, *msg;
  Channel *channel = (Channel *)user_data;

  if (channel->state < PEER_CALL_NEGOTIATING) {
    cleanup_and_quit_loop ("Can't send ICE, not in call", channel, APP_STATE_ERROR);
    return;
  }

  ice = json_object_new ();
  json_object_set_string_member (ice, "candidate", candidate);
  json_object_set_int_member (ice, "sdpMLineIndex", mlineindex);
  msg = json_object_new ();
  json_object_set_object_member (msg, "ice", ice);
  text = get_string_from_json_object (msg);
  json_object_unref (msg);

  soup_websocket_connection_send_text (channel->conn, text);
  g_free (text);
}

static void
send_sdp_offer (GstWebRTCSessionDescription * offer, Channel *channel)
{
  gchar *text;
  JsonObject *msg, *sdp;

  if (channel->state < PEER_CALL_NEGOTIATING) {
    cleanup_and_quit_loop ("Can't send offer, not in call", channel, APP_STATE_ERROR);
    return;
  }

  if(channel->video != NULL) {
    int i;
    GstSDPMedia *video = (GstSDPMedia *)&g_array_index(offer->sdp->medias, GstSDPMedia, 0);

    gchar const *key = "fmtp";
    for(i = 0; i < gst_sdp_media_attributes_len(video); i++) {
      const GstSDPAttribute *attr = gst_sdp_media_get_attribute(video, i);
      if(!g_strcmp0(attr->key, key)) {
        GstSDPAttribute *new_attr = g_new0(GstSDPAttribute, 1);
        gst_sdp_attribute_set(new_attr, key, "96 profile-level-id=42e01f;packetization-mode=1");
        gst_sdp_media_replace_attribute (video, i, new_attr);
        break;
      }
    }
  }

  text = gst_sdp_message_as_text (offer->sdp);
  g_print ("Sending offer:\n%s\n", text);

  sdp = json_object_new ();
  json_object_set_string_member (sdp, "type", "offer");
  json_object_set_string_member (sdp, "sdp", text);
  g_free (text);

  msg = json_object_new ();
  json_object_set_object_member (msg, "sdp", sdp);
  text = get_string_from_json_object (msg);
  json_object_unref (msg);

  soup_websocket_connection_send_text (channel->conn, text);
  g_free (text);
}

static void print_element_state(GstElement *element, gchar *message)
{
  GstState state;
  gchar *state_name;

  gst_element_get_state (element, &state, NULL, GST_CLOCK_TIME_NONE);
  if (state == 0) {
    state_name = "VOID_PENDING";
  } else if (state == 1) {
    state_name = "NULL";
  } else if (state == 2) {
    state_name = "READY";
  } else if (state == 3) {
    state_name = "PAUSED";
  } else if (state == 4) {
    state_name = "PLAYING";
  }
  g_print("%s - [%s]\n", message, state_name);
}

static void
data_channel_on_error (GObject * dc, gpointer user_data)
{
  cleanup_and_quit_loop ("Data channel error", (Channel *) user_data, 0);
}

static void
data_channel_on_open (GObject * dc, gpointer user_data)
{
  GBytes *bytes = g_bytes_new ("data", strlen("data"));
  g_print ("data channel opened\n");
  g_signal_emit_by_name (dc, "send-string", "Hi! from GStreamer");
  g_signal_emit_by_name (dc, "send-data", bytes);
  g_bytes_unref (bytes);
}

static void
data_channel_on_close (GObject * dc, gpointer user_data)
{
  cleanup_and_quit_loop ("Data channel closed", (Channel *) user_data, 0);
}

static void
data_channel_on_message_string (GObject * dc, gchar *str, gpointer user_data)
{
  Channel *data_channel = (Channel *) user_data;
  g_print ("Received data channel message: %s\n", str);
  if(!g_strcmp0(str, "start"))
  {
    g_print ("'Start' was entered\n");
    Channel *video_channel = malloc(sizeof(Channel));
    next_id++;
    g_print ("Next peer id %d\n", next_id);
    char id_string[16];
    sprintf(id_string, "%d", next_id); 
    video_channel->peer_id = id_string;
    VideoChannel *video = malloc(sizeof(VideoChannel));
    video->login = camera_login;
    video->password = camera_password;
    video->location = camera_location;
    video_channel->video = video;
    connect_to_websocket_server_async (video_channel);
  }
  else if(!g_strcmp0(str, "stop"))
  {
    g_print ("'Stop' was entered\n");
  }
}

static void
connect_data_channel_signals (GObject * data_channel, gpointer user_data)
{
  g_signal_connect (data_channel, "on-error", G_CALLBACK (data_channel_on_error),
      user_data);
  g_signal_connect (data_channel, "on-open", G_CALLBACK (data_channel_on_open),
      NULL);
  g_signal_connect (data_channel, "on-close", G_CALLBACK (data_channel_on_close),
      user_data);
  g_signal_connect (data_channel, "on-message-string", G_CALLBACK (data_channel_on_message_string),
      user_data);
}

static void
on_data_channel (GstElement * webrtc, GObject * data_channel, gpointer user_data)
{
  connect_data_channel_signals (data_channel, user_data);
  receive_channel = data_channel;
}

/* Offer created by our pipeline, to be sent to the peer */
static void
on_offer_created (GstPromise * promise, gpointer user_data)
{
  Channel *channel = (Channel *)user_data;
  g_signal_connect (webrtc1, "on-data-channel", G_CALLBACK (on_data_channel),
      user_data);
  /* Incoming streams will be exposed via this signal */
  /* g_signal_connect (webrtc1, "pad-added", G_CALLBACK (on_incoming_stream), pipe1); */
  /* Lifetime is the same as the pipeline itself */
  gst_object_unref (webrtc1);
  GstWebRTCSessionDescription *offer = NULL;
  const GstStructure *reply;
  g_print("on_offer_created\n");

  g_assert_cmphex (channel->state, ==, PEER_CALL_NEGOTIATING);

  g_assert_cmphex (gst_promise_wait(promise), ==, GST_PROMISE_RESULT_REPLIED);
  reply = gst_promise_get_reply (promise);
  gst_structure_get (reply, "offer",
      GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, NULL);
  gst_promise_unref (promise);

  promise = gst_promise_new ();
  g_signal_emit_by_name (webrtc1, "set-local-description", offer, promise);
  gst_promise_interrupt (promise);
  gst_promise_unref (promise);

  /* Send offer to peer */
  send_sdp_offer (offer, channel);
  gst_webrtc_session_description_free (offer);
}

static void
on_negotiation_needed (GstElement * element, gpointer user_data)
{
  GstPromise *promise;
  Channel *channel = (Channel *)user_data;

  g_signal_emit_by_name (webrtc1, "create-data-channel", "channel", NULL,
      &send_channel);

  if (send_channel) {
    g_print ("Created data channel\n");
    connect_data_channel_signals (send_channel, user_data);
  } else {
    g_print ("Could not create data channel, is usrsctp available?\n");
  }

  g_print("on_negotiation_needed\n");
  channel->state = PEER_CALL_NEGOTIATING;

  promise = gst_promise_new_with_change_func (on_offer_created, user_data, NULL);;

  g_print("promise\n");
  //слишком рано
  g_signal_emit_by_name (webrtc1, "create-offer", NULL, promise);
}

#define STUN_SERVER "stun://stun.l.google.com:19302"

static gboolean
data_channel_call (GstBus *bus,
          GstMessage *msg,
          gpointer data)
{
  return TRUE;
}

static GstElement *
create_data_channel_pipeline ()
{
  //GMainLoop *loop;

  GstElement *pipeline, *webrtcbin;
  //GstBus *bus;
  //guint bus_watch_id;

  pipeline  = gst_pipeline_new ("data-channel-pipeline");
  webrtcbin = gst_element_factory_make ("webrtcbin", "webrtc-data-channel"); 

  if (!pipeline || !webrtcbin) {
    g_printerr ("One element could not be created\n");
  }

  g_object_set (G_OBJECT (webrtcbin), "stun-server", STUN_SERVER, "bundle-policy", GST_WEBRTC_BUNDLE_POLICY_MAX_BUNDLE, NULL);

  //bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  //bus_watch_id = gst_bus_add_watch (bus, data_channel_call, loop);
  //gst_object_unref (bus);

  gst_bin_add_many (GST_BIN (pipeline), webrtcbin, NULL);

  //gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  return pipeline;
}

static gboolean
start_pipeline (Channel *channel)
{
  GstStateChangeReturn ret;
  GError *error = NULL;

  g_print("CHECK NULL\n");
  if(channel->video == NULL) {
    g_print("VIDEO IS NULL\n");
    pipe1 = create_data_channel_pipeline();
  } else {
    gchar command[512];
    VideoChannel video_channel = *channel->video;

    g_snprintf(command, 512,
            "rtspsrc user-id=%s user-pw=%s location=%s"
            " ! rtph264depay ! rtph264pay config-interval=10"
            " ! application/x-rtp,media=video,encoding-name=H264,payload=96 ! webrtcbin bundle-policy=max-bundle name=webrtc-data-channel stun-server=" STUN_SERVER,
            video_channel.login, video_channel.password, video_channel.location);

    g_print("Pipeline:\n%s\n", command);

    pipe1 = gst_parse_launch (command, &error);
  }

  if (error) {
    g_printerr ("Failed to parse launch: %s\n", error->message);
    g_error_free (error);
    goto err;
  }

  webrtc1 = gst_bin_get_by_name (GST_BIN (pipe1), "webrtc-data-channel");
  g_assert_nonnull (webrtc1);

  /* This is the gstwebrtc entry point where we create the offer and so on. It
   * will be called when the pipeline goes to PLAYING. */
  g_signal_connect (webrtc1, "on-negotiation-needed",
      G_CALLBACK (on_negotiation_needed), channel);

  /* We need to transmit this ICE candidate to the browser via the websockets
   * signalling server. Incoming ice candidates from the browser need to be
   * added by us too, see on_server_message() */ g_signal_connect (webrtc1, "on-ice-candidate", 
      G_CALLBACK (send_ice_candidate_message), channel);

  gst_element_set_state (pipe1, GST_STATE_READY);

  g_print ("Starting pipeline\n");

  ret = gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto err;

  return TRUE;

err:
  if (pipe1)
    g_clear_object (&pipe1);
  if (webrtc1)
    webrtc1 = NULL;
  return FALSE;
}

static gboolean
setup_call (Channel *channel)
{
  gchar *msg;

  if (soup_websocket_connection_get_state (channel->conn) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return FALSE;

  if (!channel->peer_id)
    return FALSE;

  g_print ("Setting up signalling server call with %s\n", channel->peer_id);
  channel->state = PEER_CONNECTING;
  msg = g_strdup_printf ("SESSION %s", channel->peer_id);
  soup_websocket_connection_send_text (channel->conn, msg);
  g_free (msg);
  return TRUE;
}

static gboolean
register_with_server (Channel *channel)
{
  gchar *hello;
  gint32 our_id;

  if (soup_websocket_connection_get_state (channel->conn) !=
      SOUP_WEBSOCKET_STATE_OPEN)
    return FALSE;

  our_id = g_random_int_range (10, 10000);
  g_print ("Registering id %i with server\n", our_id);
  channel->state = SERVER_REGISTERING;

  /* Register with the server with a random integer id. Reply will be received
   * by on_server_message() */
  hello = g_strdup_printf ("HELLO %i", our_id);
  soup_websocket_connection_send_text (channel->conn, hello);
  g_free (hello);

  return TRUE;
}

static void
on_server_closed (SoupWebsocketConnection * conn G_GNUC_UNUSED,
    gpointer user_data G_GNUC_UNUSED)
{
  Channel *channel = (Channel *) user_data;
  channel->state = SERVER_CLOSED;
  cleanup_and_quit_loop ("Server connection closed", channel, 0);
}

/* One mega message handler for our asynchronous calling mechanism */
static void
on_server_message (SoupWebsocketConnection * conn, SoupWebsocketDataType type,
    GBytes * message, gpointer user_data)
{
  Channel *channel = (Channel *)user_data;
  gchar *text;

  switch (type) {
    case SOUP_WEBSOCKET_DATA_BINARY:
      g_printerr ("Received unknown binary message, ignoring\n");
      return;
    case SOUP_WEBSOCKET_DATA_TEXT: {
      gsize size;
      const gchar *data = g_bytes_get_data (message, &size);
      /* Convert to NULL-terminated string */
      text = g_strndup (data, size);
      break;
    }
    default:
      g_assert_not_reached ();
  }

  /* Server has accepted our registration, we are ready to send commands */
  if (g_strcmp0 (text, "HELLO") == 0) {
    if (channel->state != SERVER_REGISTERING) {
      cleanup_and_quit_loop ("ERROR: Received HELLO when not registering",
          channel, APP_STATE_ERROR);
      goto out;
    }
    channel->state = SERVER_REGISTERED;
    /* Ask signalling server to connect us with a specific peer */
    if (!setup_call (channel)) {
      cleanup_and_quit_loop ("ERROR: Failed to setup call", channel, PEER_CALL_ERROR);
      goto out;
    }
  /* Call has been setup by the server, now we can start negotiation */
  } else if (g_strcmp0 (text, "SESSION_OK") == 0) {
    if (channel->state != PEER_CONNECTING) {
      cleanup_and_quit_loop ("ERROR: Received SESSION_OK when not calling",
          channel, PEER_CONNECTION_ERROR);
      goto out;
    }

    channel->state = PEER_CONNECTED;
    /* Start negotiation (exchange SDP and ICE candidates) */
    if (!start_pipeline (channel))
      cleanup_and_quit_loop ("ERROR: failed to start pipeline",
          channel, PEER_CALL_ERROR);
  /* Handle errors */
  } else if (g_str_has_prefix (text, "ERROR")) {
    switch (channel->state) {
      case SERVER_CONNECTING:
        channel->state = SERVER_CONNECTION_ERROR;
        break;
      case SERVER_REGISTERING:
        channel->state = SERVER_REGISTRATION_ERROR;
        break;
      case PEER_CONNECTING:
        channel->state = PEER_CONNECTION_ERROR;
        break;
      case PEER_CONNECTED:
      case PEER_CALL_NEGOTIATING:
        channel->state = PEER_CALL_ERROR;
      default:
        channel->state = APP_STATE_ERROR;
    }
    cleanup_and_quit_loop (text, channel, 0);
  /* Look for JSON messages containing SDP and ICE candidates */
  } else {
    JsonNode *root;
    JsonObject *object, *child;
    JsonParser *parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, text, -1, NULL)) {
      g_printerr ("Unknown message '%s', ignoring", text);
      g_object_unref (parser);
      goto out;
    }

    root = json_parser_get_root (parser);
    if (!JSON_NODE_HOLDS_OBJECT (root)) {
      g_printerr ("Unknown json message '%s', ignoring", text);
      g_object_unref (parser);
      goto out;
    }

    object = json_node_get_object (root);
    /* Check type of JSON message */
    if (json_object_has_member (object, "sdp")) {
      g_print("Receive SDP");
      int ret;
      GstSDPMessage *sdp;
      const gchar *text, *sdptype;
      GstWebRTCSessionDescription *answer;

      g_assert_cmphex (channel->state, ==, PEER_CALL_NEGOTIATING);

      child = json_object_get_object_member (object, "sdp");

      if (!json_object_has_member (child, "type")) {
        cleanup_and_quit_loop ("ERROR: received SDP without 'type'",
            channel, PEER_CALL_ERROR);
        goto out;
      }

      sdptype = json_object_get_string_member (child, "type");
      /* In this example, we always create the offer and receive one answer.
       * See tests/examples/webrtcbidirectional.c in gst-plugins-bad for how to
       * handle offers from peers and reply with answers using webrtcbin. */
      g_assert_cmpstr (sdptype, ==, "answer");

      text = json_object_get_string_member (child, "sdp");

      g_print ("Received answer:\n%s\n", text);

      ret = gst_sdp_message_new (&sdp);
      g_assert_cmphex (ret, ==, GST_SDP_OK);

      ret = gst_sdp_message_parse_buffer ((guint8 *) text, strlen (text), sdp);
      g_assert_cmphex (ret, ==, GST_SDP_OK);

      answer = gst_webrtc_session_description_new (GST_WEBRTC_SDP_TYPE_ANSWER,
          sdp);
      g_assert_nonnull (answer);

      /* Set remote description on our pipeline */
      {
        GstPromise *promise = gst_promise_new ();
        g_signal_emit_by_name (webrtc1, "set-remote-description", answer,
            promise);
        gst_promise_interrupt (promise);
        gst_promise_unref (promise);
      }

      channel->state = PEER_CALL_STARTED;
    } else if (json_object_has_member (object, "ice")) {
      const gchar *candidate;
      gint sdpmlineindex;

      child = json_object_get_object_member (object, "ice");
      candidate = json_object_get_string_member (child, "candidate");
      sdpmlineindex = json_object_get_int_member (child, "sdpMLineIndex");

      /* Add ice candidate sent by remote peer */
      g_signal_emit_by_name (webrtc1, "add-ice-candidate", sdpmlineindex,
          candidate);
    } else {
      g_printerr ("Ignoring unknown JSON message:\n%s\n", text);
    }
    g_object_unref (parser);
  }

out:
  g_free (text);
}

static void
on_server_connected (SoupSession * session, GAsyncResult * res, Channel *channel) {
  GError *error = NULL;

  channel->conn = soup_session_websocket_connect_finish (session, res, &error);
  if (error) {
    cleanup_and_quit_loop (error->message, channel, SERVER_CONNECTION_ERROR);
    g_error_free (error);
    return;
  }

  g_assert_nonnull (channel->conn);

  channel->state = SERVER_CONNECTED;
  g_print ("Connected to signalling server\n");

  g_signal_connect (channel->conn, "closed", G_CALLBACK (on_server_closed), channel);
  g_signal_connect (channel->conn, "message", G_CALLBACK (on_server_message), channel);

  /* Register with the server so it knows about us and can accept commands */
  register_with_server (channel);
}

/*
 * Connect to the signalling server. This is the entrypoint for everything else.
 */
static void
connect_to_websocket_server_async (Channel *channel)
{
  SoupLogger *logger;
  SoupMessage *message;
  SoupSession *session;
  const char *https_aliases[] = {"wss", NULL};

  session = soup_session_new_with_options (SOUP_SESSION_SSL_STRICT, !disable_ssl,
      SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
      //SOUP_SESSION_SSL_CA_FILE, "/etc/ssl/certs/ca-bundle.crt",
      SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);

  logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
  soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
  g_object_unref (logger);

  message = soup_message_new (SOUP_METHOD_GET, server_url);

  g_print ("Connecting to server...\n");

  /* Once connected, we will register */
  soup_session_websocket_connect_async (session, message, NULL, NULL, NULL,
      (GAsyncReadyCallback) on_server_connected, channel);
  channel->state = SERVER_CONNECTING;
}

static gboolean
check_plugins (void)
{
  int i;
  gboolean ret;
  GstPlugin *plugin;
  GstRegistry *registry;
  const gchar *needed[] = { "vpx", "nice", "webrtc", "dtls", "srtp", "rtpmanager", NULL};

  registry = gst_registry_get ();
  ret = TRUE;
  for (i = 0; i < g_strv_length ((gchar **) needed); i++) {
    plugin = gst_registry_find_plugin (registry, needed[i]);
    if (!plugin) {
      g_print ("Required gstreamer plugin '%s' not found\n", needed[i]);
      ret = FALSE;
      continue;
    }
    gst_object_unref (plugin);
  }
  return ret;
}

int
main (int argc, char *argv[])
{
  GOptionContext *context;
  GError *error = NULL;

  context = g_option_context_new ("- gstreamer webrtc sendrecv demo");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_add_group (context, gst_init_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("Error initializing: %s\n", error->message);
    return -1;
  }

  if (!check_plugins ())
    return -1;

  if (!peer_id) {
    g_printerr ("--peer-id is a required argument\n");
    return -1;
  }

  /* Disable ssl when running a localhost server, because
   * it's probably a test server with a self-signed certificate */
  {
    GstUri *uri = gst_uri_from_string (server_url);
    if (g_strcmp0 ("localhost", gst_uri_get_host (uri)) == 0 ||
        g_strcmp0 ("127.0.0.1", gst_uri_get_host (uri)) == 0)
      disable_ssl = TRUE;
    gst_uri_unref (uri);
  }

  loop = g_main_loop_new (NULL, FALSE);

  Channel channel;
  channel.peer_id = peer_id;
  next_id = atoi(peer_id);
  channel.video = NULL;

  connect_to_websocket_server_async (&channel);

  g_main_loop_run (loop);
  g_main_loop_unref (loop);

  if (pipe1) {
    gst_element_set_state (GST_ELEMENT (pipe1), GST_STATE_NULL);
    g_print ("Pipeline stopped\n");
    gst_object_unref (pipe1);
  }

  return 0;
}
