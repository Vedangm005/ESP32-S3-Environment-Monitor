#include <Wire.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>

#define DHT_PIN 4
#define IR_PIN 5
#define TRIG_PIN 6
#define ECHO_PIN 7

#define OLED_SDA 8
#define OLED_SCL 9

#define DHT_TYPE DHT11

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

WebServer server(80);

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
  U8G2_R0,
  U8X8_PIN_NONE);

DHT dht(
  DHT_PIN,
  DHT_TYPE);

float temperature = NAN;
float humidity = NAN;

float distance = -1;
float lastValidDistance = -1;

bool dhtOK = false;
bool ultrasonicOK = false;

bool presenceDetected = false;

const float PRESENCE_DISTANCE = 80.0;

const int TOTAL_PAGES = 4;

int currentPage = 0;

bool displayAwake = true;

unsigned long lastInteraction = 0;

const unsigned long DISPLAY_TIMEOUT = 15000;

unsigned long lastDHTRead = 0;
unsigned long lastDistanceRead = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastDataLog = 0;

const unsigned long DHT_INTERVAL = 2000;
const unsigned long DISTANCE_INTERVAL = 250;
const unsigned long DISPLAY_INTERVAL = 100;
const unsigned long DATA_LOG_INTERVAL = 1000;

bool gestureActive = false;
bool gestureReady = true;

unsigned long gestureStartTime = 0;
unsigned long lastGestureTime = 0;

const unsigned long GESTURE_CONFIRM_TIME = 120;
const unsigned long GESTURE_COOLDOWN = 800;

void showStartup() {

  oled.clearBuffer();

  oled.setFont(
    u8g2_font_ncenB14_tr);

  oled.drawStr(
    20,
    27,
    "MES LAB");

  oled.setFont(
    u8g2_font_6x10_tr);

  oled.drawStr(
    17,
    44,
    "AI SENSOR");

  oled.drawStr(
    27,
    56,
    "SYSTEM");

  oled.sendBuffer();
}

void drawProgressBar(
  int x,
  int y,
  int w,
  int h,
  int percent) {

  percent = constrain(
    percent,
    0,
    100);

  oled.drawFrame(
    x,
    y,
    w,
    h);

  int fillWidth =
    ((w - 2) * percent) / 100;

  if (fillWidth > 0) {

    oled.drawBox(
      x + 1,
      y + 1,
      fillWidth,
      h - 2);
  }
}

void drawPageIndicator(
  int page) {

  for (
    int i = 0;
    i < TOTAL_PAGES;
    i++) {

    int x =
      48 + i * 10;

    if (i == page) {

      oled.drawBox(
        x,
        61,
        6,
        2);

    } else {

      oled.drawFrame(
        x,
        61,
        6,
        2);
    }
  }
}

int calculateComfort() {

  if (!dhtOK)
    return 0;

  int score = 0;

  if (
    temperature >= 20 && temperature <= 30) {

    score += 50;

  } else if (
    temperature >= 18 && temperature <= 32) {

    score += 35;

  } else {

    score += 20;
  }

  if (
    humidity >= 40 && humidity <= 70) {

    score += 50;

  } else if (
    humidity >= 30 && humidity <= 80) {

    score += 35;

  } else {

    score += 20;
  }

  return score;
}

float getSingleDistance() {

  digitalWrite(
    TRIG_PIN,
    LOW);

  delayMicroseconds(2);

  digitalWrite(
    TRIG_PIN,
    HIGH);

  delayMicroseconds(10);

  digitalWrite(
    TRIG_PIN,
    LOW);

  long duration =
    pulseIn(
      ECHO_PIN,
      HIGH,
      25000);

  if (duration == 0)
    return -1;

  float d =
    duration * 0.0343 / 2.0;

  if (
    d < 2 || d > 400) {

    return -1;
  }

  return d;
}

float median3(
  float a,
  float b,
  float c) {

  if (a > b) {

    float temp = a;
    a = b;
    b = temp;
  }

  if (b > c) {

    float temp = b;
    b = c;
    c = temp;
  }

  if (a > b) {

    float temp = a;
    a = b;
    b = temp;
  }

  return b;
}

