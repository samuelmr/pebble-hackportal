var initialized = false;
var config = [{"name": "No Name", "cooldown": 300, "hacktimes": [], "hacks": 4}];
var messageQueue = [];
// tuple keys 0 - 5 are reserved (see settings), 
// the rest are hack times for a portal
var FIRST_HACK = 6;
var hack_separator = '|';
var errorCount = 0;
// how many times each message is retried
var MAX_ERRORS = 5;
var timelineToken;
var lastKnownTime = 0;
var hoursBefore = 2; // first sojourner alert
var minsBefore = 15; // last sojourner alert

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
  if (Pebble.getTimelineToken) {
    Pebble.getTimelineToken(
      function (token) {
        console.log('Got timeline token ' + token);
        timelineToken = token;
      },
      function (error) { 
        console.log('Error getting timeline token: ' + error);
      }
    );    
  }
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
    console.log('Got message!' + e.payload.index);
    if (e && e.payload) {
      var index = e.payload.index;
      var port = config[index];
      if (!port) {
        console.log('Got hacks for unconfigured portal!');
        return false;
      }
      port.hacks_done = e.payload.hacks_done;
      var hacked = [];
      for (var i=0; i<port.hacks_done; i++) {
        var key = (FIRST_HACK + i).toString();
        hacked.push(e.payload[key]);
        console.log('Hack ' + i + '/' + port.hacks_done + ': ' + e.payload[key]);
        if (timelineToken) {
          pushHackPin(port.name, e.payload[key]);
          console.log('Pushing pin');
          if (e.payload[key] > lastKnownTime) {
            lastKnownTime = e.payload[key];
          }
        }
      }
      port.hacktimes = hacked.join(hack_separator);
      console.log("Got hacks for portal " + index + ": " + port.hacktimes);
      config[index] = port;
      localStorage.setItem("config", JSON.stringify(config));
      if (timelineToken && lastKnownTime) {
        console.log('Pushing Sojourner pin');
        pushSojournerPin(lastKnownTime);
      }
    }
    else {
      console.log("No payload in message from watch!");  
    }
  }
);

function pushHackPin(name, time) {
  // var id = new Date().getTime().toString();
  var id = name + '-' + time;
  var pin = {
    "id": id,
    "time": UTCDate(time),
    "layout": {
      "type": "genericPin",
      "title": "Hacked " + name,
      "tinyIcon": "system://images/TIMELINE_CALENDAR_TINY"
    }
  };
  insertUserPin(pin, function(result) {console.log('Pushed hack pin: ' + result);});
}

function pushSojournerPin(time) {
  // var id = new Date().getTime().toString();
  var id = timelineToken + '-sojourner';
  var sojodeadline = time + 24 * 60 * 60 - 1;
  var pin = {
    "id": id,
    "time": UTCDate(sojodeadline),
    "layout": {
      "type": "genericPin",
      "title": "Hack by " + readableTime(sojodeadline),
      "body": "Last known hack at " + readableTime(time),
      "tinyIcon": "system://images/TIMELINE_ALARM_TINY"
    },
    "reminders": [
      {
        "time": UTCDate(sojodeadline - hoursBefore * 60 * 60),
        "layout": {
          "type": "genericReminder",
          "title": "Hack portal by " + readableTime(sojodeadline),
          "subtitle": hoursBefore + " hours",
          "body": "Last known hack at " + readableTime(time),
          "tinyIcon": "system://images/TIMELINE_ALARM_TINY"
        }
      },
      {
        "time": UTCDate(sojodeadline - minsBefore * 60),
        "layout": {
          "type": "genericReminder",
          "title": "Hack by " + readableTime(sojodeadline),
          "subtitle": minsBefore + " minutes",
          "body": "Last known hack at " + readableTime(time),
          "tinyIcon": "system://images/TIMELINE_ALARM_TINY"
        }
      }
    ]           
  };
  insertUserPin(pin, function(result) {console.log('Pushed sojo pin: ' + result);});
}

function UTCDate(time) {
  console.log('Timezone offset: ' + new Date(time*1000).getTimezoneOffset());
  var offset = new Date(time*1000).getTimezoneOffset() * 60;
  return new Date((time+offset)*1000).toISOString();
/*
  var d = new Date(time*1000);
  var y = d.getUTCFullYear();
  var m = d.getUTCMonth() + 1;
  var a = d.getUTCDate();
  var h = d.getUTCHours();
  var i = d.getUTCMinutes();
  var s = d.getUTCSeconds();
  var str = y + '-' +
      ((m < 10) ? '0' : '') + m + '-' +
      ((a < 10) ? '0' : '') + a + 'T' +
      ((h < 10) ? '0' : '') + h + ':' +
      ((i < 10) ? '0' : '') + i + ':' +
      ((s < 10) ? '0' : '') + s + 'Z';
  console.log('Converted ' + time + ' into ' + str);
  return str;
*/
}

function readableTime(time) {
  var d = new Date(time*1000);
  var h = d.getHours();
  var i = d.getMinutes();
  var s = d.getSeconds();
  var str = h + ':' +
      ((i < 10) ? '0' : '') + i + ':' +
      ((s < 10) ? '0' : '') + s;
  console.log('Converted ' + time + ' into ' + str);
  return str;
}

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

/******************************* timeline lib *********************************/

// The timeline public URL root
var API_URL_ROOT = 'https://timeline-api.getpebble.com/';

/**
 * Send a request to the Pebble public web timeline API.
 * @param pin The JSON pin to insert. Must contain 'id' field.
 * @param type The type of request, either PUT or DELETE.
 * @param callback The callback to receive the responseText after the request has completed.
 */
function timelineRequest(pin, type, callback) {
  // User or shared?
  var url = API_URL_ROOT + 'v1/user/pins/' + encodeURIComponent(pin.id);

  // Create XHR
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    console.log('timeline: response received: ' + this.responseText);
    callback(this.responseText);
  };
  xhr.open(type, url);
  // lib modified here (token already known)
  xhr.setRequestHeader('Content-Type', 'application/json');
  xhr.setRequestHeader('X-User-Token', '' + timelineToken);
  console.log('Sending pin to ' + url);
  console.log('Pin: ' + JSON.stringify(pin));
  xhr.send(JSON.stringify(pin));
  console.log('timeline: request sent.');
}

/**
 * Insert a pin into the timeline for this user.
 * @param pin The JSON pin to insert.
 * @param callback The callback to receive the responseText after the request has completed.
 */
function insertUserPin(pin, callback) {
  timelineRequest(pin, 'PUT', callback);
}

/**
 * Delete a pin from the timeline for this user.
 * @param pin The JSON pin to delete.
 * @param callback The callback to receive the responseText after the request has completed.
 */
function deleteUserPin(pin, callback) {
  timelineRequest(pin, 'DELETE', callback);
}

/***************************** end timeline lib *******************************/