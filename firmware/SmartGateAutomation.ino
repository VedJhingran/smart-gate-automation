#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include "base64.h"
#include <Espalexa.h>
#include "include/Config.h"
#include <time.h>

#define RELAY1 23
#define LED_PIN 2   // Common onboard LED pin on ESP32 Dev Module

// ---------------- FIREBASE --------------------
const char* FIREBASE_HOST = FIREBASE_DATABASE_URL;
const char* FIREBASE_AUTH = FIREBASE_DATABASE_AUTH;

WiFiClientSecure firebaseClient;
WiFiClient camClient;   // plain HTTP client for the CP Plus NVR snapshot (local network, no TLS)

// ---------------- SHARED SNAPSHOT CACHE ----------------
// One periodic fetch feeds this buffer; the webpage, Telegram, and
// Firebase all read from here instead of each hitting the camera
// independently. Many NVR snapshot CGI endpoints only handle one request
// at a time, so overlapping fetches (e.g. a doorbell press while the
// webpage is mid-refresh) could otherwise fail intermittently.
uint8_t* cachedSnapshotBuf = nullptr;
size_t cachedSnapshotLen = 0;
bool cachedSnapshotValid = false;
unsigned long lastSnapshotCacheRefresh = 0;
const unsigned long snapshotCacheInterval = 1000;   // refresh once per second

// -------------------- WIFI --------------------
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// ---------------- TELEGRAM --------------------
#define BOT_TOKEN TELEGRAM_BOT_TOKEN

String CHAT_ID_1 = TELEGRAM_CHAT_ID_PRIMARY;
String CHAT_ID_2 = TELEGRAM_CHAT_ID_SECONDARY;

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// ---------------- WEB SERVER ------------------
WebServer server(80);
Espalexa espalexa;   // declared here (not near its callback) so handleWiFi() can see it
bool serverStarted = false;   // only call server.begin() once, after first successful connect

// ---------------- STATE VARIABLES -------------
bool visitorWaiting = false;
bool gateOpen = false;

unsigned long unlockTime = 0;
const unsigned long unlockDuration = 5000;

// ---------------- TELEGRAM POLLING ----------------
unsigned long lastTelegramCheck = 0;
const unsigned long telegramInterval = 1000;   // check for button presses every 1s

// ---------------- FIREBASE POLLING ----------------
unsigned long lastFirebaseCheck = 0;
const unsigned long firebaseCommandInterval = 2000;   // check for app commands every 2s
unsigned long lastFirebaseStatusPush = 0;
const unsigned long firebaseStatusInterval = 60000;   // forced heartbeat every 60s (kept infrequent since it always writes, regardless of change - real change events push immediately via the non-forced calls elsewhere)

// ---------------- WIFI STATE ------------------
bool wasConnected = false;
unsigned long lastReconnectAttempt = 0;
unsigned long lastLedChange = 0;
bool ledState = false;

// ------------------------------------------------
// WIFI HANDLING
// ------------------------------------------------

void startWiFi()
{
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
}

void handleWiFi()
{
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected)
  {
    digitalWrite(LED_PIN, HIGH);  // LED stays ON when connected

    if (!wasConnected)
    {
      Serial.println("\nWi-Fi connected!");
      Serial.print("ESP32 IP address: ");
      Serial.println(WiFi.localIP());
      wasConnected = true;

      // Start the web server the first time we connect. Espalexa shares
      // this same server instance (rather than each starting its own on
      // port 80, which would conflict) — this call starts serving for
      // both the gate's own pages and Alexa's local discovery/control.
      if (!serverStarted)
      {
        espalexa.begin(&server);
        serverStarted = true;
        Serial.println("Web Server Started (Alexa device: \"Gate\")");

        // Advertise this device as "gatecontroller.local" on the local
        // network, so you can open the page without needing the IP.
        if (MDNS.begin("gatecontroller"))
        {
          MDNS.addService("http", "tcp", 80);
          Serial.println("mDNS responder started");
          Serial.println("Open the gate page at: http://gatecontroller.local/");
        }
        else
        {
          Serial.println("mDNS failed to start - use the IP address above instead");
        }

        // Sync real time-of-day (IST, UTC+5:30) - needed for the event
        // log timestamps and the daily open counter's date key.
        configTime(5 * 3600 + 1800, 0, "pool.ntp.org", "time.google.com");
        Serial.println("NTP time sync requested");
      }
    }
  }
  else
  {
    // Blink LED while Wi-Fi is unavailable
    if (millis() - lastLedChange >= 250)
    {
      lastLedChange = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
    }

    if (wasConnected)
    {
      Serial.println("\nWi-Fi disconnected. Waiting to reconnect...");
      wasConnected = false;
    }

    // Try again every 10 seconds
    if (millis() - lastReconnectAttempt >= 10000)
    {
      lastReconnectAttempt = millis();
      Serial.println("Trying Wi-Fi again...");
      WiFi.reconnect();
    }
  }
}

