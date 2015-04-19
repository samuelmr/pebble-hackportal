var initialized = false;
var config = [];
var messageQueue = [];
// tuple keys 0 - 5 are reserved (see settings), 
// the rest are hack times for a portal
var FIRST_HACK = 6;
var hack_separator = '|';
var errorCount = 0;
// how many times each message is retried
var MAX_ERRORS = 5;

Pebble.addEventListener("ready", function(e) {
  var storedConfig = localStorage.getItem("config");
  if (storedConfig) {
    console.log("Found stored config: " + storedConfig);
    if (storedConfig.charAt(0) == "[") {
      config = JSON.parse(storedConfig);
    }
  }
  console.log("JavaScript app ready and running!");
  sendConfig(config);
  initialized = true;
});

Pebble.addEventListener("showConfiguration",
  function() {
    var uri = "http://samuelmr.github.io/pebble-hackportal/configure.html#" +
              encodeURIComponent(JSON.stringify(config));
    console.log("Configuration url: " + uri);
    Pebble.openURL(uri);
  }
);

Pebble.addEventListener("webviewclosed",
  function(e) {
    var webConfig = decodeURIComponent(e.response);
    console.log("Webview window returned: " + webConfig);
    if (webConfig.charAt(0) == "[") {
      localStorage.setItem("config", webConfig);
      config = JSON.parse(webConfig);
      sendConfig(config);
    }
  }
);

Pebble.addEventListener("appmessage",
  function(e) {
    if (e && e.payload) {
      var index = e.payload.index;
      var port = config[index];
      if (!port) {
        return false;
      }
      port.hacks_done = e.payload.hacks_done;
      var hacked = [];
      for (var i=0; i<port.hacks_done; i++) {
        var key = (FIRST_HACK + i).toString();
        hacked.push(e.payload[key]);
      }
      port.hacktimes = hacked.join(hack_separator);
      console.log("Got hacks for portal " + index + ": " + port.hacktimes);
      config[index] = port;
      localStorage.setItem("config", JSON.stringify(config));
    }
    else {
      console.log("No payload in message from watch!");  
    }
  }
);

function sendConfig(config) {
  for (var i=0; i<config.length; i++) {
    var hacktimes = [];
    if (config[i].hacktimes && (config[i].hacktimes.length > 0)) {
      hacktimes = config[i].hacktimes.split(hack_separator); 
      console.log("Found " + hacktimes.length + " hack times from " + config[i].hacktimes);
    }
    var msg = {portals: config.length,
               index: i,
               name: config[i].name,
               cooldown: parseInt(config[i].cooldown),
               hacks: parseInt(config[i].hacks),
               hacks_done: hacktimes.length};
    for (var j=0; j<hacktimes.length; j++) {
      var key = (FIRST_HACK + j).toString();
      msg[key] = parseInt(hacktimes[j]);
    }
    messageQueue.push(msg);
  }
  sendNextMessage();
}

function sendNextMessage() {
  if (messageQueue.length > 0) {
    Pebble.sendAppMessage(messageQueue[0], appMessageAck, appMessageNack);
    console.log("Sent message to Pebble! " + messageQueue.length + ': ' + JSON.stringify(messageQueue[0]));
  }
}

function appMessageAck(e) {
  console.log("Message accepted by Pebble!");
  messageQueue.shift();
  sendNextMessage();
}

function appMessageNack(e) {
  console.log("Message rejected by Pebble! " + e.error);
  if (e && e.data && e.data.transactionId) {
    console.log("Rejected message id: " + e.data.transactionId);
  }
  if (errorCount >= MAX_ERRORS) {
    messageQueue.shift();
  }
  else {
    errorCount++;
    console.log("Retrying, " + errorCount);
  }
  sendNextMessage();
}