float getFilteredDistance() {

  float d1 =
    getSingleDistance();

  delay(8);

  float d2 =
    getSingleDistance();

  delay(8);

  float d3 =
    getSingleDistance();

  if (
    d1 < 0 && d2 < 0 && d3 < 0) {

    return -1;
  }

  if (
    d1 < 0 || d2 < 0) {

    if (d1 > 0)
      return d1;

    if (d2 > 0)
      return d2;

    return d3;
  }

  if (d3 < 0) {

    return (
             d1 + d2)
           / 2.0;
  }

  return median3(
    d1,
    d2,
    d3);
}

void readDHT() {

  float t =
    dht.readTemperature();

  float h =
    dht.readHumidity();

  if (
    !isnan(t) && !isnan(h)) {

    temperature = t;
    humidity = h;

    dhtOK = true;

  } else {

    dhtOK = false;
  }
}

void readDistance() {

  float newDistance =
    getFilteredDistance();

  if (newDistance > 0) {

    distance =
      newDistance;

    lastValidDistance =
      newDistance;

    ultrasonicOK =
      true;

  } else {

    if (lastValidDistance > 0) {

      distance =
        lastValidDistance;

    } else {

      distance = -1;
    }

    ultrasonicOK =
      false;
  }

  if (
    distance > 0 && distance <= PRESENCE_DISTANCE) {

    presenceDetected =
      true;

  } else {

    presenceDetected =
      false;
  }
}

void processGesture(
  unsigned long now) {

  int irState =
    digitalRead(
      IR_PIN);

  if (irState == LOW) {

    if (!gestureActive) {

      gestureActive =
        true;

      gestureStartTime =
        now;
    }

    if (
      gestureReady && (now - gestureStartTime >= GESTURE_CONFIRM_TIME) && (now - lastGestureTime >= GESTURE_COOLDOWN)) {

      currentPage++;

      if (
        currentPage >= TOTAL_PAGES) {

        currentPage = 0;
      }

      lastGestureTime =
        now;

      lastInteraction =
        now;

      displayAwake =
        true;

      gestureReady =
        false;
    }

  } else {

    gestureActive =
      false;

    gestureReady =
      true;
  }
}

void showEnvironmentPage() {

  oled.clearBuffer();

  oled.setFont(
    u8g2_font_6x10_tr);

  oled.drawStr(
    0,
    9,
    "ENVIRONMENT");

  oled.drawLine(
    0,
    12,
    127,
    12);

  oled.setFont(
    u8g2_font_ncenB14_tr);

  oled.setCursor(
    0,
    34);

  if (dhtOK) {

    oled.print(
      temperature,
      1);

    oled.print("C");

  } else {

    oled.print(
      "--.-C");
  }

  int tempPercent = 0;

  if (dhtOK) {

    tempPercent =
      map(
        constrain(
          (int)temperature,
          0,
          40),
        0,
        40,
        0,
        100);
  }

  drawProgressBar(
    75,
    21,
    48,
    7,
    tempPercent);

  oled.setFont(
    u8g2_font_6x10_tr);

  oled.setCursor(
    0,
    48);

  oled.print(
    "Humidity ");

  if (dhtOK)
    oled.print(
      humidity,
      0);
  else
    oled.print("--");

  oled.print("%");

  int humidityPercent = 0;

  if (dhtOK) {

    humidityPercent =
      constrain(
        (int)humidity,
        0,
        100);
  }

  drawProgressBar(
    75,
    37,
    48,
    7,
    humidityPercent);

  oled.setCursor(
    75,
    54);

  if (!dhtOK)
    oled.print("DHT ERR");

  else if (
    temperature > 30)
    oled.print("HOT");

  else if (
    humidity > 70)
    oled.print("HUMID");

  else if (
    temperature < 20)
    oled.print("COOL");

  else
    oled.print("GOOD");

  drawPageIndicator(
    0);

  oled.sendBuffer();
}

void showPresencePage() {

  oled.clearBuffer();

  oled.setFont(
    u8g2_font_6x10_tr);

  oled.drawStr(
    0,
    9,
    "PRESENCE");

  oled.drawLine(
    0,
    12,
    127,
    12);

  oled.setFont(
    u8g2_font_ncenB14_tr);

  oled.setCursor(
    5,
    35);

  if (distance > 0) {

    oled.print(
      distance,
      1);

    oled.print(
      "cm");

  } else {

    oled.print(
      "--");
  }

  oled.setFont(
    u8g2_font_6x10_tr);

  oled.setCursor(
    5,
    49);

  if (presenceDetected) {

    oled.print(
      "PERSON DETECTED");

  } else {

    oled.print(
      "AREA CLEAR");
  }

  int proximityPercent = 0;

  if (distance > 0) {

    proximityPercent =
      map(
        constrain(
          (int)distance,
          0,
          200),
        200,
        0,
        0,
        100);
  }

  drawProgressBar(
    5,
    54,
    38,
    5,
    proximityPercent);

  drawPageIndicator(
    1);

  oled.sendBuffer();
}