// ------------------------------------------------
// TELEGRAM NOTIFICATION (with inline buttons)
// ------------------------------------------------

void sendVisitorNotification()
{
  String msg =
      "🔔 *Visitor at Main Gate*\n\n"
      "Someone has pressed the doorbell.\n\n"
      "Choose an action.";

  String keyboard =
  "["
    "["
      "{\"text\":\"🟢 Open Gate\",\"callback_data\":\"OPEN_GATE\"}"
    "],"
    "["
      "{\"text\":\"📷 Camera\",\"callback_data\":\"CAMERA\"}"
    "],"
    "["
      "{\"text\":\"❌ Ignore\",\"callback_data\":\"IGNORE\"}"
    "]"
  "]";

  bot.sendMessageWithInlineKeyboard(
      CHAT_ID_1,
      msg,
      "Markdown",
      keyboard
  );

  bot.sendMessageWithInlineKeyboard(
      CHAT_ID_2,
      msg,
      "Markdown",
      keyboard
  );

  Serial.println("Telegram Notification Sent (with buttons)");
}

// ------------------------------------------------
// SHARED GATE-OPEN LOGIC
// (used by both the web /open route and the Telegram button)
// ------------------------------------------------

void gateOpenAction() { gateOpenAction("Unknown"); }

void gateOpenAction(const String& source)
{
  Serial.println("");
  Serial.println("================================");
  Serial.println("OPEN GATE REQUEST (" + source + ")");
  Serial.println("Relay ON");
  Serial.println("Gate Unlocked");
  Serial.println("================================");

  digitalWrite(RELAY1, LOW);

  gateOpen = true;
  visitorWaiting = false;

  unlockTime = millis();

  logEvent("Gate opened by " + source);
  incrementDailyOpenCounter();
}

// ------------------------------------------------
// ALEXA (local network device, via Espalexa)
// Emulates a Philips Hue-style on/off device so Alexa can discover it
// locally — no cloud, no AWS, no account linking. Only the on/off
// vocabulary is available this way ("Alexa, turn on gate"), not custom
// verbs like "unlock."
// ------------------------------------------------

void onGateAlexaChange(uint8_t brightness)
{
  if (brightness > 0)
  {
    // "Alexa, turn on gate" -> same action as the web button / Telegram / PWA
    Serial.println("Alexa: turn on Gate received");
    gateOpenAction("Alexa");
    pushStatusToFirebase();
  }
  // brightness == 0 ("Alexa, turn off gate") -> nothing to do; the gate
  // auto-relocks itself and there's no persistent "locked" state to set.
}

// ------------------------------------------------
// TELEGRAM CALLBACK (BUTTON PRESS) HANDLING
// ------------------------------------------------

