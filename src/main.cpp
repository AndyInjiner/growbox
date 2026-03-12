/*
 * Проект: Система управления гроубоксом на ESP32
 * Версия: 1.0
 * 
 * Функционал:
 * - 7 режимов работы с настраиваемыми порогами
 * - TFT дисплей с меню
 * - Web server/client для удалённого управления
 * - Датчики: влажности воздуха, почвы, температуры
 * - Управление: вентиляция, освещение (PWM), насосы, обогрев, зуммер
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <TFT_eSPI.h>
#include <OneButton.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Preferences.h>

// ========== ПИНЫ (из platformio.ini) ==========
#ifndef DHT_AIR_PIN
#define DHT_AIR_PIN 32
#endif
#ifndef DHT_OUT_PIN
#define DHT_OUT_PIN 33
#endif
#ifndef SOIL_MOIST_PIN
#define SOIL_MOIST_PIN 34
#endif
#ifndef BTN_MODE
#define BTN_MODE 14
#endif
#ifndef BTN_CONFIRM
#define BTN_CONFIRM 15
#endif
#ifndef VENT_RELAY
#define VENT_RELAY 2
#endif
#ifndef LIGHT_PWM
#define LIGHT_PWM 12
#endif
#ifndef PUMP_OUT_PIN
#define PUMP_OUT_PIN 4
#endif
#ifndef PUMP_SOIL_PIN
#define PUMP_SOIL_PIN 13
#endif
#ifndef HEATER_RELAY
#define HEATER_RELAY 25
#endif
#ifndef BUZZER_PIN
#define BUZZER_PIN 26
#endif

// ========== КОНСТАНТЫ ==========
#define DHT_TYPE DHT22
#define ADC_MAX 4095
#define SOIL_MIN_ADC 2000  // 0% влажности (сухой)
#define SOIL_MAX_ADC 3000  // 100% влажности (вода)

// Режимы работы
enum Mode : uint8_t {
    MODE_VENT_HUM_TEMP = 0,      // 1. Вентиляция по влажности/температуре
    MODE_VENT_PERIODIC,          // 2. Вентиляция периодическая
    MODE_LIGHT_SUNRISE_SUNSET,   // 3. Освещение восход/закат
    MODE_PUMP_OUT_HUM,           // 4. Насос наружный по влажности
    MODE_PUMP_OUT_PERIODIC,      // 5. Насос наружный периодический
    MODE_PUMP_SOIL_HUM,          // 6. Насос почвы по влажности
    MODE_HEATER_TEMP,            // 7. Обогрев по температуре
    MODE_COUNT
};

// Состояния системы
enum SystemState : uint8_t {
    STATE_IDLE,
    STATE_SENSORS_VIEW,
    STATE_MODE_SELECT,
    STATE_MODE_CONFIG
};

// ========== ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ==========
TFT_eSPI tft = TFT_eSPI();
DHT dhtAir(DHT_AIR_PIN, DHT_TYPE);
DHT dhtOut(DHT_OUT_PIN, DHT_TYPE);
OneButton btnMode(BTN_MODE, true);
OneButton btnConfirm(BTN_CONFIRM, true);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, 0, 60000);
Preferences preferences;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ========== ПЕРЕМЕННЫЕ ==========
// Сенсоры
float tempAirIn = 0.0f;
float humAirIn = 0.0f;
float tempAirOut = 0.0f;
float humAirOut = 0.0f;
int soilMoistRaw = 0;
float soilMoistPercent = 0.0f;
unsigned long lastSensorRead = 0;

// Режимы
Mode currentMode = MODE_VENT_HUM_TEMP;
bool modeActive[MODE_COUNT] = {false};
SystemState sysState = STATE_IDLE;

// Пороги и настройки режимов
struct ModeSettings {
    float humThreshold = 70.0f;      // Порог влажности
    float tempThreshold = 25.0f;     // Порог температуры
    uint16_t intervalMinutes = 120;  // Интервал (мин)
    uint16_t durationMinutes = 5;    // Длительность (мин)
    uint16_t sunriseHour = 6;        // Час восхода
    uint16_t sunsetHour = 20;        // Час заката
    float soilHumLow = 30.0f;        // Низкий порог влажности почвы
    float soilHumHigh = 80.0f;       // Высокий порог влажности почвы
    float heaterThreshold = 10.0f;   // Порог температуры для обогрева
} modeSettings[MODE_COUNT];

// Таймеры режимов
unsigned long modeTimers[MODE_COUNT] = {0};
bool modeOutputState[MODE_COUNT] = {false};

// WiFi
String wifiSSID = "";
String wifiPass = "";
bool wifiConnected = false;
bool apMode = true;  // true = локальный режим, false = интернет

// TFT
unsigned long sensorsViewTimeout = 0;
uint8_t modeSelectIndex = 0;

// ========== ПРОТОТИПЫ ФУНКЦИЙ ==========
void initPins();
void initSensors();
void initTFT();
void initWiFi();
void initWebServer();
void initWebSocket();
void readSensors();
void updateTFT();
void updateModes();
void processButtons();
void beepShort();
void beepSiren();
void setOutput(uint8_t output, bool state);
String getModeName(Mode mode);
String formatTime(time_t t);
void saveSettings();
void loadSettings();

// ========== SETUP ==========
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== Grobox System Start ===");
    
    initPins();
    initSensors();
    initTFT();
    loadSettings();
    initWiFi();
    initWebSocket();
    initWebServer();
    
    timeClient.begin();
    timeClient.setUpdateInterval(60000);
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Grobox System");
    tft.println("Initializing...");
    
    beepShort();
    delay(500);
    beepShort();
}

// ========== LOOP ==========
void loop() {
    timeClient.update();
    readSensors();
    processButtons();
    updateModes();
    updateTFT();
    ws.cleanupClients();
    
    delay(10);
}

// ========== ИНИЦИАЛИЗАЦИЯ ==========
void initPins() {
    // Выходы
    pinMode(VENT_RELAY, OUTPUT);
    pinMode(LIGHT_PWM, OUTPUT);
    pinMode(PUMP_OUT_PIN, OUTPUT);
    pinMode(PUMP_SOIL_PIN, OUTPUT);
    pinMode(HEATER_RELAY, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    
    // Кнопки
    pinMode(BTN_MODE, INPUT_PULLUP);
    pinMode(BTN_CONFIRM, INPUT_PULLUP);
    
    // Выключаем всё
    digitalWrite(VENT_RELAY, LOW);
    digitalWrite(PUMP_OUT_PIN, LOW);
    digitalWrite(PUMP_SOIL_PIN, LOW);
    digitalWrite(HEATER_RELAY, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    ledcAttachPin(LIGHT_PWM, 0);
    ledcSetup(0, 5000, 8);
    ledcWrite(0, 0);
    
    // Обработчики кнопок
    btnMode.attachClick([]() {
        beepShort();
        switch(sysState) {
            case STATE_IDLE:
                sysState = STATE_SENSORS_VIEW;
                sensorsViewTimeout = millis() + 10000;
                break;
            case STATE_SENSORS_VIEW:
                sysState = STATE_MODE_SELECT;
                modeSelectIndex = currentMode;
                break;
            case STATE_MODE_SELECT:
                if (modeActive[modeSelectIndex]) {
                    sysState = STATE_MODE_CONFIG;
                } else {
                    currentMode = modeSelectIndex;
                    modeActive[currentMode] = true;
                    sysState = STATE_IDLE;
                    saveSettings();
                }
                break;
            case STATE_MODE_CONFIG:
                modeActive[modeSelectIndex] = false;
                sysState = STATE_IDLE;
                saveSettings();
                break;
        }
    });
    
    btnMode.attachLongPressStart([]() {
        beepShort();
        if (sysState == STATE_MODE_SELECT) {
            modeSelectIndex = (modeSelectIndex + 1) % MODE_COUNT;
        }
    });
    
    btnConfirm.attachClick([]() {
        beepShort();
        if (sysState == STATE_SENSORS_VIEW) {
            sysState = STATE_IDLE;
        }
    });
    
    btnConfirm.attachLongPressStart([]() {
        beepShort();
        sysState = STATE_IDLE;
    });
}

void initSensors() {
    dhtAir.begin();
    dhtOut.begin();
    delay(100);
}

void initTFT() {
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
}

void initWiFi() {
    // Загружаем сохранённые настройки
    preferences.begin("grobox", false);
    wifiSSID = preferences.getString("ssid", "");
    wifiPass = preferences.getString("pass", "");
    apMode = preferences.getBool("apmode", true);
    preferences.end();
    
    if (!wifiSSID.isEmpty()) {
        Serial.printf("Connecting to WiFi: %s\n", wifiSSID.c_str());
        WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            apMode = false;
            Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("\nWiFi connect failed, starting AP");
        }
    }
    
    if (!wifiConnected) {
        // Создаём точку доступа
        WiFi.softAP("Grobox_AP", "grobox123");
        Serial.printf("AP started: Grobox_AP, IP: %s\n", WiFi.softAPIP().toString().c_str());
        wifiConnected = true;
    }
}

void initWebServer() {
    // Главная страница
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Grobox Control</title>
    <style>
        body { font-family: Arial, sans-serif; background: #1a1a2e; color: #eee; margin: 0; padding: 20px; }
        .container { max-width: 800px; margin: 0 auto; }
        h1 { text-align: center; color: #00d9ff; }
        .card { background: #16213e; border-radius: 10px; padding: 15px; margin: 10px 0; }
        .sensors { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
        .sensor { background: #0f3460; padding: 10px; border-radius: 5px; text-align: center; }
        .sensor-value { font-size: 24px; color: #00d9ff; }
        .sensor-label { font-size: 12px; color: #aaa; }
        .modes { display: grid; grid-template-columns: repeat(auto-fit, minmax(100px, 1fr)); gap: 8px; margin-top: 15px; }
        .mode-btn { background: #0f3460; border: 2px solid #00d9ff; color: #fff; padding: 10px; border-radius: 5px; cursor: pointer; }
        .mode-btn.active { background: #00d9ff; color: #000; }
        .mode-config { display: none; background: #16213e; border-radius: 10px; padding: 15px; margin-top: 10px; }
        .mode-config.show { display: block; }
        .slider-container { margin: 10px 0; }
        .slider { width: 100%; }
        input[type='number'], input[type='time'] { background: #0f3460; border: 1px solid #00d9ff; color: #fff; padding: 8px; border-radius: 5px; }
        .btn { background: #00d9ff; color: #000; border: none; padding: 10px 20px; border-radius: 5px; cursor: pointer; margin: 5px; }
        .wifi-form { margin-top: 20px; }
        .wifi-form input { display: block; margin: 10px 0; width: 100%; box-sizing: border-box; }
        .tabs { display: flex; gap: 10px; margin-bottom: 20px; }
        .tab { flex: 1; text-align: center; padding: 10px; background: #0f3460; border-radius: 5px; cursor: pointer; }
        .tab.active { background: #00d9ff; color: #000; }
    </style>
</head>
<body>
    <div class='container'>
        <h1>🌱 Grobox Control</h1>
        
        <div class='tabs'>
            <div class='tab active' onclick='showTab("main")'>Основной</div>
            <div class='tab' onclick='showTab("wifi")'>WiFi</div>
        </div>
        
        <div id='main-tab'>
            <div class='card'>
                <h3>📊 Датчики</h3>
                <div class='sensors'>
                    <div class='sensor'>
                        <div class='sensor-value' id='temp-in'>--</div>
                        <div class='sensor-label'>Температура внутри (°C)</div>
                    </div>
                    <div class='sensor'>
                        <div class='sensor-value' id='hum-in'>--</div>
                        <div class='sensor-label'>Влажность внутри (%)</div>
                    </div>
                    <div class='sensor'>
                        <div class='sensor-value' id='temp-out'>--</div>
                        <div class='sensor-label'>Температура снаружи (°C)</div>
                    </div>
                    <div class='sensor'>
                        <div class='sensor-value' id='hum-out'>--</div>
                        <div class='sensor-label'>Влажность снаружи (%)</div>
                    </div>
                    <div class='sensor'>
                        <div class='sensor-value' id='soil'>--</div>
                        <div class='sensor-label'>Влажность почвы (%)</div>
                    </div>
                </div>
            </div>
            
            <div class='card'>
                <h3>⚙️ Режимы работы</h3>
                <div class='modes' id='mode-buttons'></div>
            </div>
            
            <div class='card mode-config' id='mode-config-panel'>
                <h3 id='config-title'>Настройка режима</h3>
                <div id='config-content'></div>
                <button class='btn' onclick='saveConfig()'>Сохранить</button>
                <button class='btn' onclick='toggleMode()'>Вкл/Выкл режим</button>
                <button class='btn' onclick='closeConfig()'>Закрыть</button>
            </div>
        </div>
        
        <div id='wifi-tab' style='display:none;'>
            <div class='card wifi-form'>
                <h3>📡 Подключение к WiFi</h3>
                <input type='text' id='ssid' placeholder='SSID сети'>
                <input type='password' id='pass' placeholder='Пароль'>
                <button class='btn' onclick='connectWiFi()'>Подключиться</button>
            </div>
        </div>
    </div>
    
    <script>
        const MODE_NAMES = [
            'Вентиляция (влажность/темпер.)',
            'Вентиляция (периодическая)',
            'Освещение (восход/закат)',
            'Насос наружный (влажность)',
            'Насос наружный (период.)',
            'Насос почвы (влажность)',
            'Обогрев (температура)'
        ];
        
        let ws;
        let currentConfigMode = null;
        let modeSettings = {};
        let modeActive = [];
        
        function connectWS() {
            ws = new WebSocket('ws://' + window.location.hostname + '/ws');
            ws.onopen = () => console.log('WS connected');
            ws.onmessage = (e) => {
                const data = JSON.parse(e.data);
                if (data.type === 'sensors') {
                    document.getElementById('temp-in').textContent = data.tempIn.toFixed(1);
                    document.getElementById('hum-in').textContent = data.humIn.toFixed(1);
                    document.getElementById('temp-out').textContent = data.tempOut.toFixed(1);
                    document.getElementById('hum-out').textContent = data.humOut.toFixed(1);
                    document.getElementById('soil').textContent = data.soil.toFixed(1);
                } else if (data.type === 'modes') {
                    modeActive = data.active;
                    modeSettings = data.settings;
                    renderModeButtons();
                }
            };
            ws.onclose = () => setTimeout(connectWS, 2000);
        }
        
        function renderModeButtons() {
            const container = document.getElementById('mode-buttons');
            container.innerHTML = '';
            MODE_NAMES.forEach((name, i) => {
                const btn = document.createElement('button');
                btn.className = 'mode-btn' + (modeActive[i] ? ' active' : '');
                btn.textContent = (i + 1) + '. ' + name.split('(')[0];
                btn.onclick = () => openConfig(i);
                container.appendChild(btn);
            });
        }
        
        function openConfig(modeIndex) {
            currentConfigMode = modeIndex;
            const panel = document.getElementById('mode-config-panel');
            const title = document.getElementById('config-title');
            const content = document.getElementById('config-content');
            
            title.textContent = MODE_NAMES[modeIndex];
            
            let html = '';
            const s = modeSettings[modeIndex] || {};
            
            if (s.humThreshold !== undefined) {
                html += '<div class="slider-container"><label>Порог влажности (%): <span id="hum-val">' + (s.humThreshold||70) + '</span></label>';
                html += '<input type="range" class="slider" min="0" max="100" value="' + (s.humThreshold||70) + '" oninput="document.getElementById(\'hum-val\').textContent=this.value" data-key="humThreshold"></div>';
            }
            if (s.tempThreshold !== undefined) {
                html += '<div class="slider-container"><label>Порог температуры (°C): <span id="temp-val">' + (s.tempThreshold||25) + '</span></label>';
                html += '<input type="range" class="slider" min="0" max="50" value="' + (s.tempThreshold||25) + '" oninput="document.getElementById(\'temp-val\').textContent=this.value" data-key="tempThreshold"></div>';
            }
            if (s.intervalMinutes !== undefined) {
                html += '<div class="slider-container"><label>Интервал (мин): </label><input type="number" value="' + (s.intervalMinutes||120) + '" data-key="intervalMinutes"></div>';
            }
            if (s.durationMinutes !== undefined) {
                html += '<div class="slider-container"><label>Длительность (мин): </label><input type="number" value="' + (s.durationMinutes||5) + '" data-key="durationMinutes"></div>';
            }
            if (s.sunriseHour !== undefined) {
                html += '<div class="slider-container"><label>Восход: </label><input type="time" value="' + (String(s.sunriseHour||6).padStart(2,'0') + ':00') + '" data-key="sunriseHour"></div>';
            }
            if (s.sunsetHour !== undefined) {
                html += '<div class="slider-container"><label>Закат: </label><input type="time" value="' + (String(s.sunsetHour||20).padStart(2,'0') + ':00') + '" data-key="sunsetHour"></div>';
            }
            if (s.soilHumLow !== undefined) {
                html += '<div class="slider-container"><label>Низкий порог почвы (%): </label><input type="number" value="' + (s.soilHumLow||30) + '" data-key="soilHumLow"></div>';
            }
            if (s.soilHumHigh !== undefined) {
                html += '<div class="slider-container"><label>Высокий порог почвы (%): </label><input type="number" value="' + (s.soilHumHigh||80) + '" data-key="soilHumHigh"></div>';
            }
            if (s.heaterThreshold !== undefined) {
                html += '<div class="slider-container"><label>Порог обогрева (°C): </label><input type="number" value="' + (s.heaterThreshold||10) + '" data-key="heaterThreshold"></div>';
            }
            
            if (!html) {
                html = '<p>⚠️ Сначала установите пороги включения и время работы а далее активируйте режим</p>';
            }
            
            content.innerHTML = html;
            panel.classList.add('show');
        }
        
        function saveConfig() {
            if (currentConfigMode === null) return;
            const content = document.getElementById('config-content');
            const inputs = content.querySelectorAll('input');
            const settings = {};
            
            inputs.forEach(input => {
                const key = input.dataset.key;
                if (key) {
                    let val = input.type === 'number' ? parseFloat(input.value) : input.value;
                    if (key.includes('Hour') && typeof val === 'string') {
                        val = parseInt(val.split(':')[0]);
                    }
                    settings[key] = val;
                }
            });
            
            ws.send(JSON.stringify({type: 'settings', mode: currentConfigMode, settings: settings}));
            alert('Настройки сохранены!');
        }
        
        function toggleMode() {
            if (currentConfigMode === null) return;
            ws.send(JSON.stringify({type: 'toggle', mode: currentConfigMode}));
        }
        
        function closeConfig() {
            document.getElementById('mode-config-panel').classList.remove('show');
            currentConfigMode = null;
        }
        
        function showTab(tab) {
            document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
            document.querySelectorAll('.tab')[tab === 'main' ? 0 : 1].classList.add('active');
            document.getElementById('main-tab').style.display = tab === 'main' ? 'block' : 'none';
            document.getElementById('wifi-tab').style.display = tab === 'wifi' ? 'block' : 'none';
        }
        
        function connectWiFi() {
            const ssid = document.getElementById('ssid').value;
            const pass = document.getElementById('pass').value;
            ws.send(JSON.stringify({type: 'wifi', ssid: ssid, pass: pass}));
            alert('Попытка подключения...');
        }
        
        connectWS();
        setInterval(() => ws.send(JSON.stringify({type: 'request'})), 2000);
    </script>
</body>
</html>
)rawliteral";
        request->send(200, "text/html", html);
    });
    
    server.begin();
    Serial.println("Web server started");
}

void initWebSocket() {
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
        if (type == WS_EVT_DATA) {
            AwsFrameInfo *info = (AwsFrameInfo*)arg;
            if (info->final && info->index == 0 && info->len == len) {
                data[len] = 0;
                StaticJsonDocument<512> doc;
                DeserializationError error = deserializeJson(doc, (char*)data);
                
                if (!error) {
                    const char* type = doc["type"];
                    
                    if (strcmp(type, "request") == 0) {
                        // Отправка данных сенсоров и режимов
                        StaticJsonDocument<1024> resp;
                        resp["type"] = "sensors";
                        resp["tempIn"] = tempAirIn;
                        resp["humIn"] = humAirIn;
                        resp["tempOut"] = tempAirOut;
                        resp["humOut"] = humAirOut;
                        resp["soil"] = soilMoistPercent;
                        
                        String json;
                        serializeJson(resp, json);
                        client->text(json);
                        
                        // Отправка режимов
                        StaticJsonDocument<1024> resp2;
                        resp2["type"] = "modes";
                        JsonArray active = resp2.createNestedArray("active");
                        for (int i = 0; i < MODE_COUNT; i++) {
                            active.add(modeActive[i]);
                        }
                        JsonArray settings = resp2.createNestedArray("settings");
                        for (int i = 0; i < MODE_COUNT; i++) {
                            JsonObject s = settings.createNestedObject();
                            s["humThreshold"] = modeSettings[i].humThreshold;
                            s["tempThreshold"] = modeSettings[i].tempThreshold;
                            s["intervalMinutes"] = modeSettings[i].intervalMinutes;
                            s["durationMinutes"] = modeSettings[i].durationMinutes;
                            s["sunriseHour"] = modeSettings[i].sunriseHour;
                            s["sunsetHour"] = modeSettings[i].sunsetHour;
                            s["soilHumLow"] = modeSettings[i].soilHumLow;
                            s["soilHumHigh"] = modeSettings[i].soilHumHigh;
                            s["heaterThreshold"] = modeSettings[i].heaterThreshold;
                        }
                        json = "";
                        serializeJson(resp2, json);
                        client->text(json);
                    }
                    else if (strcmp(type, "settings") == 0) {
                        int mode = doc["mode"];
                        if (doc["settings"]["humThreshold"]) modeSettings[mode].humThreshold = doc["settings"]["humThreshold"];
                        if (doc["settings"]["tempThreshold"]) modeSettings[mode].tempThreshold = doc["settings"]["tempThreshold"];
                        if (doc["settings"]["intervalMinutes"]) modeSettings[mode].intervalMinutes = doc["settings"]["intervalMinutes"];
                        if (doc["settings"]["durationMinutes"]) modeSettings[mode].durationMinutes = doc["settings"]["durationMinutes"];
                        if (doc["settings"]["sunriseHour"]) modeSettings[mode].sunriseHour = doc["settings"]["sunriseHour"];
                        if (doc["settings"]["sunsetHour"]) modeSettings[mode].sunsetHour = doc["settings"]["sunsetHour"];
                        if (doc["settings"]["soilHumLow"]) modeSettings[mode].soilHumLow = doc["settings"]["soilHumLow"];
                        if (doc["settings"]["soilHumHigh"]) modeSettings[mode].soilHumHigh = doc["settings"]["soilHumHigh"];
                        if (doc["settings"]["heaterThreshold"]) modeSettings[mode].heaterThreshold = doc["settings"]["heaterThreshold"];
                        saveSettings();
                    }
                    else if (strcmp(type, "toggle") == 0) {
                        int mode = doc["mode"];
                        modeActive[mode] = !modeActive[mode];
                        saveSettings();
                    }
                    else if (strcmp(type, "wifi") == 0) {
                        const char* ssid = doc["ssid"];
                        const char* pass = doc["pass"];
                        if (ssid && strlen(ssid) > 0) {
                            preferences.begin("grobox", false);
                            preferences.putString("ssid", ssid);
                            preferences.putString("pass", pass);
                            preferences.putBool("apmode", false);
                            preferences.end();
                            WiFi.begin(ssid, pass);
                        }
                    }
                }
            }
        }
    });
    server.addHandler(&ws);
}

// ========== СЕНСОРЫ ==========
void readSensors() {
    if (millis() - lastSensorRead < 2000) return;
    lastSensorRead = millis();
    
    // DHT внутри
    float h = dhtAir.readHumidity();
    float t = dhtAir.readTemperature();
    if (!isnan(h) && !isnan(t)) {
        humAirIn = h;
        tempAirIn = t;
    }
    
    // DHT снаружи
    h = dhtOut.readHumidity();
    t = dhtOut.readTemperature();
    if (!isnan(h) && !isnan(t)) {
        humAirOut = h;
        tempAirOut = t;
    }
    
    // Почва
    soilMoistRaw = analogRead(SOIL_MOIST_PIN);
    soilMoistPercent = map(soilMoistRaw, SOIL_MIN_ADC, SOIL_MAX_ADC, 100, 0);
    soilMoistPercent = constrain(soilMoistPercent, 0, 100);
    
    Serial.printf("Sensors: T_in=%.1f H_in=%.1f T_out=%.1f H_out=%.1f Soil=%.1f\n",
                  tempAirIn, humAirIn, tempAirOut, humAirOut, soilMoistPercent);
}

// ========== TFT ДИСПЛЕЙ ==========
void updateTFT() {
    static SystemState lastState = STATE_IDLE;
    
    if (sysState != lastState) {
        tft.fillScreen(TFT_BLACK);
        lastState = sysState;
    }
    
    time_t epochTime = timeClient.getEpochTime();
    
    switch (sysState) {
        case STATE_IDLE:
            drawIdleScreen(epochTime);
            break;
        case STATE_SENSORS_VIEW:
            drawSensorsScreen();
            if (millis() > sensorsViewTimeout) {
                sysState = STATE_IDLE;
            }
            break;
        case STATE_MODE_SELECT:
            drawModeSelectScreen();
            break;
        case STATE_MODE_CONFIG:
            drawModeConfigScreen();
            break;
    }
}

void drawIdleScreen(time_t epochTime) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    
    // Время и дата
    char timeBuf[9];
    strftime(timeBuf, 9, "%H:%M:%S", localtime(&epochTime));
    tft.setCursor(10, 10);
    tft.print(timeBuf);
    
    char dateBuf[11];
    strftime(dateBuf, 11, "%d.%m.%Y", localtime(&epochTime));
    tft.setCursor(150, 10);
    tft.print(dateBuf);
    
    // Текст подключения
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 40);
    String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    tft.printf("Для подключения используйте: %s", ipAddr.c_str());
}

void drawSensorsScreen() {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    
    int y = 10;
    tft.setCursor(10, y); tft.printf("T внутри: %.1f C", tempAirIn);
    tft.setCursor(10, y + 25); tft.printf("T снаружи: %.1f C", tempAirOut);
    tft.setCursor(10, y + 50); tft.printf("H воздух: %.1f %%", humAirIn);
    tft.setCursor(10, y + 75); tft.printf("H почвы: %.1f %%", soilMoistPercent);
    
    // Рамка
    tft.drawRect(5, 5, 310, 110, TFT_GREEN);
}

void drawModeSelectScreen() {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    
    String name = getModeName((Mode)modeSelectIndex);
    tft.setCursor(10, 20);
    tft.println("[" + name + "]");
    
    // Виджет активности
    tft.setCursor(10, 60);
    if (modeActive[modeSelectIndex]) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.println("АКТИВЕН");
    } else {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.println("НЕ АКТИВЕН");
    }
    
    // Настройки
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 100);
    
    ModeSettings& s = modeSettings[modeSelectIndex];
    if (s.humThreshold < 100) tft.printf("Порог H: %.0f%%  ", s.humThreshold);
    if (s.tempThreshold < 50) tft.printf("Порог T: %.0fC  ", s.tempThreshold);
    if (s.intervalMinutes > 0) tft.printf("Интервал: %dмин  ", s.intervalMinutes);
    if (s.durationMinutes > 0) tft.printf("Длит: %dмин", s.durationMinutes);
}

void drawModeConfigScreen() {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    
    String name = getModeName((Mode)modeSelectIndex);
    tft.setCursor(10, 20);
    tft.println("[" + name + "]");
    
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(10, 60);
    tft.println(modeActive[modeSelectIndex] ? "АКТИВЕН" : "НЕ АКТИВЕН");
    
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 100);
    tft.println("Нажмите для выхода");
}

String getModeName(Mode mode) {
    switch (mode) {
        case MODE_VENT_HUM_TEMP: return "Вентиляция H/T";
        case MODE_VENT_PERIODIC: return "Вентиляция период.";
        case MODE_LIGHT_SUNRISE_SUNSET: return "Освещение восход/закат";
        case MODE_PUMP_OUT_HUM: return "Насос наружный H";
        case MODE_PUMP_OUT_PERIODIC: return "Насос наружный пер.";
        case MODE_PUMP_SOIL_HUM: return "Насос почвы H";
        case MODE_HEATER_TEMP: return "Обогрев T";
        default: return "Неизвестно";
    }
}

// ========== УПРАВЛЕНИЕ РЕЖИМАМИ ==========
void updateModes() {
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate < 1000) return;
    lastUpdate = millis();
    
    time_t now = timeClient.getEpochTime();
    struct tm* timeinfo = localtime(&now);
    int currentHour = timeinfo->tm_hour;
    int currentMin = timeinfo->tm_min;
    
    for (int m = 0; m < MODE_COUNT; m++) {
        if (!modeActive[m]) {
            modeOutputState[m] = false;
            continue;
        }
        
        ModeSettings& s = modeSettings[m];
        bool shouldActivate = false;
        
        switch ((Mode)m) {
            case MODE_VENT_HUM_TEMP:
                if (humAirIn > s.humThreshold || tempAirIn > s.tempThreshold) {
                    shouldActivate = true;
                }
                break;
                
            case MODE_VENT_PERIODIC:
                {
                    int totalMin = currentHour * 60 + currentMin;
                    int cycleMin = (s.intervalMinutes * 60) % (24 * 60);
                    if (totalMin % s.intervalMinutes < s.durationMinutes) {
                        shouldActivate = true;
                    }
                }
                break;
                
            case MODE_LIGHT_SUNRISE_SUNSET:
                {
                    int currentTotalMin = currentHour * 60 + currentMin;
                    int sunriseMin = s.sunriseHour * 60;
                    int sunsetMin = s.sunsetHour * 60;
                    
                    if (currentTotalMin >= sunriseMin && currentTotalMin <= sunsetMin) {
                        // PWM симуляция восхода/заката
                        int dayLength = sunsetMin - sunriseMin;
                        int pos = currentTotalMin - sunriseMin;
                        int brightness;
                        
                        if (pos < dayLength / 4) {  // Восход
                            brightness = map(pos, 0, dayLength/4, 0, 255);
                        } else if (pos < 3 * dayLength / 4) {  // День
                            brightness = 255;
                        } else {  // Закат
                            brightness = map(pos, 3*dayLength/4, dayLength, 255, 0);
                        }
                        ledcWrite(0, brightness);
                        shouldActivate = true;
                    } else {
                        ledcWrite(0, 0);
                    }
                }
                break;
                
            case MODE_PUMP_OUT_HUM:
                if (humAirOut < s.humThreshold) {
                    shouldActivate = true;
                }
                break;
                
            case MODE_PUMP_OUT_PERIODIC:
                {
                    int totalMin = currentHour * 60 + currentMin;
                    if (totalMin % s.intervalMinutes < s.durationMinutes) {
                        shouldActivate = true;
                    }
                }
                break;
                
            case MODE_PUMP_SOIL_HUM:
                if (soilMoistPercent < s.soilHumLow) {
                    shouldActivate = true;
                } else if (soilMoistPercent >= s.soilHumHigh) {
                    shouldActivate = false;
                }
                break;
                
            case MODE_HEATER_TEMP:
                if (tempAirIn < s.heaterThreshold) {
                    shouldActivate = true;
                }
                break;
        }
        
        if (shouldActivate != modeOutputState[m]) {
            modeOutputState[m] = shouldActivate;
            applyModeOutput((Mode)m, shouldActivate);
        }
    }
}

void applyModeOutput(Mode mode, bool state) {
    switch (mode) {
        case MODE_VENT_HUM_TEMP:
        case MODE_VENT_PERIODIC:
            setOutput(VENT_RELAY, state);
            break;
        case MODE_LIGHT_SUNRISE_SUNSET:
            // PWM управляется отдельно
            break;
        case MODE_PUMP_OUT_HUM:
        case MODE_PUMP_OUT_PERIODIC:
            setOutput(PUMP_OUT_PIN, state);
            break;
        case MODE_PUMP_SOIL_HUM:
            setOutput(PUMP_SOIL_PIN, state);
            break;
        case MODE_HEATER_TEMP:
            setOutput(HEATER_RELAY, state);
            break;
    }
}

void setOutput(uint8_t output, bool state) {
    digitalWrite(output, state ? HIGH : LOW);
    Serial.printf("Output %d: %s\n", output, state ? "ON" : "OFF");
}

// ========== ЗВУК ==========
void beepShort() {
    tone(BUZZER_PIN, 2000, 100);
}

void beepSiren() {
    for (int i = 0; i < 5; i++) {
        tone(BUZZER_PIN, 1500 + i * 200, 100);
        delay(100);
    }
    noTone(BUZZER_PIN);
}

// ========== НАСТРОЙКИ ==========
void saveSettings() {
    preferences.begin("grobox", false);
    preferences.putBool("apmode", apMode);
    
    for (int i = 0; i < MODE_COUNT; i++) {
        char key[20];
        sprintf(key, "mode_%d_active", i);
        preferences.putBool(key, modeActive[i]);
        
        sprintf(key, "mode_%d_hum", i);
        preferences.putFloat(key, modeSettings[i].humThreshold);
        
        sprintf(key, "mode_%d_temp", i);
        preferences.putFloat(key, modeSettings[i].tempThreshold);
        
        sprintf(key, "mode_%d_int", i);
        preferences.putUInt(key, modeSettings[i].intervalMinutes);
        
        sprintf(key, "mode_%d_dur", i);
        preferences.putUInt(key, modeSettings[i].durationMinutes);
        
        sprintf(key, "mode_%d_sunrise", i);
        preferences.putUInt(key, modeSettings[i].sunriseHour);
        
        sprintf(key, "mode_%d_sunset", i);
        preferences.putUInt(key, modeSettings[i].sunsetHour);
        
        sprintf(key, "mode_%d_soil_low", i);
        preferences.putFloat(key, modeSettings[i].soilHumLow);
        
        sprintf(key, "mode_%d_soil_high", i);
        preferences.putFloat(key, modeSettings[i].soilHumHigh);
        
        sprintf(key, "mode_%d_heater", i);
        preferences.putFloat(key, modeSettings[i].heaterThreshold);
    }
    preferences.end();
}

void loadSettings() {
    preferences.begin("grobox", false);
    apMode = preferences.getBool("apmode", true);
    
    for (int i = 0; i < MODE_COUNT; i++) {
        char key[20];
        
        sprintf(key, "mode_%d_active", i);
        modeActive[i] = preferences.getBool(key, false);
        
        sprintf(key, "mode_%d_hum", i);
        modeSettings[i].humThreshold = preferences.getFloat(key, 70.0f);
        
        sprintf(key, "mode_%d_temp", i);
        modeSettings[i].tempThreshold = preferences.getFloat(key, 25.0f);
        
        sprintf(key, "mode_%d_int", i);
        modeSettings[i].intervalMinutes = preferences.getUInt(key, 120);
        
        sprintf(key, "mode_%d_dur", i);
        modeSettings[i].durationMinutes = preferences.getUInt(key, 5);
        
        sprintf(key, "mode_%d_sunrise", i);
        modeSettings[i].sunriseHour = preferences.getUInt(key, 6);
        
        sprintf(key, "mode_%d_sunset", i);
        modeSettings[i].sunsetHour = preferences.getUInt(key, 20);
        
        sprintf(key, "mode_%d_soil_low", i);
        modeSettings[i].soilHumLow = preferences.getFloat(key, 30.0f);
        
        sprintf(key, "mode_%d_soil_high", i);
        modeSettings[i].soilHumHigh = preferences.getFloat(key, 80.0f);
        
        sprintf(key, "mode_%d_heater", i);
        modeSettings[i].heaterThreshold = preferences.getFloat(key, 10.0f);
    }
    preferences.end();
}