void showComfortPage() {

  oled.clearBuffer();

  oled.setFont(
    u8g2_font_6x10_tr);

  oled.drawStr(
    0,
    9,
    "COMFORT INDEX");

  oled.drawLine(
    0,
    12,
    127,
    12);

  int score =
    calculateComfort();

  oled.setFont(
    u8g2_font_ncenB14_tr);

  oled.setCursor(
    5,
    35);

  if (dhtOK) {

    oled.print(
      score);

    oled.print(
      "%");

  } else {

    oled.print(
      "--%");
  }

  drawProgressBar(
    55,
    24,
    68,
    9,
    score);

  oled.setFont(
    u8g2_font_6x10_tr);

  oled.setCursor(
    5,
    50);

  if (!dhtOK) {

    oled.print(
      "SENSOR ERROR");

  } else if (
    score >= 80) {

    oled.print(
      "VERY COMFORTABLE");

  } else if (
    score >= 60) {

    oled.print(
      "COMFORTABLE");

  } else {

    oled.print(
      "NEEDS ATTENTION");
  }

  drawPageIndicator(
    2);

  oled.sendBuffer();
}

void showSystemPage() {

  oled.clearBuffer();

  oled.setFont(
    u8g2_font_6x10_tr);

  oled.drawStr(
    0,
    9,
    "SYSTEM STATUS");

  oled.drawLine(
    0,
    12,
    127,
    12);

  oled.setCursor(
    0,
    25);

  oled.print(
    "MCU ESP32-S3");

  oled.setCursor(
    0,
    37);

  oled.print(
    "DHT11 ");

  oled.print(
    dhtOK
      ? "OK"
      : "ERR");

  oled.setCursor(
    65,
    37);

  oled.print(
    "US ");

  oled.print(
    ultrasonicOK
      ? "OK"
      : "ERR");

  oled.setCursor(
    0,
    49);

  oled.print(
    "IR ");

  oled.print(
    digitalRead(IR_PIN) == LOW
      ? "ACTIVE"
      : "READY");

  oled.setCursor(
    65,
    49);

  oled.print(
    WiFi.status() == WL_CONNECTED
      ? "WiFi OK"
      : "WiFi ERR");

  unsigned long totalSeconds =
    millis() / 1000;

  unsigned long hours =
    totalSeconds / 3600;

  unsigned long minutes =
    (totalSeconds % 3600) / 60;

  unsigned long seconds =
    totalSeconds % 60;

  oled.setCursor(
    0,
    60);

  oled.print(
    "UP ");

  if (hours < 10)
    oled.print("0");

  oled.print(
    hours);

  oled.print(":");

  if (minutes < 10)
    oled.print("0");

  oled.print(
    minutes);

  oled.print(":");

  if (seconds < 10)
    oled.print("0");

  oled.print(
    seconds);

  drawPageIndicator(
    3);

  oled.sendBuffer();
}

void showSleepScreen() {

  oled.clearBuffer();

  oled.setFont(
    u8g2_font_ncenB14_tr);

  oled.drawStr(
    18,
    28,
    "READY");

  oled.setFont(
    u8g2_font_6x10_tr);

  oled.drawStr(
    18,
    46,
    "Wave to interact");

  oled.sendBuffer();
}

void updateDisplay() {

  if (!displayAwake) {

    showSleepScreen();

    return;
  }

  switch (
    currentPage) {

    case 0:
      showEnvironmentPage();
      break;

    case 1:
      showPresencePage();
      break;

    case 2:
      showComfortPage();
      break;

    case 3:
      showSystemPage();
      break;
  }
}

