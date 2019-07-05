// Set this to override the automatic detection in websocketServerConnect()
var ws_server;
var ws_port;
// Override with your own STUN servers if you want
var rtc_configuration = {iceServers: [{urls: "stun:stun.services.mozilla.com"},
                                      {urls: "stun:stun.l.google.com:19302"}]};
var connect_attempts = 0;
var send_channels = [];
var connections = [];
var data_connections = [];

var id = 0;

function getDataChannel() {
  return send_channels[0];
}

function getOurId() {
    return ++id;
}

function resetState(index) {
    // This will call onServerClose()
    connections[index].close();
}

function handleIncomingError(error, index) {
    setError("ERROR: " + error);
    resetState(index);
}

function getVideoElement(index) {
    return document.getElementById("stream_" + index);
}

function createVideoElement(index) {
    const videoTag = "<video id='stream_"+ index + "' style='width:80%' autoplay playsinline>Your browser doesn't support video</video>";
    var template = document.createElement('template');
    var html = videoTag.trim(); // Never return a text node of whitespace as the result
    template.innerHTML = html;
    document.getElementById("video-container").appendChild(template.content.firstChild);
}

function setStatus(text) {
    console.log(text);
    var span = document.getElementById("status")
    // Don't set the status if it already contains an error
    if (!span.classList.contains('error'))
        span.textContent = text;
}

function setError(text) {
    console.error(text);
    var span = document.getElementById("status")
    span.textContent = text;
    span.classList.add('error');
}

function resetVideo(index) {
    // Reset the video element and stop showing the last received frame
    var videoElement = getVideoElement(index);
    if(!videoElement) {
      return;
    }
    videoElement.pause();
    videoElement.src = "";
    videoElement.load();
}

// SDP offer received from peer, set remote description and create an answer
function onIncomingSDP(sdp, index) {
    let data_connection = data_connections[index];
    data_connection.setRemoteDescription(sdp).then(() => {
        setStatus("Remote SDP set");
        if (sdp.type != "offer")
            return;
        setStatus("Got SDP offer");
        data_connection.createAnswer().then(desc => onLocalDescription(desc, index)).catch(setError);
    }).catch(setError);
}

// Local description was set, send it to peer
function onLocalDescription(desc, index) {
    let data_connection = data_connections[index];
    let ws_conn = connections[index];
    console.log("Got local description: " + JSON.stringify(desc));
    data_connection.setLocalDescription(desc).then(() => {
        setStatus("Sending SDP answer");
        sdp = {'sdp': data_connection.localDescription}
        ws_conn.send(JSON.stringify(sdp));
    });
}

// ICE candidate received from peer, add it to the peer connection
function onIncomingICE(ice, data_connection) {
    var candidate = new RTCIceCandidate(ice);
    data_connection.addIceCandidate(candidate).catch(setError);
}

function onServerMessage(event) {
    console.log("Received " + event.data);
    let currentId = id - 1;
    let data_connection = data_connections[currentId];
    switch (event.data) {
        case "HELLO":
            setStatus("Registered with server, waiting for call");
            return;
        default:
            if (event.data.startsWith("ERROR")) {
                handleIncomingError(event.data, currentId);
                return;
            }
            // Handle incoming JSON SDP and ICE messages
            try {
                msg = JSON.parse(event.data);
            } catch (e) {
                if (e instanceof SyntaxError) {
                    handleIncomingError("Error parsing incoming JSON: " + event.data);
                } else {
                    handleIncomingError("Unknown error parsing response: " + event.data);
                }
                return;
            }
            
            // Incoming JSON signals the beginning of a call
            if (!data_connection) {
                data_connection = createCall(msg, currentId);
                data_connections[currentId] = data_connection;
            }

            if (msg.sdp != null) {
                onIncomingSDP(msg.sdp, currentId);
                window.setTimeout(websocketServerConnect, 1000);
            } else if (msg.ice != null) {
                onIncomingICE(msg.ice, data_connection);
            } else {
                handleIncomingError("Unknown incoming JSON: " + msg);
            }
    }
}

function onServerClose(event, index) {
    setStatus('Disconnected from server');
    resetVideo(index);
    let data_channel = getDataChannel();

    if (data_channel) {
        data_channel.close();
        data_channel = null;
    }

    // Reset after a second
    window.setTimeout(websocketServerConnect, 1000);
}

function onServerError(event) {
    setError("Unable to connect to server, did you add an exception for the certificate?")
    // Retry after 3 seconds
    window.setTimeout(websocketServerConnect, 3000);
}

