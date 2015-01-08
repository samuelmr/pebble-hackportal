var initialized = false;
var config = {};
// var setPebbleToken = "YJHH";
var messageQueue = [];

Pebble.addEventListener("ready", function(e) {
  config = JSON.parse(localStorage.getItem("config"));
  initialized = true;
  console.log("JavaScript app ready and running! " + e.ready);
  sendConfig(config);
});

Pebble.addEventListener("showConfiguration",
  function() {
    // var uri = "http://x.setpebble.com/" + setPebbleToken + "/" + Pebble.getAccountToken();
    var uri = "https://rawgithub.com/samuelmr/pebble-hackportal/master/configure.html#" +
              JSON.stringify(config);
    console.log("Configuration url: " + uri);
    Pebble.openURL(uri);
  }
);

Pebble.addEventListener("webviewclosed",
  function(e) {
    var webconfig = decodeURIComponent(e.response);
    console.log("Webview window returned: " + webconfig);
    localStorage.setItem("config", webconfig);
    config = JSON.parse(webconfig);
    sendConfig(config);
  }
);

function sendConfig(config) {
  for (var i=0; i<config.length; i++) {
    var msg = {"0": i,
               "1": config[i].name,
               "2": parseInt(config[i].cooldown),
               "3": parseInt(config[i].hacks),
               "4": config.length};
    messageQueue.push(msg);
  }
  sendNextMessage();
}

function sendNextMessage() {
  if (messageQueue.length > 0) {
    Pebble.sendAppMessage(messageQueue[0], appMessageAck, appMessageNack);
    console.log("Sent message to Pebble! " + JSON.stringify(messageQueue[0]));
  }
}

function appMessageAck(e) {
  console.log("Message accepted by Pebble!");
  messageQueue.shift();
  sendNextMessage();
}

function appMessageNack(e) {
  console.log("Message rejected by Pebble! " + e.error);
  // try again
  sendNextMessage();
}