void logSensorData(
  unsigned long now) {

  if (
    now - lastDataLog < DATA_LOG_INTERVAL)
    return;

  lastDataLog =
    now;

  Serial.print(
    now);

  Serial.print(",");

  if (dhtOK)
    Serial.print(
      temperature,
      2);
  else
    Serial.print(
      "NaN");

  Serial.print(",");

  if (dhtOK)
    Serial.print(
      humidity,
      2);
  else
    Serial.print(
      "NaN");

  Serial.print(",");

  if (
    distance > 0 && ultrasonicOK) {

    Serial.print(
      distance,
      2);

  } else {

    Serial.print(
      "NaN");
  }

  Serial.print(",");

  if (
    digitalRead(IR_PIN)
    == HIGH) {

    Serial.print(
      "1");

  } else {

    Serial.print(
      "0");
  }

  Serial.print(",");

  if (
    presenceDetected) {

    Serial.println(
      "1");

  } else {

    Serial.println(
      "0");
  }
}

void handleData() {

  int comfort =
    calculateComfort();

  String json =
    "{";

  json +=
    "\"temperature\":";

  if (dhtOK)
    json += String(
      temperature,
      1);
  else
    json += "null";

  json +=
    ",\"humidity\":";

  if (dhtOK)
    json += String(
      humidity,
      1);
  else
    json += "null";

  json +=
    ",\"distance\":";

  if (
    distance > 0)
    json += String(
      distance,
      1);
  else
    json += "null";

  json +=
    ",\"occupied\":";

  json +=
    presenceDetected
      ? "true"
      : "false";

  json +=
    ",\"comfort\":";

  json +=
    String(
      comfort);

  json +=
    ",\"dhtOK\":";

  json +=
    dhtOK
      ? "true"
      : "false";

  json +=
    ",\"ultrasonicOK\":";

  json +=
    ultrasonicOK
      ? "true"
      : "false";

  json +=
    ",\"ir\":";

  json +=
    digitalRead(IR_PIN)
        == LOW
      ? "true"
      : "false";

  json +=
    ",\"uptime\":";

  json +=
    String(
      millis() / 1000);

  json +=
    ",\"rssi\":";

  if (
    WiFi.status()
    == WL_CONNECTED) {

    json += String(
      WiFi.RSSI());

  } else {

    json += "null";
  }

  json +=
    ",\"oledPage\":";

  json +=
    String(
      currentPage + 1);

  json +=
    ",\"totalPages\":";

  json +=
    String(
      TOTAL_PAGES);

  json +=
    "}";

  server.sendHeader(
    "Access-Control-Allow-Origin",
    "*");

  server.sendHeader(
    "Access-Control-Allow-Methods",
    "GET,OPTIONS");

  server.sendHeader(
    "Access-Control-Allow-Headers",
    "Content-Type");

  server.send(
    200,
    "application/json",
    json);
}