void handleCallback(int index)
{
  String callback_data = bot.messages[index].text;      // e.g. "OPEN_GATE"
  String query_id       = bot.messages[index].query_id;
  String from_chat_id   = bot.messages[index].chat_id;

  if (callback_data == "OPEN_GATE")
  {
    gateOpenAction("Telegram");
    pushStatusToFirebase();

    bot.answerCallbackQuery(query_id, "Gate opening...");
    bot.sendMessage(from_chat_id, "🟢 Gate has been *opened*.", "Markdown");
  }
  else if (callback_data == "CAMERA")
  {
    bot.answerCallbackQuery(query_id, "Fetching camera feed...");

    bool sent = sendCameraSnapshotToTelegram(from_chat_id);
    if (!sent)
    {
      bot.sendMessage(from_chat_id, "⚠️ Could not fetch the camera snapshot right now.", "");
    }
  }
  else if (callback_data == "IGNORE")
  {
    visitorWaiting = false;
    pushStatusToFirebase();

    bot.answerCallbackQuery(query_id, "Ignored");
    bot.sendMessage(from_chat_id, "❌ Visitor request ignored.", "");
  }
  else
  {
    bot.answerCallbackQuery(query_id, "Unknown action");
  }
}

void checkTelegramMessages()
{
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  while (numNewMessages)
  {
    for (int i = 0; i < numNewMessages; i++)
    {
      if (bot.messages[i].type == "callback_query")
      {
        handleCallback(i);
      }
    }
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  }
}

// ------------------------------------------------
// FIREBASE (used by the mobile PWA)
// ------------------------------------------------

// Pushes current gate/visitor state so the app can display it live.
// Only writes to Firebase when something actually changed since the last
// push, since most of the periodic 5s calls have nothing new to report -
// this cuts down on unnecessary writes. Pass force=true to always write
// regardless (used by the periodic heartbeat, so the connection status
// still reads as "live" even during long stretches of no activity).
bool lastPushedGateOpen = false;
bool lastPushedVisitorWaiting = false;
bool firstStatusPushDone = false;

void pushStatusToFirebase() { pushStatusToFirebase(false); }

void pushStatusToFirebase(bool force)
{
  if (WiFi.status() != WL_CONNECTED) return;

  bool changed = (gateOpen != lastPushedGateOpen) || (visitorWaiting != lastPushedVisitorWaiting);

  if (!force && firstStatusPushDone && !changed)
  {
    return;   // nothing meaningful changed - skip the write
  }

  HTTPClient http;
  String url = String(FIREBASE_HOST) + "/gate/status.json?auth=" + FIREBASE_AUTH;

  http.begin(firebaseClient, url);
  http.addHeader("Content-Type", "application/json");

  String payload = String("{\"gateOpen\":") + (gateOpen ? "true" : "false") +
                    ",\"visitorWaiting\":" + (visitorWaiting ? "true" : "false") +
                    ",\"uptimeSeconds\":" + String(millis() / 1000) +
                    ",\"lastUpdate\":" + String(millis()) + "}";

  int code = http.PUT(payload);

  if (code <= 0)
  {
    Serial.print("Firebase status push failed: ");
    Serial.println(http.errorToString(code));
  }
  else
  {
    lastPushedGateOpen = gateOpen;
    lastPushedVisitorWaiting = visitorWaiting;
    firstStatusPushDone = true;
  }

  http.end();
}

// Resets /gate/command back to "NONE" after acting on it, so the same
// command doesn't get executed again on the next poll.
void clearFirebaseCommand()
{
  HTTPClient http;
  String url = String(FIREBASE_HOST) + "/gate/command.json?auth=" + FIREBASE_AUTH;

  http.begin(firebaseClient, url);
  http.addHeader("Content-Type", "application/json");
  http.PUT("\"NONE\"");
  http.end();
}

// Polls /gate/command for anything the PWA has written, acts on it,
// then clears it.
void checkFirebaseCommand()
{
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(FIREBASE_HOST) + "/gate/command.json?auth=" + FIREBASE_AUTH;

  http.begin(firebaseClient, url);
  int code = http.GET();

  if (code == 200)
  {
    String response = http.getString();
    response.trim();

    if (response == "\"OPEN_GATE\"")
    {
      Serial.println("Firebase: OPEN_GATE command received");
      gateOpenAction("App");
      clearFirebaseCommand();
      pushStatusToFirebase();
    }
    else if (response == "\"IGNORE\"")
    {
      Serial.println("Firebase: IGNORE command received");
      visitorWaiting = false;
      clearFirebaseCommand();
      pushStatusToFirebase();
    }
    else if (response == "\"SNAPSHOT\"")
    {
      Serial.println("Firebase: SNAPSHOT command received");
      clearFirebaseCommand();
      captureAndUploadSnapshotToFirebase();
    }
    // response == "null" (no command set) -> do nothing
  }
  else if (code > 0)
  {
    Serial.print("Firebase command check HTTP code: ");
    Serial.println(code);
  }

  http.end();
}

