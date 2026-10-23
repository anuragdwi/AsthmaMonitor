
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <math.h>

// =====================================================
// WIFI
// =====================================================

const char* WIFI_SSID = "Anurag's iPhone";
const char* WIFI_PASSWORD = "anurag112";

// Backup network if iPhone hotspot fails
const char* BACKUP_SSID = "AsthmaMonitor";
const char* BACKUP_PASSWORD = "asthma123";

// =====================================================
// DHT22
// =====================================================

#define DHT_PIN 4
#define DHT_TYPE DHT22

DHT dht(DHT_PIN, DHT_TYPE);

// =====================================================
// WEB SERVER
// =====================================================

WebServer server(80);

// =====================================================
// SIMULATED PARTICULATE VALUES
// NOT REAL MEASUREMENTS
// =====================================================

const float pm25Values[] = {12, 24, 35, 58, 72};
const float pm10Values[] = {20, 38, 55, 84, 110};

const int SAMPLE_COUNT = 5;
int sampleIndex = 0;

float temperatureC = NAN;
float humidity = NAN;

float pm25 = 12;
float pm10 = 20;

bool sensorOK = false;
String risk = "WAITING";

unsigned long lastDHT = 0;
unsigned long lastPM = 0;

// =====================================================
// DASHBOARD
// =====================================================

const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>

<head>

<meta name="viewport"
content="width=device-width,initial-scale=1">

<title>Asthma Monitor</title>

<style>

body{
margin:0;
padding:18px;
background:#f1f5f9;
color:#172033;
font-family:Arial,sans-serif
}

main{
max-width:760px;
margin:auto
}

header{
background:#142b55;
color:white;
padding:25px;
border-radius:16px
}

h1{
margin:0 0 8px
}

small{
color:#dbeafe
}

.grid{
display:grid;
grid-template-columns:1fr 1fr;
gap:14px;
margin-top:16px
}

.card{
background:white;
padding:20px;
border-radius:14px
}

.label{
color:#64748b;
font-size:13px
}

.value{
font-size:27px;
font-weight:bold;
margin-top:10px
}

.note{
font-size:12px;
color:#64748b;
line-height:1.5;
margin-top:18px
}

@media(max-width:500px){
.grid{
grid-template-columns:1fr
}
}

</style>

</head>

<body>

<main>

<header>

<h1>Asthma Monitor</h1>

<small>
Environmental monitoring prototype
</small>

</header>


<div class="grid">

<div class="card">

<div class="label">
Temperature
</div>

<div class="value" id="temp">
--
</div>

</div>


<div class="card">

<div class="label">
Humidity
</div>

<div class="value" id="hum">
--
</div>

</div>


<div class="card">

<div class="label">
PM2.5
</div>

<div class="value" id="pm">
--
</div>

<div class="label">
Simulated µg/m³
</div>

</div>


<div class="card">

<div class="label">
PM10
</div>

<div class="value" id="pm10">
--
</div>

<div class="label">
Simulated µg/m³
</div>

</div>


<div class="card" style="grid-column:1/-1">

<div class="label">
Experimental indicator
</div>

<div class="value" id="risk">
WAITING
</div>

<div id="sensor">
Waiting for sensor data
</div>

</div>

</div>


<div id="updated" style="margin-top:15px;font-size:12px">
Connecting to device...
</div>


<div class="note">

PM values are simulated. The indicator uses experimental
rules and is not a medically validated asthma-attack
prediction or diagnosis. Do not use it for medical decisions.

</div>

</main>


<script>

async function update(){

try{

const response =
await fetch('/data',{cache:'no-store'});

const d =
await response.json();


document.getElementById('temp').textContent =
d.temperature===null
?'--'
:d.temperature.toFixed(1)+' °C';


document.getElementById('hum').textContent =
d.humidity===null
?'--'
:d.humidity.toFixed(1)+' %';


document.getElementById('pm').textContent =
d.pm25.toFixed(1);


document.getElementById('pm10').textContent =
d.pm10.toFixed(1);


document.getElementById('risk').textContent =
d.risk;


document.getElementById('sensor').textContent =
d.sensorOK
?'DHT22 is reading correctly'
:'DHT22 not reading';


document.getElementById('updated').textContent =
'Last update: '+new Date().toLocaleTimeString();

}

catch(e){

document.getElementById('updated').textContent =
'Cannot reach ESP32. Check Wi-Fi.';

}

}

update();

setInterval(update,5000);

</script>

</body>

</html>
)HTML";

// =====================================================
// RISK CALCULATION
// =====================================================

void calculateRisk(){

if(!sensorOK){

risk = "SENSOR ERROR";

return;

}

int score = 0;


// Temperature

if(temperatureC >= 32)
score += 2;

else if(temperatureC >= 29)
score += 1;


// Humidity

if(humidity >= 80)
score += 2;

else if(humidity >= 70)
score += 1;


// PM2.5

if(pm25 >= 55)
score += 3;

else if(pm25 >= 35)
score += 2;

else if(pm25 >= 15)
score += 1;


// Final risk

if(score <= 1)
risk = "LOW";

else if(score <= 3)
risk = "MODERATE";

else if(score <= 5)
risk = "HIGH";

else
risk = "CRITICAL";

}

// =====================================================
// ROUTES
// =====================================================

void handleHome(){

server.send_P(
200,
"text/html",
PAGE
);

}


void handleData(){

String json = "{";

json += "\"temperature\":";

json += sensorOK
? String(temperatureC,1)
: "null";


json += ",\"humidity\":";

json += sensorOK
? String(humidity,1)
: "null";


json += ",\"pm25\":";
json += String(pm25,1);


json += ",\"pm10\":";
json += String(pm10,1);


json += ",\"risk\":\"";
json += risk;
json += "\"";


json += ",\"sensorOK\":";

json += sensorOK
? "true"
: "false";


json += "}";


server.sendHeader(
"Cache-Control",
"no-store"
);


server.send(
200,
"application/json",
json
);

}

