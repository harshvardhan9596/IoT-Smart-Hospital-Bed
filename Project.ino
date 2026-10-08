#include <WiFi.h>
#include <Wire.h>
#include <WebServer.h>
#include <math.h>

// Function declarations
void handleRoot();
void handleData();
int readIRStable(int pin);

// WiFi
const char* ssid = "One Plus";
const char* password = "12345678";

WebServer server(80);

// MPU
int16_t AcX, AcY, AcZ;

// IR pins
#define IR1 14
#define IR2 27

// LM35
#define LM35 34

// LED
#define LED 25

float temperature = 0;

int status = 0;
String tilt = "Stable";
String arrow = "⬤";

void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);
  pinMode(LED, OUTPUT);

  Wire.begin(21, 22);
  delay(500);

  // Wake MPU6050
  Wire.beginTransmission(0x68);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  Serial.println("Connected!");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);

  server.begin();
}

void loop() {
  server.handleClient();

  // MPU read
  Wire.beginTransmission(0x68);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, 6, true);

  AcX = Wire.read() << 8 | Wire.read();
  AcY = Wire.read() << 8 | Wire.read();
  AcZ = Wire.read() << 8 | Wire.read();

  // IR read (stable)
  int ir1 = readIRStable(IR1);
  int ir2 = readIRStable(IR2);

  // LM35 read
  int value = analogRead(LM35);
  float voltage = value * (3.3 / 4095.0);
  temperature = voltage * 120.0;

  float total = sqrt(AcX*AcX + AcY*AcY + AcZ*AcZ);

  // STATUS LOGIC
  if (ir1 == LOW && ir2 == LOW) {
    if (total < 14000) status = 4; // FALL
    else if (abs(AcX) > 8000 || abs(AcY) > 8000) status = 2; // MOVING
    else status = 1; // ON BED
  }
  else if (ir1 == HIGH && ir2 == HIGH) {
    status = 3; // LEFT BED
  }
  else {
    status = 2; // PARTIAL MOVEMENT
  }

  // TILT
  if (AcX > 6000) { tilt = "Tilt Right"; arrow = "➡️"; }
  else if (AcX < -6000) { tilt = "Tilt Left"; arrow = "⬅️"; }
  else if (AcY > 6000) { tilt = "Tilt Forward"; arrow = "⬆️"; }
  else if (AcY < -6000) { tilt = "Tilt Backward"; arrow = "⬇️"; }
  else { tilt = "Stable"; arrow = "⬤"; }

  // 🔴 FINAL LED LOGIC
  if (status == 4 || status == 3 || temperature > 38) {
    digitalWrite(LED, HIGH);
  } 
  else {
    digitalWrite(LED, LOW);
  }

  // Debug
  Serial.print("Temp: "); Serial.print(temperature);
  Serial.print(" IR1: "); Serial.print(ir1);
  Serial.print(" IR2: "); Serial.print(ir2);
  Serial.print(" STATUS: "); Serial.println(status);

  delay(200);
}

// Stable IR
int readIRStable(int pin) {
  int count = 0;
  for (int i = 0; i < 5; i++) {
    if (digitalRead(pin) == LOW) count++;
    delay(2);
  }
  return (count >= 3) ? LOW : HIGH;
}

// 🌐 Dashboard
void handleRoot() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width">
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

<style>
body{text-align:center;font-family:Arial;background:#f4f6f9;}
.box{padding:15px;color:white;font-size:22px;border-radius:10px;margin:10px;}
canvas{max-width:90%;height:200px;}
</style>
</head>

<body>

<h2>Smart Hospital Dashboard</h2>

<div id="statusBox" class="box">Loading...</div>

<h3>Patient Tilt</h3>
<h1 id="arrow">⬤</h1>
<p id="tilt">Stable</p>

<h3>Temperature</h3>
<p id="temp">-- °C</p>

<canvas id="chart" width="250" height="120"></canvas>

<script>
let x=[], y=[], z=[];

const ctx = document.getElementById('chart');
const chart = new Chart(ctx, {
  type: 'line',
  data: {
    labels: [],
    datasets: [
      {label:'X', data:x, borderColor:'red'},
      {label:'Y', data:y, borderColor:'green'},
      {label:'Z', data:z, borderColor:'blue'}
    ]
  }
});

function updateUI(d){

  let text="", color="";

  if(d.status==1){ text="Patient ON Bed"; color="green"; }
  else if(d.status==2){ text="Patient Moving"; color="orange"; }
  else if(d.status==3){ text="Patient LEFT Bed"; color="red"; }
  else{ text="FALL DETECTED"; color="red"; }

  document.getElementById("statusBox").innerHTML = text;
  document.getElementById("statusBox").style.background = color;

  document.getElementById("tilt").innerHTML = d.tilt;
  document.getElementById("arrow").innerHTML = d.arrow;

  document.getElementById("temp").innerHTML = d.temp + " °C";
}

function updateData(){
  fetch('/data')
  .then(res=>res.json())
  .then(d=>{

    updateUI(d);

    if(x.length>20){
      x.shift(); y.shift(); z.shift();
      chart.data.labels.shift();
    }

    x.push(d.x);
    y.push(d.y);
    z.push(d.z);

    chart.data.labels.push('');
    chart.update();

    // Voice alerts
    if(d.status==4){
      speechSynthesis.speak(
        new SpeechSynthesisUtterance("Fall detected")
      );
    }

    if(d.temp > 38){
      speechSynthesis.speak(
        new SpeechSynthesisUtterance("High temperature detected")
      );
    }
  });
}

setInterval(updateData,1000);
</script>

</body>
</html>
)rawliteral";

  server.send(200,"text/html",html);
}

// 📡 API
void handleData() {
  String json = "{";
  json += "\"x\":" + String(AcX) + ",";
  json += "\"y\":" + String(AcY) + ",";
  json += "\"z\":" + String(AcZ) + ",";
  json += "\"status\":" + String(status) + ",";
  json += "\"tilt\":\"" + tilt + "\",";
  json += "\"arrow\":\"" + arrow + "\",";
  json += "\"temp\":" + String(temperature);
  json += "}";

  server.send(200,"application/json",json);
}