// Writes a new event record (not just a status flag) so the Cloud Function
// fires reliably even on rapid repeat presses, rather than relying on a
// boolean that might already be true.
void pushDoorbellEvent()
{
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(FIREBASE_HOST) + "/gate/events.json?auth=" + FIREBASE_AUTH;

  http.begin(firebaseClient, url);
  http.addHeader("Content-Type", "application/json");

  String payload = String("{\"timestamp\":") + String(millis()) + "}";

  int code = http.POST(payload);

  if (code <= 0)
  {
    Serial.print("Firebase doorbell event push failed: ");
    Serial.println(http.errorToString(code));
  }

  http.end();
}

// ------------------------------------------------
// TIME HELPERS (needs the NTP sync started in handleWiFi())
// ------------------------------------------------

String getTimeString()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) return "--:--";
  char buf[6];
  strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
  return String(buf);
}

String getDateString()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 1000)) return "unknown-date";
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

// ------------------------------------------------
// EVENT LOG (last 100 entries kept via the Cloud Function's trim job)
// ------------------------------------------------

void logEvent(const String& eventText)
{
  if (WiFi.status() != WL_CONNECTED) return;

  String payload = "{\"time\":\"" + getTimeString() + "\",\"event\":\"" + eventText + "\"}";

  HTTPClient http;
  String url = String(FIREBASE_HOST) + "/gate/eventLog.json?auth=" + FIREBASE_AUTH;

  http.begin(firebaseClient, url);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(payload);   // POST = new push-keyed entry, chronologically sortable

  if (code <= 0)
  {
    Serial.print("Event log push failed: ");
    Serial.println(http.errorToString(code));
  }

  http.end();
}

// ------------------------------------------------
// DAILY OPEN COUNTER
// ------------------------------------------------

void incrementDailyOpenCounter()
{
  if (WiFi.status() != WL_CONNECTED) return;

  String path = "/gate/dailyOpens/" + getDateString();
  String url = String(FIREBASE_HOST) + path + ".json?auth=" + FIREBASE_AUTH;

  // Read the current count for today
  HTTPClient getHttp;
  getHttp.begin(firebaseClient, url);
  int getCode = getHttp.GET();

  int currentCount = 0;
  if (getCode == 200)
  {
    String body = getHttp.getString();
    body.trim();
    if (body != "null") currentCount = body.toInt();
  }
  getHttp.end();

  // Write back the incremented count (safe without a transaction since
  // only the ESP32 itself ever writes this path)
  HTTPClient putHttp;
  putHttp.begin(firebaseClient, url);
  putHttp.addHeader("Content-Type", "application/json");
  putHttp.PUT(String(currentCount + 1));
  putHttp.end();
}

// ------------------------------------------------
// HTML PAGE
// ------------------------------------------------

