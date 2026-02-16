#include <SPI.h>
#include <LoRa.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoOTA.h>

// --- KONFIGURACJA SIECI ---
const char* ssid = "Skaner_SX1278_Pro_3D";
const char* password = "password123";

// --- PINY SX1278 ---
const int csPin = 15;    // NSS (D8)
const int resetPin = 5;  // RST (D1)
const int irqPin = 4;    // DIO0 (D2)

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// --- WIZUALIZACJA: PANEL INFO + ZBALANSOWANY SZUM ---
const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>3D RF PRO SCANNER</title>
    <style>
        body { margin: 0; background: #000; overflow: hidden; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; color: #0ff; }
        
        /* Panel Informacyjny */
        #info-panel { 
            position: absolute; top: 20px; left: 20px; z-index: 1000; 
            background: rgba(0, 20, 20, 0.8); padding: 15px; border-left: 3px solid #0ff;
            pointer-events: none; box-shadow: 0 0 20px rgba(0,255,255,0.2);
        }
        .label { font-size: 10px; color: #088; text-transform: uppercase; letter-spacing: 1px; }
        .stat { font-size: 16px; font-weight: bold; color: #fff; margin-bottom: 8px; }

        #container { width: 100vw; height: 100vh; perspective: 1200px; display: flex; align-items: center; justify-content: center; }
        
        #world { 
            width: 800px; height: 400px; 
            transform-style: preserve-3d; 
            transform: rotateX(65deg) rotateZ(-10deg); 
            position: relative;
        }

        #grid { 
            position: absolute; width: 100%; height: 100%; border: 1px solid #044;
            background-image: linear-gradient(rgba(0,255,255,0.05) 1px, transparent 1px), linear-gradient(90deg, rgba(0,255,255,0.05) 1px, transparent 1px);
            background-size: 40px 40px; 
        }

        .needle { 
            position: absolute; bottom: 50%; width: 2px; 
            transform-origin: bottom; 
            transform: rotateX(-90deg); 
            transition: height 0.04s linear;
        }
    </style>
</head>
<body>
    <div id="info-panel">
        <div class="label">Częstotliwość</div>
        <div class="stat">433.92 MHz</div>
        <div class="label">Szerokość Pasma</div>
        <div class="stat">500.00 kHz</div>
        <div class="label">Adres IP</div>
        <div class="stat">192.168.4.1</div>
        <div class="label">Status / RSSI</div>
        <div id="status" class="stat" style="color:#0f0">LIVE: <span id="val">0</span></div>
    </div>

    <div id="container">
        <div id="world"><div id="grid"></div></div>
    </div>

    <script>
        const world = document.getElementById('world');
        const SEGMENTS = 90;
        let needles = [];
        let data = new Array(SEGMENTS).fill(10);
        let noiseFloor = 40; // Domyślny punkt startowy

        // Tworzenie igieł
        for(let i=0; i<SEGMENTS; i++) {
            let n = document.createElement('div');
            n.className = 'needle';
            n.style.left = (i * (800 / SEGMENTS)) + "px";
            n.style.background = "rgba(0, 255, 255, 0.3)";
            world.appendChild(n);
            needles.push(n);
        }

        // Sterowanie obrotem (LPM)
        window.onmousemove = (e) => {
            if(e.buttons === 1) {
                let rX = 40 + (e.clientY / window.innerHeight) * 50;
                let rZ = (e.clientX / window.innerWidth - 0.5) * 120;
                world.style.transform = `rotateX(${rX}deg) rotateZ(${rZ}deg)`;
            }
        };

        let ws = new WebSocket('ws://' + window.location.hostname + ':81');
        ws.binaryType = "arraybuffer";
        ws.onmessage = (e) => {
            let raw = new Uint8Array(e.data)[0];
            document.getElementById('val').innerText = raw;
            
            data.shift();
            
            // LOGIKA SKALOWANIA:
            // Szum tła (ok. 40) ma dawać h = ok. 15px
            // Impuls (ok. 80) ma dawać h = ok. 350px
            let h = (raw - 38) * 8.0; 
            
            // Minimalna wysokość, aby szum był widoczny, ale niski
            if(h < 12) h = 12 + (Math.random() * 8); 
            if(h > 450) h = 450;
            
            data.push(h);

            for(let i=0; i<SEGMENTS; i++) {
                let v = data[i];
                needles[i].style.height = v + "px";
                
                // Wizualizacja: szum jest ciemniejszy, impuls jaśnieje i puchnie
                if(v > 50) {
                    needles[i].style.background = "linear-gradient(to top, #0ff, #fff)";
                    needles[i].style.width = "4px";
                    needles[i].style.boxShadow = "0 0 15px #0ff";
                } else {
                    needles[i].style.background = "rgba(0, 255, 255, 0.25)";
                    needles[i].style.width = "2px";
                    needles[i].style.boxShadow = "none";
                }
            }
        };
    </script>
</body>
</html>
)=====";

void writeRegister(uint8_t address, uint8_t value) {
    digitalWrite(csPin, LOW);
    SPI.transfer(address | 0x80);
    SPI.transfer(value);
    digitalWrite(csPin, HIGH);
}

void setup() {
    Serial.begin(115200);
    
    // Sieć AP
    WiFi.softAP(ssid, password);

    // LoRa Start
    LoRa.setPins(csPin, resetPin, irqPin);
    if (!LoRa.begin(433.92E6)) {
        while (1) { delay(1000); }
    }

    LoRa.setSignalBandwidth(500E3);
    LoRa.setGain(6); 
    writeRegister(0x01, 0x85); // Mode: Continuous RSSI read

    // WebServer & WebSocket
    server.on("/", []() { server.send(200, "text/html", INDEX_HTML); });
    server.begin();
    webSocket.begin();

    // OTA - Wi-Fi Update
    ArduinoOTA.setHostname("Skaner-RF-3D-Pro");
    ArduinoOTA.begin();
}

void loop() {
    server.handleClient();
    webSocket.loop();
    ArduinoOTA.handle();

    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 30) {
        lastUpdate = millis();
        digitalWrite(csPin, LOW);
        SPI.transfer(0x1B); 
        uint8_t raw = SPI.transfer(0x00);
        digitalWrite(csPin, HIGH);
        
        webSocket.broadcastBIN(&raw, 1);
    }
}