void handleRoot() {

  String html = R"rawliteral(

<!DOCTYPE html>

<html>

<head>

<meta charset="UTF-8">

<meta
name="viewport"
content="width=device-width, initial-scale=1.0">

<title>
ESP32-S3 Environment Hub
</title>

<style>

* {
  box-sizing: border-box;
}

body {

  margin: 0;

  background:
  #080d13;

  color:
  #f1f5f9;

  font-family:
  Arial,
  Helvetica,
  sans-serif;
}

.container {

  max-width:
  1100px;

  margin:
  auto;

  padding:
  25px;
}

.header {

  display:
  flex;

  justify-content:
  space-between;

  align-items:
  center;

  gap:
  15px;

  flex-wrap:
  wrap;

  margin-bottom:
  25px;
}

h1 {

  margin:
  0;

  font-size:
  28px;
}

.subtitle {

  color:
  #7d8a9a;

  margin-top:
  7px;
}

.status {

  padding:
  9px 15px;

  border-radius:
  20px;

  background:
  #10251c;

  color:
  #4cff9b;

  border:
  1px solid
  #245f43;
}

.grid {

  display:
  grid;

  grid-template-columns:
  repeat(
    auto-fit,
    minmax(
      220px,
      1fr
    )
  );

  gap:
  18px;
}

.card {

  background:
  #111923;

  border:
  1px solid
  #202d3a;

  border-radius:
  18px;

  padding:
  20px;

  box-shadow:
  0 10px 30px
  rgba(
    0,
    0,
    0,
    0.25
  );
}

.card-title {

  color:
  #7f8c9d;

  font-size:
  13px;

  margin-bottom:
  10px;
}

.value {

  font-size:
  36px;

  font-weight:
  bold;
}

.unit {

  font-size:
  16px;

  color:
  #7f8c9d;
}

.bar {

  height:
  8px;

  margin-top:
  15px;

  background:
  #202b37;

  border-radius:
  10px;

  overflow:
  hidden;
}

.fill {

  height:
  100%;

  width:
  0%;

  background:
  linear-gradient(
    90deg,
    #00d9ff,
    #00ff9d
  );

  transition:
  width
  0.4s;
}

.big {

  margin-top:
  18px;
}

.info-grid {

  display:
  grid;

  grid-template-columns:
  repeat(
    auto-fit,
    minmax(
      170px,
      1fr
    )
  );

  gap:
  12px;
}

.info {

  background:
  #0b121a;

  border-radius:
  12px;

  padding:
  15px;
}

.info span {

  display:
  block;

  color:
  #6f7d8d;

  font-size:
  11px;

  margin-bottom:
  6px;
}

.info strong {

  font-size:
  15px;
}

.good {

  color:
  #4cff9b;
}

.bad {

  color:
  #ff6173;
}

.footer {

  text-align:
  center;

  color:
  #566373;

  margin-top:
  25px;

  font-size:
  12px;
}

</style>

</head>

<body>

<div class="container">

<div class="header">

<div>

<h1>
ESP32-S3 Environment Hub
</h1>

<div class="subtitle">
AI-Ready Embedded Monitoring System
</div>

</div>

<div
class="status"
id="connection">

● CONNECTED

</div>

</div>

<div class="grid">

<div class="card">

<div class="card-title">
TEMPERATURE
</div>

<div class="value">

<span
id="temperature">
--
</span>

<span class="unit">
°C
</span>

</div>

<div class="bar">

<div
class="fill"
id="tempBar">
</div>

</div>

</div>

<div class="card">

<div class="card-title">
HUMIDITY
</div>

<div class="value">

<span
id="humidity">
--
</span>

<span class="unit">
%
</span>

</div>

<div class="bar">

<div
class="fill"
id="humidityBar">
</div>

</div>

</div>

<div class="card">

<div class="card-title">
DISTANCE
</div>

<div class="value">

<span
id="distance">
--
</span>

<span class="unit">
cm
</span>

</div>

<div class="bar">

<div
class="fill"
id="distanceBar">
</div>

</div>

</div>

<div class="card">

<div class="card-title">
COMFORT INDEX
</div>

<div class="value">

<span
id="comfort">
--
</span>

<span class="unit">
%
</span>

</div>

<div class="bar">

<div
class="fill"
id="comfortBar">
</div>

</div>

</div>

</div>

<div class="card big">

<div class="card-title">
SYSTEM STATUS
</div>

<div class="info-grid">

<div class="info">

<span>
PRESENCE
</span>

<strong
id="presence">
--
</strong>

</div>

<div class="info">

<span>
DHT11
</span>

<strong
id="dht">
--
</strong>

</div>

<div class="info">

<span>
ULTRASONIC
</span>

<strong
id="ultrasonic">
--
</strong>

</div>

<div class="info">

<span>
IR SENSOR
</span>

<strong
id="ir">
--
</strong>

</div>

<div class="info">

<span>
OLED PAGE
</span>

<strong
id="oledPage">
--
</strong>

</div>

<div class="info">

<span>
WIFI RSSI
</span>

<strong
id="rssi">
--
</strong>

</div>

<div class="info">

<span>
UPTIME
</span>

<strong
id="uptime">
--
</strong>

</div>

</div>

</div>

<div class="footer">

ESP32-S3 • MES LAB • Embedded AI Research Prototype

</div>

</div>

<script>

function setText(
  id,
  value
) {

  document.getElementById(
    id
  ).textContent =
    value;
}

function setBar(
  id,
  value
) {

  value =
    Math.max(
      0,
      Math.min(
        100,
        value
      )
    );

  document.getElementById(
    id
  ).style.width =
    value + "%";
}

function formatUptime(
  seconds
) {

  let h =
    Math.floor(
      seconds / 3600
    );

  let m =
    Math.floor(
      (
        seconds % 3600
      ) / 60
    );

  let s =
    Math.floor(
      seconds % 60
    );

  return (
    String(h).padStart(2, "0")
    + ":" +
    String(m).padStart(2, "0")
    + ":" +
    String(s).padStart(2, "0")
  );
}

async function updateData() {

  try {

    const response =
      await fetch(
        "/data"
      );

    const data =
      await response.json();

    if (
      data.temperature !== null
    ) {

      setText(
        "temperature",
        data.temperature.toFixed(1)
      );

      setBar(
        "tempBar",
        data.temperature / 40 * 100
      );
    }

    if (
      data.humidity !== null
    ) {

      setText(
        "humidity",
        data.humidity.toFixed(0)
      );

      setBar(
        "humidityBar",
        data.humidity
      );
    }

    if (
      data.distance !== null
    ) {

      setText(
        "distance",
        data.distance.toFixed(1)
      );

      let proximity =
        100 -
        (
          data.distance /
          200
        ) * 100;

      setBar(
        "distanceBar",
        proximity
      );
    }

    setText(
      "comfort",
      data.comfort
    );

    setBar(
      "comfortBar",
      data.comfort
    );

    if (
      data.occupied
    ) {

      setText(
        "presence",
        "PERSON DETECTED"
      );

      document
        .getElementById(
          "presence"
        )
        .className =
        "good";

    } else {

      setText(
        "presence",
        "AREA CLEAR"
      );

      document
        .getElementById(
          "presence"
        )
        .className =
        "";
    }

    setText(
      "dht",
      data.dhtOK
        ? "ONLINE"
        : "ERROR"
    );

    setText(
      "ultrasonic",
      data.ultrasonicOK
        ? "ONLINE"
        : "ERROR"
    );

    setText(
      "ir",
      data.ir
        ? "ACTIVE"
        : "READY"
    );

    setText(
      "oledPage",
      data.oledPage
      + " / "
      + data.totalPages
    );

    if (
      data.rssi !== null
    ) {

      setText(
        "rssi",
        data.rssi
        + " dBm"
      );
    }

    setText(
      "uptime",
      formatUptime(
        data.uptime
      )
    );

    setText(
      "connection",
      "● CONNECTED"
    );

  }

  catch(error) {

    setText(
      "connection",
      "● DISCONNECTED"
    );
  }
}

updateData();

setInterval(
  updateData,
  1000
);

</script>

</body>

</html>

)rawliteral";

  server.sendHeader(
    "Access-Control-Allow-Origin",
    "*");

  server.send(
    200,
    "text/html",
    html);
}

