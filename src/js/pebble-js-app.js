
var initialized = false;

var config = {};
// var setPebbleToken = "YJHH";

Pebble.addEventListener("ready", function(e) {
  config = JSON.parse(localStorage.getItem("config"));
  initialized = true;
  console.log("JavaScript app ready and running! " + e.ready);
});

Pebble.addEventListener("showConfiguration",
  function() {
    // var uri = "http://x.setpebble.com/" + setPebbleToken + "/" + Pebble.getAccountToken();
    var uri = "https://rawgithub.com/samuelmr/pebble-hackportal/master/configure.html";
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
    for (var i=0; i<config.length; i++) {
      var msg = {"0": i, "1": config[i].name, "2": config[i].cooldown, "3": config[i].hacks};
      sendMessage(msg);
    }
  }
);

function sendMessage(dict) {
  Pebble.sendAppMessage(dict, appMessageAck, appMessageNack);
  console.log("Sent message to Pebble! " + JSON.stringify(dict));
}

function appMessageAck(e) {
  console.log("Message accepted by Pebble!");
}

function appMessageNack(e) {
  console.log("Message rejected by Pebble! " + e.error);
}