function websocketServerConnect() {
    connect_attempts++;
    if (connect_attempts > 3) {
        setError("Too many connection attempts, aborting. Refresh page to try again");
        return;
    }
    // Clear errors in the status span
    var span = document.getElementById("status");
    span.classList.remove('error');
    span.textContent = '';
    // Fetch the peer id to use
    peer_id = getOurId();
    ws_port = ws_port || '8443';
    if (window.location.protocol.startsWith ("file")) {
        ws_server = ws_server || "127.0.0.1";
    } else if (window.location.protocol.startsWith ("http")) {
        ws_server = ws_server || window.location.hostname;
    } else {
        throw new Error ("Don't know how to connect to the signalling server with uri" + window.location);
    }
    var ws_url = 'wss://' + ws_server + ':' + ws_port
    setStatus("Connecting to server " + ws_url);
    let ws_conn = new WebSocket(ws_url);
    /* When connected, immediately register with the server */
    ws_conn.addEventListener('open', (event) => {
        document.getElementById("peer-id").textContent = peer_id;
        ws_conn.send('HELLO ' + peer_id);
        setStatus("Registering with server");
    });
    ws_conn.addEventListener('error', onServerError);
    ws_conn.addEventListener('message', onServerMessage);
    ws_conn.addEventListener('close', event => onServerClose(event, peer_id - 1));
    connections[peer_id - 1] = ws_conn;
}

function onRemoteTrack(event, index) {
    let elementNumber = index - 1;
    createVideoElement(elementNumber);
    let videoElement = getVideoElement(elementNumber);
    console.log("stream", event.streams[0]);
    if (videoElement.srcObject !== event.streams[0]) {
        console.log('Incoming stream');
        videoElement.srcObject = event.streams[0];
    }
}

function errorUserMediaHandler() {
    setError("Browser doesn't support getUserMedia!");
}

const handleDataChannelOpen = (event) =>{
    console.log("dataChannel.OnOpen", event);
};

const handleDataChannelMessageReceived = (event) =>{
    console.log("dataChannel.OnMessage:", event, event.data.type);

    setStatus("Received data channel message");
    if (typeof event.data === 'string' || event.data instanceof String) {
        console.log('Incoming string message: ' + event.data);
        textarea = document.getElementById("text")
        textarea.value = textarea.value + '\n' + event.data
    } else {
        console.log('Incoming data message');
    }
    getDataChannel().send("Hi! (from browser)");
};

const handleDataChannelError = (error) =>{
    console.log("dataChannel.OnError:", error);
};

const handleDataChannelClose = (event) =>{
    console.log("dataChannel.OnClose", event);
};

function onDataChannel(event) {
    setStatus("Data channel created");
    let receiveChannel = event.channel;
    receiveChannel.onopen = handleDataChannelOpen;
    receiveChannel.onmessage = handleDataChannelMessageReceived;
    receiveChannel.onerror = handleDataChannelError;
    receiveChannel.onclose = handleDataChannelClose;
}

function createCall(msg, index) {
    // Reset connection attempts because we connected successfully
    connect_attempts = 0;

    console.log('Creating RTCPeerConnection');

    let data_channel_peer_connection = new RTCPeerConnection(rtc_configuration);
    let send_channel = data_channel_peer_connection.createDataChannel('label_' + index, null);
    send_channel.onopen = handleDataChannelOpen;
    send_channel.onmessage = handleDataChannelMessageReceived;
    send_channel.onerror = handleDataChannelError;
    send_channel.onclose = handleDataChannelClose;
    data_channel_peer_connection.ondatachannel = onDataChannel;
    data_channel_peer_connection.ontrack = (event) => onRemoteTrack(event, index);

    if (!msg.sdp) {
        console.log("WARNING: First message wasn't an SDP message!?");
    }

    data_channel_peer_connection.onicecandidate = (event) => {
      // We have a candidate, send it to the remote party with the same uuid
      if (event.candidate == null) {
        console.log("ICE Candidate was null, done");
        return;
      }
      connections[index].send(JSON.stringify({'ice': event.candidate}));
    };

    setStatus("Created peer connection for call, waiting for SDP");
    send_channels[index] = send_channel;
    return data_channel_peer_connection;
}

function sendText() {
    var textChannelArea = document.getElementById('text-channel-area');
    var data = textChannelArea.value;
    console.log("Send: " + data);
    getDataChannel().send(data);
}

function start() {
    console.log("START");
    getDataChannel().send("start");
}

function stop() {
    console.log("STOP");
    getDataChannel().send("stop");
}