void handleOptions() {

  server.sendHeader(
    "Access-Control-Allow-Origin",
    "*");

  server.sendHeader(
    "Access-Control-Allow-Methods",
    "GET,OPTIONS");

  server.sendHeader(
    "Access-Control-Allow-Headers",
    "Content-Type");

  server.send(
    204);
}

void setup() {

  Serial.begin(
    115200);

  Wire.begin(
    OLED_SDA,
    OLED_SCL);

  oled.begin();

  dht.begin();

  pinMode(
    IR_PIN,
    INPUT);

  pinMode(
    TRIG_PIN,
    OUTPUT);

  pinMode(
    ECHO_PIN,
    INPUT);

  digitalWrite(
    TRIG_PIN,
    LOW);

  showStartup();

  delay(
    1500);

  lastInteraction =
    millis();

  WiFi.mode(
    WIFI_STA);

  WiFi.begin(
    ssid,
    password);

  unsigned long wifiStart =
    millis();

  while (
    WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {

    delay(250);
  }

  server.on(
    "/",
    HTTP_GET,
    handleRoot);

  server.on(
    "/data",
    HTTP_GET,
    handleData);

  server.on(
    "/data",
    HTTP_OPTIONS,
    handleOptions);

  server.begin();

  Serial.println(
    "timestamp,temp,humidity,distance,ir,presence");
}

void loop() {

  unsigned long now =
    millis();

  if (
    now - lastDHTRead
    >= DHT_INTERVAL) {

    readDHT();

    lastDHTRead =
      now;
  }

  if (
    now - lastDistanceRead
    >= DISTANCE_INTERVAL) {

    readDistance();

    lastDistanceRead =
      now;
  }

  processGesture(
    now);

  if (
    presenceDetected) {

    displayAwake =
      true;

    lastInteraction =
      now;
  }

  if (
    now - lastInteraction
    >= DISPLAY_TIMEOUT) {

    displayAwake =
      false;
  }

  if (
    now - lastDisplayUpdate
    >= DISPLAY_INTERVAL) {

    updateDisplay();

    lastDisplayUpdate =
      now;
  }

  logSensorData(
    now);

  server.handleClient();
}