// =====================================================
// SETUP
// =====================================================

void setup(){

Serial.begin(115200);

delay(1000);


Serial.println();
Serial.println();

Serial.println(
"================================"
);

Serial.println(
"ASTHMA MONITOR STARTING"
);

Serial.println(
"================================"
);


// -----------------------------------------------------
// DHT22
// -----------------------------------------------------

Serial.println(
"STEP 1: Starting DHT22..."
);

dht.begin();

delay(500);

Serial.println(
"STEP 1 OK"
);


// -----------------------------------------------------
// WIFI MODE
// -----------------------------------------------------

Serial.println(
"STEP 2: Setting WiFi mode..."
);

WiFi.mode(WIFI_STA);

Serial.println(
"STEP 2 OK"
);


// -----------------------------------------------------
// WIFI SLEEP
// -----------------------------------------------------

Serial.println(
"STEP 3: Disabling WiFi sleep..."
);

WiFi.setSleep(false);

Serial.println(
"STEP 3 OK"
);


// -----------------------------------------------------
// CONNECT TO IPHONE
// -----------------------------------------------------

Serial.println();
Serial.println(
"STEP 4: Connecting to iPhone hotspot..."
);

Serial.print(
"SSID: "
);

Serial.println(
WIFI_SSID
);


WiFi.begin(
WIFI_SSID,
WIFI_PASSWORD
);


Serial.print(
"Connecting"
);


unsigned long start =
millis();


while(
WiFi.status() != WL_CONNECTED &&
millis() - start < 15000
){

delay(500);

Serial.print(".");

}


Serial.println();


// -----------------------------------------------------
// CHECK CONNECTION
// -----------------------------------------------------

if(WiFi.status() == WL_CONNECTED){

Serial.println();
Serial.println(
"================================"
);

Serial.println(
"IPHONE HOTSPOT CONNECTED!"
);

Serial.print(
"ESP32 IP address: "
);

Serial.println(
WiFi.localIP()
);

Serial.print(
"Signal strength: "
);

Serial.print(
WiFi.RSSI()
);

Serial.println(
" dBm"
);

Serial.println(
"================================"
);

}

else{

// -----------------------------------------------------
// IPHONE CONNECTION FAILED
// -----------------------------------------------------

Serial.println();

Serial.println(
"iPhone hotspot connection failed."
);

Serial.println(
"Starting backup WiFi..."
);


// Do NOT use WiFi.disconnect(true)
// because it is unnecessary here.

WiFi.mode(WIFI_AP);

delay(500);


if(
WiFi.softAP(
BACKUP_SSID,
BACKUP_PASSWORD
)
){

Serial.println(
"================================"
);

Serial.println(
"BACKUP WIFI STARTED"
);

Serial.print(
"WiFi name: "
);

Serial.println(
BACKUP_SSID
);

Serial.print(
"WiFi password: "
);

Serial.println(
BACKUP_PASSWORD
);

Serial.print(
"Dashboard URL: http://"
);

Serial.println(
WiFi.softAPIP()
);

Serial.println(
"================================"
);

}

else{

Serial.println(
"ERROR: Backup WiFi failed!"
);

}

}


// -----------------------------------------------------
// WEB SERVER
// -----------------------------------------------------

Serial.println();

Serial.println(
"STEP 5: Starting web server..."
);


server.on(
"/",
handleHome
);


server.on(
"/data",
handleData
);


server.begin();


Serial.println(
"WEB SERVER STARTED"
);


// -----------------------------------------------------
// FINAL NETWORK MESSAGE
// -----------------------------------------------------

if(
WiFi.status() == WL_CONNECTED
){

Serial.print(
"Open dashboard at: http://"
);

Serial.println(
WiFi.localIP()
);

}

else if(
WiFi.getMode() == WIFI_AP
){

Serial.print(
"Connect to WiFi: "
);

Serial.println(
BACKUP_SSID
);

Serial.print(
"Then open: http://"
);

Serial.println(
WiFi.softAPIP()
);

}


Serial.println();

Serial.println(
"SETUP COMPLETE"
);

Serial.println(
"================================"
);

}

// =====================================================
// MAIN LOOP
// =====================================================

void loop(){

// Keep web server running

server.handleClient();


unsigned long now =
millis();


// =====================================================
// DHT22
// Every 5 seconds
// =====================================================

if(now - lastDHT >= 5000){

lastDHT = now;


float t =
dht.readTemperature();


float h =
dht.readHumidity();


if(
!isnan(t) &&
!isnan(h)
){

temperatureC = t;

humidity = h;

sensorOK = true;


Serial.printf(
"Temp: %.1f C | Humidity: %.1f %%\n",
temperatureC,
humidity
);

}

else{

sensorOK = false;

Serial.println(
"DHT22 read error - check wiring"
);

}


calculateRisk();


Serial.print(
"Demo indicator: "
);

Serial.println(
risk
);

}


// =====================================================
// SIMULATED PM VALUES
// Every 5 seconds
// =====================================================

if(now - lastPM >= 5000){

lastPM = now;


sampleIndex =
(sampleIndex + 1) % SAMPLE_COUNT;


pm25 =
pm25Values[sampleIndex];


pm10 =
pm10Values[sampleIndex];


calculateRisk();


Serial.print(
"Simulated PM2.5: "
);

Serial.print(
pm25
);

Serial.print(
" | PM10: "
);

Serial.println(
pm10
);

}


delay(2);

}