String htmlPage()
{
  String page = R"rawliteral(
<!DOCTYPE html>
<html>

<head>

<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">

<title>Smart Gate Controller</title>

<style>

body{
    margin:0;
    font-family:Segoe UI,Arial,sans-serif;
    background:#eef2f7;
}

.container{
    max-width:600px;
    margin:auto;
    padding:20px;
}

.card{
    background:white;
    border-radius:20px;
    padding:20px;
    margin-bottom:20px;
    box-shadow:0 4px 15px rgba(0,0,0,0.15);
}

.title{
    text-align:center;
    font-size:30px;
    font-weight:bold;
    color:#1f2937;
}

.camera{
    height:220px;
    border-radius:15px;
    background:#111827;
    color:white;
    display:flex;
    align-items:center;
    justify-content:center;
    font-size:22px;
}

.status{
    display:flex;
    justify-content:space-between;
    margin-top:15px;
}

.status-box{
    width:48%;
    padding:15px;
    border-radius:15px;
    text-align:center;
    font-size:18px;
    font-weight:bold;
}

.visitor{
    background:#dbeafe;
    color:#1d4ed8;
}

.locked{
    background:#fee2e2;
    color:#b91c1c;
}

.unlocked{
    background:#dcfce7;
    color:#15803d;
}

.btn{
    width:100%;
    height:70px;
    border:none;
    border-radius:15px;
    font-size:22px;
    font-weight:bold;
    margin-top:15px;
    cursor:pointer;
}

.bell{
    background:#2563eb;
    color:white;
}

.open{
    background:#16a34a;
    color:white;
}

.footer{
    text-align:center;
    color:#6b7280;
    margin-top:15px;
}

</style>

</head>

<body>

<div class="container">

<div class="card">

<div class="title">
🏡 Smart Gate Controller
</div>

</div>

<div class="card">

<div class="camera" style="padding:0; overflow:hidden;">
<video id="camVideo" autoplay muted playsinline style="width:100%; height:100%; object-fit:cover; display:block; background:#111827;"></video>
<img id="camFeed" alt="Camera Feed" style="width:100%; height:100%; object-fit:cover; display:none;">
</div>

<div class="status">

<div class="status-box visitor">
👤 VISITOR_STATUS
</div>

<div class="status-box GATE_CLASS">
🔐 GATE_STATUS
</div>

</div>

<form action="/doorbell">
<button class="btn bell" type="submit">
🔔 Visitor At Gate
</button>
</form>

<form action="/open">
<button class="btn open" type="submit">
🟢 Open Gate
</button>
</form>

<div class="footer">
Gate automatically locks after 5 seconds
</div>

</div>

</div>

<script src="https://cdn.jsdelivr.net/npm/hls.js@1.5.13"></script>
<script>
// Tries the real live video feed first (relayed from the MediaMTX phone).
// If it doesn't start within a few seconds - phone off, MediaMTX not
// running, network hiccup - falls back automatically to the snapshot
// endpoint, so the camera box never just sits broken.
(function(){
  var video = document.getElementById('camVideo');
  var img = document.getElementById('camFeed');

  // ── REPLACE WITH YOUR PHONE'S ACTUAL LOCAL IP ──
  var streamUrl = "http://LOCAL_STREAM_HOST:8888/cam/index.m3u8";

  var usingVideo = true;
  var fallbackTimer = null;

  function startSnapshotFallback(){
    if (!usingVideo) return;   // already fell back, don't do it twice
    usingVideo = false;
    video.style.display = 'none';
    img.style.display = 'block';

    function refreshCam(){
      img.src = "/camera.jpg?ts=" + Date.now();
    }
    // Waits for each image to finish loading (or fail) before requesting
    // the next one, so slow responses don't pile up multiple requests.
    img.onload = function(){ setTimeout(refreshCam, 1000); };
    img.onerror = function(){ setTimeout(refreshCam, 3000); };
    refreshCam();
  }

  fallbackTimer = setTimeout(startSnapshotFallback, 6000);   // give the stream 6s to start

  video.addEventListener('playing', function(){
    clearTimeout(fallbackTimer);
  });
  video.addEventListener('error', startSnapshotFallback);

  if (video.canPlayType('application/vnd.apple.mpegurl')) {
    video.src = streamUrl;
  } else if (typeof Hls !== 'undefined' && Hls.isSupported()) {
    var hls = new Hls();
    hls.loadSource(streamUrl);
    hls.attachMedia(video);
    hls.on(Hls.Events.ERROR, function(event, data){
      if (data.fatal) startSnapshotFallback();
    });
  } else {
    // No HLS support at all in this browser - skip straight to snapshots
    startSnapshotFallback();
  }
})();
</script>

</body>
</html>
)rawliteral";

  page.replace(
      "VISITOR_STATUS",
      visitorWaiting ? "Visitor Waiting" : "No Visitor"
  );

  page.replace(
      "GATE_STATUS",
      gateOpen ? "Unlocked" : "Locked"
  );

  page.replace(
      "GATE_CLASS",
      gateOpen ? "unlocked" : "locked"
  );

  return page;
}

