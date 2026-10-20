/*
  SMART LAB - ESP32 Standalone Web Server

  Features:
  - DHT22 temperature and humidity
  - MQ-2 analog reading on GPIO34
  - Flame detection on GPIO27
  - People counter using two IR sensors
  - Local buzzer alarm on GPIO25
  - Mobile-friendly website
  - Temperature and humidity charts
  - No Blynk required

  Fan runs independently from a separate 12V adapter.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>

// =====================================================
// WIFI SETTINGS
// =====================================================

// Connect to your existing Wi-Fi first.
const char* WIFI_SSID = "ssid";
const char* WIFI_PASSWORD = "password";

// If Wi-Fi connection fails, ESP32 creates a hotspot.
const char* AP_SSID = "SMART-LAB";
const char* AP_PASSWORD = "SmartLab123";

// =====================================================
// HARDWARE PINS
// =====================================================

#define DHT_PIN       4
#define DHT_TYPE      DHT22

#define MQ2_AO_PIN    34
#define FLAME_PIN     27

#define IR_A_PIN      18
#define IR_B_PIN      19

#define BUZZER_PIN    25

// =====================================================
// SENSOR SETTINGS
// =====================================================

// Adjust after observing safe baseline readings.
// This is an example threshold, not a calibrated gas level.
const int MQ2_THRESHOLD = 2000;

// Many digital flame sensor modules are active LOW.
// Change to HIGH if your module works oppositely.
const int FLAME_ACTIVE = LOW;

// Many IR obstacle modules are active LOW.
const int IR_ACTIVE = LOW;

// =====================================================
// OBJECTS
// =====================================================

WebServer server(80);
DHT dht(DHT_PIN, DHT_TYPE);

// =====================================================
// SENSOR DATA
// =====================================================

float temperature = NAN;
float humidity = NAN;

int mq2RawValue = 0;

bool gasDetected = false;
bool flameDetected = false;

int peopleInside = 0;

String alertMessage = "SMART LAB starting";

unsigned long lastSensorRead = 0;

// =====================================================
// PEOPLE COUNTER VARIABLES
// =====================================================

int firstSensor = 0;
unsigned long firstSensorTime = 0;
unsigned long lastCountTime = 0;

// =====================================================
// WEBSITE
// =====================================================

const char PAGE[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">

<title>SMART LAB</title>

<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

<style>
* {
  box-sizing: border-box;
}

body {
  margin: 0;
  font-family: Arial, sans-serif;
  background: #101827;
  color: white;
}

header {
  background: #172554;
  padding: 22px 12px;
  text-align: center;
}

header h1 {
  margin: 0;
  font-size: 30px;
}

header p {
  color: #dbeafe;
}

#connection {
  font-size: 14px;
  color: #86efac;
}

main {
  max-width: 1000px;
  margin: auto;
  padding: 15px;
}

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(145px, 1fr));
  gap: 12px;
}

.card {
  background: #1e293b;
  border-radius: 12px;
  padding: 18px;
  text-align: center;
  margin-bottom: 12px;
  overflow: hidden;
}

.label {
  color: #cbd5e1;
  font-size: 15px;
}

.value {
  font-size: 25px;
  font-weight: bold;
  margin-top: 13px;
  overflow-wrap: anywhere;
}

.status {
  font-size: 17px;
  font-weight: bold;
}

.ok {
  color: #4ade80;
}

.bad {
  color: #f87171;
}

canvas {
  background: white;
  border-radius: 8px;
  max-height: 300px;
}

.note {
  color: #cbd5e1;
  font-size: 13px;
  line-height: 1.6;
}

footer {
  text-align: center;
  padding: 20px;
  color: #cbd5e1;
}
</style>
</head>

<body>

<header>
  <h1>SMART LAB</h1>
  <p>Laboratory Monitoring and Safety System</p>
  <div id="connection">Connecting to ESP32...</div>
</header>

<main>

  <div class="grid">

    <div class="card">
      <div class="label">Temperature</div>
      <div class="value">
        <span id="temp">--</span> °C
      </div>
    </div>

    <div class="card">
      <div class="label">Humidity</div>
      <div class="value">
        <span id="hum">--</span> %
      </div>
    </div>

    <div class="card">
      <div class="label">MQ-2 Raw Reading</div>
      <div class="value" id="mq2">--</div>
    </div>

    <div class="card">
      <div class="label">Gas / Smoke Status</div>
      <div class="value status" id="gas">Checking</div>
    </div>

    <div class="card">
      <div class="label">Flame Sensor</div>
      <div class="value status" id="flame">Checking</div>
    </div>

    <div class="card">
      <div class="label">People Inside</div>
      <div class="value" id="people">0</div>
    </div>

  </div>

  <div class="card">
    <h2>Emergency Status</h2>
    <div class="value status" id="alert">
      Initializing...
    </div>
  </div>

  <div class="card">
    <h2>Temperature History</h2>
    <canvas id="tempChart"></canvas>
  </div>

  <div class="card">
    <h2>Humidity History</h2>
    <canvas id="humChart"></canvas>
  </div>

  <div class="card">
    <h3>System Information</h3>
    <p class="note">
      Readings refresh every two seconds.
      Charts retain recent readings in this browser session.
      Reloading the page clears the chart history.
      The MQ-2 value is a raw analog reading, not gas concentration.
    </p>
  </div>

</main>

<footer>
  SMART LAB | ESP32 Local Web Dashboard
</footer>

<script>

let labels = [];
let temperatures = [];
let humidities = [];

let lastAlarm = false;
let lastChartTime = 0;

const tempChart = new Chart(
  document.getElementById('tempChart'),
  {
    type: 'line',
    data: {
      labels: labels,
      datasets: [{
        label: 'Temperature (°C)',
        data: temperatures,
        borderColor: '#2563eb',
        backgroundColor: 'rgba(37,99,235,0.15)',
        fill: true,
        tension: 0.25
      }]
    },
    options: {
      responsive: true,
      animation: false
    }
  }
);

const humChart = new Chart(
  document.getElementById('humChart'),
  {
    type: 'line',
    data: {
      labels: labels,
      datasets: [{
        label: 'Humidity (%)',
        data: humidities,
        borderColor: '#0891b2',
        backgroundColor: 'rgba(8,145,178,0.15)',
        fill: true,
        tension: 0.25
      }]
    },
    options: {
      responsive: true,
      animation: false
    }
  }
);

// Optional browser beep.
// Audio may require a user interaction first.
function playBrowserAlarm() {
  try {
    const audio = new AudioContext();
    const oscillator = audio.createOscillator();
    const gain = audio.createGain();

    oscillator.connect(gain);
    gain.connect(audio.destination);

    oscillator.frequency.value = 880;
    gain.gain.value = 0.12;

    oscillator.start();

    setTimeout(() => {
      oscillator.stop();
      audio.close();
    }, 700);

  } catch (e) {
    console.log("Browser audio unavailable");
  }
}

async function updateDashboard() {

  try {

    const response = await fetch('/data', {
      cache: 'no-store'
    });

    if (!response.ok) {
      throw new Error('HTTP error');
    }

    const d = await response.json();

    document.getElementById('connection').textContent =
      'ESP32 connected | Live updates';

    document.getElementById('temp').textContent =
      d.temperature === null ? '--' : d.temperature.toFixed(1);

    document.getElementById('hum').textContent =
      d.humidity === null ? '--' : d.humidity.toFixed(1);

    document.getElementById('mq2').textContent =
      d.mq2Raw;

    const gas = document.getElementById('gas');

    gas.textContent = d.gas ? 'ALERT' : 'No trigger';
    gas.className =
      'value status ' + (d.gas ? 'bad' : 'ok');

    const flame = document.getElementById('flame');

    flame.textContent =
      d.flame ? 'FLAME ALERT' : 'No trigger';

    flame.className =
      'value status ' + (d.flame ? 'bad' : 'ok');

    document.getElementById('people').textContent =
      d.people;

    const alert = document.getElementById('alert');

    alert.textContent = d.alert;
    alert.className =
      'value status ' + (d.alarm ? 'bad' : 'ok');

    // Add a graph sample every 10 seconds.
    // This avoids filling the chart too quickly.
    const now = Date.now();

    if (
      now - lastChartTime >= 10000 &&
      d.temperature !== null &&
      d.humidity !== null
    ) {

      lastChartTime = now;

      labels.push(new Date().toLocaleTimeString());
      temperatures.push(d.temperature);
      humidities.push(d.humidity);

      // Keep the latest 100 samples.
      if (labels.length > 100) {
        labels.shift();
        temperatures.shift();
        humidities.shift();
      }

      tempChart.update();
      humChart.update();
    }

    // Beep once when a new alarm starts.
    if (d.alarm && !lastAlarm) {
      playBrowserAlarm();
    }

    lastAlarm = d.alarm;

  } catch (error) {

    document.getElementById('connection').textContent =
      'ESP32 unavailable. Check Wi-Fi connection.';

  }
}

updateDashboard();
setInterval(updateDashboard, 2000);

</script>

</body>
</html>
)HTMLPAGE";

// =====================================================
// READ SENSORS
// =====================================================

void readSensors() {

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  temperature = t;
  humidity = h;

  // Read MQ-2 analog value.
  mq2RawValue = analogRead(MQ2_AO_PIN);

  // Initial threshold only.
  gasDetected = mq2RawValue >= MQ2_THRESHOLD;

  // Detect flame.
  flameDetected =
      digitalRead(FLAME_PIN) == FLAME_ACTIVE;

  // Update emergency message.
  if (gasDetected && flameDetected) {

    alertMessage = "WARNING: GAS/SMOKE AND FLAME SENSOR ALERT";

  } else if (flameDetected) {

    alertMessage = "WARNING: FLAME SENSOR ACTIVATED";

  } else if (gasDetected) {

    alertMessage = "WARNING: MQ-2 THRESHOLD EXCEEDED";

  } else {

    alertMessage = "No sensor alarm detected";

  }

  // Operate local buzzer through a suitable driver.
  digitalWrite(
    BUZZER_PIN,
    (gasDetected || flameDetected) ? HIGH : LOW
  );

  // Serial Monitor output.
  Serial.println("\n===== SMART LAB =====");

  if (!isnan(temperature)) {
    Serial.printf("Temperature: %.1f C\n", temperature);
  } else {
    Serial.println("Temperature sensor read error");
  }

  if (!isnan(humidity)) {
    Serial.printf("Humidity: %.1f %%\n", humidity);
  } else {
    Serial.println("Humidity sensor read error");
  }

  Serial.printf("MQ-2 raw reading: %d\n", mq2RawValue);
  Serial.printf("Gas alert: %s\n", gasDetected ? "YES" : "NO");
  Serial.printf("Flame alert: %s\n", flameDetected ? "YES" : "NO");
  Serial.printf("People inside: %d\n", peopleInside);
  Serial.println("Status: " + alertMessage);
}

// =====================================================
// PEOPLE COUNTER
// =====================================================

// Assumes A followed by B = entry.
// Assumes B followed by A = exit.

void countPeople() {

  bool a = digitalRead(IR_A_PIN) == IR_ACTIVE;
  bool b = digitalRead(IR_B_PIN) == IR_ACTIVE;

  unsigned long now = millis();

  // Expire incomplete sequences.
  if (
    firstSensor != 0 &&
    now - firstSensorTime > 1500
  ) {
    firstSensor = 0;
  }

  if (now - lastCountTime < 500) {
    return;
  }

  if (firstSensor == 0) {

    if (a && !b) {

      firstSensor = 1;
      firstSensorTime = now;

    } else if (b && !a) {

      firstSensor = 2;
      firstSensorTime = now;

    }

  } else if (firstSensor == 1 && b) {

    peopleInside++;
    firstSensor = 0;
    lastCountTime = now;

    Serial.printf(
      "Entry detected. People inside: %d\n",
      peopleInside
    );

  } else if (firstSensor == 2 && a) {

    if (peopleInside > 0) {
      peopleInside--;
    }

    firstSensor = 0;
    lastCountTime = now;

    Serial.printf(
      "Exit detected. People inside: %d\n",
      peopleInside
    );

  }
}

// =====================================================
// SENSOR DATA ENDPOINT
// =====================================================

void handleData() {

  String json = "{";

  json += "\"temperature\":";

  if (isnan(temperature)) {
    json += "null";
  } else {
    json += String(temperature, 1);
  }

  json += ",\"humidity\":";

  if (isnan(humidity)) {
    json += "null";
  } else {
    json += String(humidity, 1);
  }

  json += ",\"mq2Raw\":";
  json += String(mq2RawValue);

  json += ",\"gas\":";
  json += gasDetected ? "true" : "false";

  json += ",\"flame\":";
  json += flameDetected ? "true" : "false";

  json += ",\"people\":";
  json += String(peopleInside);

  json += ",\"alarm\":";
  json += (gasDetected || flameDetected)
          ? "true"
          : "false";

  json += ",\"alert\":\"";
  json += alertMessage;
  json += "\"}";

  server.sendHeader("Cache-Control", "no-store");

  server.send(
    200,
    "application/json",
    json
  );
}

// =====================================================
// MAIN WEBSITE
// =====================================================

void handleRoot() {

  server.send_P(
    200,
    "text/html",
    PAGE
  );
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  pinMode(MQ2_AO_PIN, INPUT);
  pinMode(FLAME_PIN, INPUT);

  pinMode(IR_A_PIN, INPUT);
  pinMode(IR_B_PIN, INPUT);

  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);

  // ESP32 ADC setup.
  analogReadResolution(12);
  analogSetPinAttenuation(
    MQ2_AO_PIN,
    ADC_11db
  );

  dht.begin();

  // Connect to existing Wi-Fi.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");

  unsigned long startTime = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 15000
  ) {

    delay(300);
    Serial.print(".");

  }

  // Start hotspot if connection fails.
  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("\nWi-Fi connected!");

    Serial.print("Open dashboard at: http://");
    Serial.println(WiFi.localIP());

  } else {

    Serial.println("\nStarting SMART LAB hotspot");

    WiFi.mode(WIFI_AP);
    WiFi.softAP(
      AP_SSID,
      AP_PASSWORD
    );

    Serial.print("Wi-Fi name: ");
    Serial.println(AP_SSID);

    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);

    Serial.print("Open dashboard at: http://");
    Serial.println(WiFi.softAPIP());

  }

  // Initialize readings.
  readSensors();

  // Register website endpoints.
  server.on("/", handleRoot);
  server.on("/data", handleData);

  server.begin();

  Serial.println("SMART LAB web server started");
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {

  server.handleClient();

  unsigned long now = millis();

  // Update sensors every two seconds.
  if (now - lastSensorRead >= 2000) {

    lastSensorRead = now;
    readSensors();

  }

  // Check door sensors frequently.
  countPeople();

}
