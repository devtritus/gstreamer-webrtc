var fs = require('fs');
var http = require('http');
var https = require('https');
var privateKey = fs.readFileSync('../signalling/key.pem', 'utf8');
var certificate = fs.readFileSync('../signalling/cert.pem', 'utf8');

var credentials = { key: privateKey, cert: certificate };
var express = require('express');

var app = express();

app.use(express.static('./'));

var httpServer = http.createServer(app);
var httpsServer = https.createServer(credentials, app);

app.get('/', function(req, res) {
  res.sendfile('index.html');
});

httpServer.listen(8080);
httpsServer.listen(7443);
console.log("Server was started");