// ------------------------------------------------
// WEB HANDLERS
// ------------------------------------------------

void handleRoot()
{
  server.send(200, "text/html; charset=UTF-8", htmlPage());
}

// Shared by the web /doorbell route and the physical hardware button
// (wired in via the isolation relay) so both trigger identically.
void triggerDoorbellSequence()
{
  visitorWaiting = true;

  Serial.println("");
  Serial.println("================================");
  Serial.println("DOORBELL PRESSED");
  Serial.println("Visitor Waiting At Gate");
  Serial.println("================================");

  sendVisitorNotification();
  pushStatusToFirebase();
  pushDoorbellEvent();
  logEvent("Visitor");
  captureAndUploadSnapshotToFirebase();
}

void handleDoorbell()
{
  triggerDoorbellSequence();
  server.send(200, "text/html; charset=UTF-8", htmlPage());
}

// ------------------------------------------------
// PHYSICAL DOORBELL BUTTON (via isolation relay)
// The relay's isolated NO/COM contacts pull this pin LOW when the real
// doorbell button is pressed. Idle state is HIGH via the external 10k
// pull-up to 3.3V.
// ------------------------------------------------

#define DOORBELL_PIN 27   // relay's isolated NO contact -> here, COM -> GND; internal pull-up used, no external resistor needed

bool lastDoorbellPinState = HIGH;
unsigned long lastDoorbellTrigger = 0;
const unsigned long doorbellDebounceMs = 2000;   // ignore repeat triggers within 2s

void checkPhysicalDoorbell()
{
  bool currentState = digitalRead(DOORBELL_PIN);

  if (currentState == LOW && lastDoorbellPinState == HIGH &&
      (millis() - lastDoorbellTrigger) > doorbellDebounceMs)
  {
    lastDoorbellTrigger = millis();
    Serial.println("Physical doorbell button triggered");
    triggerDoorbellSequence();
  }

  lastDoorbellPinState = currentState;
}

void handleOpen()
{
  gateOpenAction("Web");
  pushStatusToFirebase();

  server.send(200, "text/html; charset=UTF-8", htmlPage());
}

// ------------------------------------------------
// CAMERA SNAPSHOT — SHARED FETCH HELPER
// Used by the local homepage relay, the Telegram photo upload, and the
// Firebase relay for the remote PWA.
// ------------------------------------------------

// Fetches one JPEG snapshot from the NVR into a heap buffer.
// On success, *outBuf is malloc'd and must be free()'d by the caller.
bool fetchCameraSnapshotBuffer(uint8_t** outBuf, size_t* outLen)
{
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient camHttp;
  String camUrl = CAMERA_SNAPSHOT_URL;

camHttp.begin(camClient, camUrl);

// Build Basic Authentication header manually
String credentials = String(CAMERA_USERNAME) + ":" + CAMERA_PASSWORD;
String auth = base64::encode(credentials);

camHttp.addHeader("Authorization", "Basic " + auth);

camHttp.setTimeout(4000);

int code = camHttp.GET();

Serial.print("HTTP Code: ");
Serial.println(code);
  if (code != 200)
  {
    Serial.print("Camera fetch failed, HTTP code: ");
    Serial.println(code);
    camHttp.end();
    return false;
  }

  int imgLen = camHttp.getSize();
  if (imgLen <= 0)
  {
    Serial.println("Camera snapshot has unknown/zero length");
    camHttp.end();
    return false;
  }

  uint8_t* buf = (uint8_t*)malloc(imgLen);
  if (!buf)
  {
    Serial.println("Not enough free memory to buffer camera photo");
    camHttp.end();
    return false;
  }

  WiFiClient* stream = camHttp.getStreamPtr();
  size_t written = 0;
  unsigned long lastData = millis();

  while (written < (size_t)imgLen && (millis() - lastData) < 5000)
  {
    size_t avail = stream->available();
    if (avail)
    {
      size_t toRead = avail;
      if (toRead > (size_t)imgLen - written) toRead = imgLen - written;
      int c = stream->readBytes(buf + written, toRead);
      written += c;
      lastData = millis();
    }
    else
    {
      delay(1);
    }
  }

  camHttp.end();

  if (written != (size_t)imgLen)
  {
    Serial.println("Incomplete camera image read");
    free(buf);
    return false;
  }

  *outBuf = buf;
  *outLen = written;
  return true;
}

// Fetches one fresh snapshot and replaces the shared cache. Called
// periodically from loop() - this is the ONLY place that talks to the
// camera for snapshots; everyone else reads the cache.
void refreshSnapshotCache()
{
  uint8_t* newBuf = nullptr;
  size_t newLen = 0;

  if (fetchCameraSnapshotBuffer(&newBuf, &newLen))
  {
    if (cachedSnapshotBuf) free(cachedSnapshotBuf);
    cachedSnapshotBuf = newBuf;
    cachedSnapshotLen = newLen;
    cachedSnapshotValid = true;
  }
  // On failure, deliberately keep serving the last good cached image
  // rather than blanking it out over one bad fetch.
}

// ------------------------------------------------
// CAMERA -> TELEGRAM (uploads an actual photo, since Telegram's servers
// can't reach your home network to fetch a URL themselves)
// ------------------------------------------------

bool sendCameraSnapshotToTelegram(const String& chatId)
{
  if (!cachedSnapshotValid || cachedSnapshotBuf == nullptr)
  {
    Serial.println("Telegram photo request: no cached snapshot available yet");
    return false;
  }

  uint8_t* imgBuf = cachedSnapshotBuf;   // borrowed from the shared cache - do not free
  size_t imgLen = cachedSnapshotLen;

  String boundary = "ESP32CamBoundary7f3d9";
  String header =
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
      chatId + "\r\n" +
      "--" + boundary + "\r\n"
      "Content-Disposition: form-data; name=\"photo\"; filename=\"snapshot.jpg\"\r\n"
      "Content-Type: image/jpeg\r\n\r\n";
  String footer = "\r\n--" + boundary + "--\r\n";

  size_t totalLen = header.length() + imgLen + footer.length();
  uint8_t* body = (uint8_t*)malloc(totalLen);

  if (!body)
  {
    Serial.println("Not enough memory to build Telegram photo request");
    return false;
  }

  memcpy(body, header.c_str(), header.length());
  memcpy(body + header.length(), imgBuf, imgLen);
  memcpy(body + header.length() + imgLen, footer.c_str(), footer.length());

  HTTPClient tgHttp;
  String tgUrl = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/sendPhoto";

  tgHttp.begin(client, tgUrl);   // reuses the existing insecure TLS client
  tgHttp.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  int tgCode = tgHttp.POST(body, totalLen);
  free(body);

  if (tgCode <= 0)
  {
    Serial.print("Telegram sendPhoto failed: ");
    Serial.println(tgHttp.errorToString(tgCode));
    tgHttp.end();
    return false;
  }

  Serial.print("Telegram sendPhoto HTTP code: ");
  Serial.println(tgCode);
  tgHttp.end();
  return (tgCode == 200);
}

// ------------------------------------------------
// CAMERA -> FIREBASE (lets the remote PWA see a snapshot, on demand,
// without needing the camera or PWA to talk to each other directly)
// ------------------------------------------------

void captureAndUploadSnapshotToFirebase()
{
  if (!cachedSnapshotValid || cachedSnapshotBuf == nullptr)
  {
    Serial.println("Skipping Firebase camera upload - no cached snapshot available yet");
    return;
  }

  String encoded = base64::encode(cachedSnapshotBuf, cachedSnapshotLen);   // reads the shared cache, doesn't free it

  // base64's character set (A-Z a-z 0-9 + / =) needs no JSON escaping.
  String payload = "{\"image\":\"" + encoded + "\",\"timestamp\":" + String(millis()) + "}";

  HTTPClient http;
  String url = String(FIREBASE_HOST) + "/gate/cameraSnapshot.json?auth=" + FIREBASE_AUTH;

  http.begin(firebaseClient, url);
  http.addHeader("Content-Type", "application/json");

  int code = http.PUT(payload);

  if (code <= 0)
  {
    Serial.print("Firebase camera snapshot upload failed: ");
    Serial.println(http.errorToString(code));
  }
  else
  {
    Serial.print("Camera snapshot uploaded to Firebase, HTTP code: ");
    Serial.println(code);
    logEvent("Snapshot");
  }

  http.end();
}

// Fetches a fresh snapshot from the CP Plus NVR (using proper HTTP Basic
// Auth, not credentials-in-URL) and relays the JPEG bytes straight through
// to whoever requested /camera.jpg. Keeps the camera password out of the
// page source entirely.
void handleCameraSnapshot()
{
  if (!cachedSnapshotValid || cachedSnapshotBuf == nullptr)
  {
    server.send(503, "text/plain", "No cached snapshot yet - try again shortly");
    return;
  }

  server.setContentLength(cachedSnapshotLen);
  server.send(200, "image/jpeg", "");
  server.client().write(cachedSnapshotBuf, cachedSnapshotLen);
}

// ------------------------------------------------
// SETUP
// ------------------------------------------------

void setup()
{
  Serial.begin(115200);
  delay(1500);   // gives you time to open the Serial Monitor before the boot log prints

  pinMode(RELAY1, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(DOORBELL_PIN, INPUT_PULLUP);   // internal pull-up - no external resistor needed

  // Active LOW relay
  digitalWrite(RELAY1, HIGH);
  digitalWrite(LED_PIN, LOW);

  Serial.println("");
  Serial.println("================================");
  Serial.println("ESP32 Smart Gate Controller");
  Serial.println("================================");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  startWiFi();

  client.setInsecure();
  firebaseClient.setInsecure();

  // Register web routes now; espalexa.begin(&server) starts the shared
  // server once Wi-Fi connects for the first time (see handleWiFi()).
  server.on("/", handleRoot);
  server.on("/doorbell", handleDoorbell);
  server.on("/open", handleOpen);
  server.on("/camera.jpg", handleCameraSnapshot);

  // Espalexa needs first refusal on any unmatched request, so it can
  // recognize Alexa's discovery/control calls before we 404 them.
  server.onNotFound([]() {
    if (!espalexa.handleAlexaApiCall(server.uri(), server.arg(0)))
    {
      server.send(404, "text/plain", "Not found");
    }
  });

  espalexa.addDevice("Gate", onGateAlexaChange);
}

// ------------------------------------------------
// LOOP
// ------------------------------------------------

void loop()
{
  handleWiFi();
  checkPhysicalDoorbell();

  if (wasConnected && serverStarted)
  {
    espalexa.loop();   // handles both regular page requests and Alexa's local calls

    if (millis() - lastTelegramCheck >= telegramInterval)
    {
      lastTelegramCheck = millis();
      checkTelegramMessages();
    }

    if (millis() - lastFirebaseCheck >= firebaseCommandInterval)
    {
      lastFirebaseCheck = millis();
      checkFirebaseCommand();
    }

    if (millis() - lastFirebaseStatusPush >= firebaseStatusInterval)
    {
      lastFirebaseStatusPush = millis();
      pushStatusToFirebase(true);   // forced heartbeat - keeps uptime/liveness fresh even with no change
    }

    if (millis() - lastSnapshotCacheRefresh >= snapshotCacheInterval)
    {
      lastSnapshotCacheRefresh = millis();
      refreshSnapshotCache();
    }
  }

  if (gateOpen)
  {
    if (millis() - unlockTime >= unlockDuration)
    {
      digitalWrite(RELAY1, HIGH);

      gateOpen = false;

      Serial.println("");
      Serial.println("================================");
      Serial.println("AUTO LOCK");
      Serial.println("Relay OFF");
      Serial.println("Gate Locked");
      Serial.println("================================");

      logEvent("Door closed");
      pushStatusToFirebase();
    }
  }
}
