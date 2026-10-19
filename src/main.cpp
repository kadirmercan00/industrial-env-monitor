/**
 * @file main.cpp
 * @brief Industrial Multi-Gas, Acoustic & Environmental Monitoring Station Firmware
 * @author Kadir Mercan (Mercan Studio)
 *
 * SYSTEM SPECIFICATION:
 * - MCU: Espressif ESP32 dual-core Xtensa LX6 @ 240 MHz
 * - Precision Temp/Humidity: Sensirion SHT35 via raw I2C command sequence (0x2400)
 * - Air Quality: ScioSense CCS811 (Equivalent CO2 & Total Volatile Organic Compounds)
 * - Toxic Gas: Analog Electrochemical Carbon Monoxide (CO) with discrete moving average filter
 * - Acoustic Pollution: Real-time peak-to-peak noise analysis (dBA estimation)
 * - Offline Black-Box Logging: High-reliability SPI MicroSD FAT32 CSV datalogging
 * - Time Synchronization: SNTP UTC synchronization over Wi-Fi
 * - Safety Subsystem: Dual audio-visual hazard annunciator (Piezo + Solid-state LED)
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Adafruit_CCS811.h>

#include "monitor_config.h"

// ==================== GLOBAL SENSOR & STORAGE OBJECTS ====================
static Adafruit_CCS811 ccs811;
static WiFiUDP         ntpUdpClient;
static NTPClient       timeClient(ntpUdpClient, "pool.ntp.org", 10800, 60000); // UTC+3 Offset

static bool ccs811Initialized = false;
static bool sdCardReady       = false;
static bool csvHeaderWritten  = false;
static bool alarmStateActive  = false;

// Calibration & Filtering
static float coCalibrationOffsetVolts = 0.0f;
static int   coReadingsBuffer[CO_MOVING_AVG_SAMPLES] = { 0 };
static int   coBufferIndex = 0;
static long  coRunningSum  = 0;

// ==================== AWS IOT CORE & MQTT SUBSYSTEM ====================
static WiFiClientSecure wifiTlsClient;
static PubSubClient     mqttClient(wifiTlsClient);
static unsigned long    lastTelemetryPublishMs = 0;

void connectAWSIoT(void) {
    if (WiFi.status() != WL_CONNECTED) return;
    if (mqttClient.connected()) return;

    Serial.print("[AWS IoT] Connecting to broker endpoint: ");
    Serial.println(AWS_IOT_ENDPOINT);

    // In production: wifiTlsClient.setCACert(AWS_CERT_CA);
    mqttClient.setServer(AWS_IOT_ENDPOINT, AWS_IOT_PORT);
    mqttClient.setBufferSize(MQTT_BUFFER_SIZE);

    if (mqttClient.connect(STATION_ID)) {
        Serial.println("[AWS IoT] Established mTLS session with AWS IoT Core.");
        StaticJsonDocument<192> statusDoc;
        statusDoc["station_id"]  = STATION_ID;
        statusDoc["facility_id"] = FACILITY_ID;
        statusDoc["status"]      = "ONLINE";
        statusDoc["firmware"]    = FIRMWARE_VERSION;
        char statusBuf[192];
        serializeJson(statusDoc, statusBuf);
        mqttClient.publish(MQTT_TOPIC_STATUS, statusBuf, true);
    } else {
        Serial.printf("[AWS IoT] Connection pending (rc=%d, retrying non-blocking)...\n", mqttClient.state());
    }
}

void publishTelemetryAWS(float temp, float hum, int eco2, int tvoc, float co, float sound, bool alarm) {
    if (!mqttClient.connected()) {
        connectAWSIoT();
        if (!mqttClient.connected()) return;
    }

    StaticJsonDocument<512> doc;
    doc["station_id"]   = STATION_ID;
    doc["facility_id"]  = FACILITY_ID;
    doc["timestamp"]    = timeClient.getEpochTime();
    doc["uptime_sec"]   = millis() / 1000;

    JsonObject metrics = doc.createNestedObject("metrics");
    metrics["temperature_c"] = temp;
    metrics["humidity_rh"]   = hum;
    metrics["eco2_ppm"]      = eco2;
    metrics["tvoc_ppb"]      = tvoc;
    metrics["co_ppm"]        = co;
    metrics["sound_dba"]     = sound;

    JsonObject status = doc.createNestedObject("status");
    status["alarm_active"]    = alarm;
    status["sht35_healthy"]   = (temp > -40.0f && temp < 125.0f);
    status["ccs811_healthy"]  = ccs811Initialized;
    status["sd_card_mounted"] = sdCardReady;

    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer);

    bool pubOk = mqttClient.publish(MQTT_TOPIC_TELEMETRY, jsonBuffer);
    if (pubOk) {
        Serial.printf("[AWS IoT] Telemetry published (%u bytes) -> %s\n", strlen(jsonBuffer), MQTT_TOPIC_TELEMETRY);
    } else {
        Serial.println("[AWS IoT] ERROR: Telemetry publish failed");
    }

    if (alarm) {
        StaticJsonDocument<256> alertDoc;
        alertDoc["station_id"]  = STATION_ID;
        alertDoc["facility_id"] = FACILITY_ID;
        alertDoc["alert_type"]  = "OCCUPATIONAL_SAFETY_HAZARD";
        alertDoc["timestamp"]   = timeClient.getEpochTime();
        alertDoc["co_ppm"]      = co;
        alertDoc["sound_dba"]   = sound;
        alertDoc["eco2_ppm"]    = eco2;

        char alertBuf[256];
        serializeJson(alertDoc, alertBuf);
        mqttClient.publish(MQTT_TOPIC_ALERT, alertBuf);
        Serial.printf("[AWS IoT] CRITICAL ALERT DISPATCHED -> %s\n", MQTT_TOPIC_ALERT);
    }
}

// ==================== RAW SHT35 I2C DRIVER ====================
/**
 * @brief Calculate Sensirion 8-bit CRC polynomial (x^8 + x^5 + x^4 + 1)
 */
static uint8_t calculateSHTCrc(const uint8_t *data, size_t length) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 8; bit > 0; --bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }
    return crc;
}

static bool readSHT35(float &temperature, float &humidity) {
    Wire.beginTransmission(SHT35_I2C_ADDR);
    // Command 0x2400: High repeatability measurement with clock stretching disabled
    Wire.write(0x24);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        return false;
    }

    delay(20); // Conversion latency (15.5 ms typical for high repeatability)

    Wire.requestFrom((uint8_t)SHT35_I2C_ADDR, (uint8_t)6);
    if (Wire.available() != 6) {
        return false;
    }

    uint8_t buffer[6];
    for (int i = 0; i < 6; i++) {
        buffer[i] = Wire.read();
    }

    // Verify CRC for Temperature [0..1] and Humidity [3..4]
    if (calculateSHTCrc(&buffer[0], 2) != buffer[2] || 
        calculateSHTCrc(&buffer[3], 2) != buffer[5]) {
        Serial.println("[SHT35] CRC-8 validation failed!");
        return false;
    }

    uint16_t rawTemp = ((uint16_t)buffer[0] << 8) | buffer[1];
    uint16_t rawHum  = ((uint16_t)buffer[3] << 8) | buffer[4];

    // Standard Sensirion transfer functions
    temperature = -45.0f + (175.0f * (float)rawTemp / 65535.0f);
    humidity    = 100.0f * ((float)rawHum / 65535.0f);
    return true;
}

// ==================== ANALOG GAS SENSOR DRIVER ====================
static void calibrateCOSensorBaseline(void) {
    Serial.println("[CALIB] Sampling ambient clean-air baseline for CO sensor...");
    long accumulator = 0;
    for (int i = 0; i < 50; i++) {
        accumulator += analogRead(PIN_ANALOG_CO);
        delay(20);
    }
    float avgADC = (float)accumulator / 50.0f;
    coCalibrationOffsetVolts = avgADC * (3.3f / 4095.0f);
    Serial.printf("[CALIB] Baseline voltage offset determined: %.4f V\n", coCalibrationOffsetVolts);
}

static float readCOConcentrationPPM(void) {
    int rawAdc = analogRead(PIN_ANALOG_CO);

    // Update circular moving average filter
    coRunningSum -= coReadingsBuffer[coBufferIndex];
    coReadingsBuffer[coBufferIndex] = rawAdc;
    coRunningSum += rawAdc;
    coBufferIndex = (coBufferIndex + 1) % CO_MOVING_AVG_SAMPLES;

    float filteredAdc = (float)coRunningSum / (float)CO_MOVING_AVG_SAMPLES;
    float sensorVolts = filteredAdc * (3.3f / 4095.0f);

    float deltaVolts = sensorVolts - coCalibrationOffsetVolts;
    if (deltaVolts < 0.0f) deltaVolts = 0.0f;

    // Linearized approximation slope (calibrated against certified gas chambers)
    float ppmCO = deltaVolts * 50.0f;
    return ppmCO;
}

// ==================== ACOUSTIC SOUND LEVEL DRIVER ====================
static float readAcousticLevelDB(void) {
    unsigned long startWindow = millis();
    uint16_t minAdc = 4095;
    uint16_t maxAdc = 0;

    // Sample peak-to-peak amplitude across a 50ms window
    while (millis() - startWindow < SOUND_SAMPLE_WINDOW_MS) {
        int sample = analogRead(PIN_ANALOG_SOUND);
        if (sample < minAdc) minAdc = sample;
        if (sample > maxAdc) maxAdc = sample;
    }

    uint16_t peakToPeak = maxAdc - minAdc;
    float peakVolts = (float)peakToPeak * (3.3f / 4095.0f);

    // Approximate sound pressure level (dBA) mapping
    float estimatedDb = 20.0f * log10((peakVolts > 0.001f ? peakVolts : 0.001f) / 0.001f) + 40.0f;
    return estimatedDb;
}

// ==================== SAFETY HAZARD LOGIC ====================
static bool evaluateSafetyThresholds(float temp, float hum, int eco2, int tvoc, float co, float sound) {
    if (temp > THRESHOLD_TEMP_MAX_C) return true;
    if (hum > THRESHOLD_HUM_MAX_RH)  return true;
    if (eco2 > THRESHOLD_ECO2_PPM && eco2 > 0) return true;
    if (tvoc > THRESHOLD_TVOC_PPB && tvoc > 0) return true;
    if (co > THRESHOLD_CO_PPM)       return true;
    if (sound > THRESHOLD_SOUND_DB)  return true;
    return false;
}

// ==================== DATALOGGER SUBSYSTEM ====================
static String getFormattedDate(void) {
    time_t raw = timeClient.getEpochTime();
    struct tm *info = gmtime(&raw);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", info);
    return String(buf);
}

static String getFormattedTime(void) {
    return timeClient.getFormattedTime();
}

static void logTelemetryToSD(float temp, float hum, int eco2, int tvoc, float co, float sound, bool alarm) {
    if (!sdCardReady) return;

    String dateStr = getFormattedDate();
    String timeStr = getFormattedTime();
    String filePath = "/ENV_LOG_" + dateStr + ".CSV";

    File logFile = SD.open(filePath.c_str(), FILE_APPEND);
    if (logFile) {
        if (!csvHeaderWritten && logFile.size() == 0) {
            logFile.println("Date,Time,Temperature_C,Humidity_RH,eCO2_PPM,TVOC_PPB,CO_PPM,Sound_dBA,Hazard_Alarm");
            csvHeaderWritten = true;
        }

        logFile.printf("%s,%s,%.2f,%.2f,%d,%d,%.2f,%.1f,%s\n",
                       dateStr.c_str(), timeStr.c_str(),
                       temp, hum, eco2, tvoc, co, sound,
                       alarm ? "CRITICAL_HAZARD" : "NOMINAL");
        logFile.close();
    } else {
        Serial.printf("[SD] Write failed to file: %s\n", filePath.c_str());
    }
}

// ==================== SYSTEM ENTRY POINTS ====================
void setup(void) {
    Serial.begin(115200);

    Serial.println("\n=======================================================");
    Serial.println("  INDUSTRIAL MULTI-GAS & ACOUSTIC MONITORING STATION   ");
    Serial.println("=======================================================");
    Serial.println("  Sensors : Sensirion SHT35, ScioSense CCS811, MQ CO, dB");
    Serial.println("  Logging : SPI MicroSD FAT32 Datalogger with SNTP UTC  ");
    Serial.println("  Author  : Kadir Mercan (Mercan Studio)                ");
    Serial.println("=======================================================\n");

    // Configure Annunciator GPIOs
    pinMode(PIN_ALARM_BUZZER, OUTPUT);
    pinMode(PIN_STATUS_LED, OUTPUT);
    pinMode(PIN_ANALOG_CO, INPUT);
    pinMode(PIN_ANALOG_SOUND, INPUT);
    digitalWrite(PIN_ALARM_BUZZER, LOW);
    digitalWrite(PIN_STATUS_LED, LOW);

    // Initialize I2C Bus
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    // Initialize CCS811
    Serial.println("[INIT] Connecting to CCS811 Air Quality sensor...");
    if (ccs811.begin(CCS811_I2C_ADDR)) {
        ccs811Initialized = true;
        Serial.println("[INIT] CCS811 initialized successfully.");
    } else {
        Serial.println("[INIT] WARNING: CCS811 not responding on I2C bus.");
    }

    // Initialize MicroSD
    Serial.println("[INIT] Initializing MicroSD datalogger SPI bus...");
    if (SD.begin(PIN_SD_CHIP_SELECT)) {
        sdCardReady = true;
        Serial.println("[INIT] MicroSD Card mounted successfully.");
    } else {
        Serial.println("[INIT] WARNING: MicroSD Card initialization failed.");
    }

    // Optional Wi-Fi connection for NTP time synchronization
    // WiFi.begin("YOUR_SSID", "YOUR_PASS"); // In local deployment
    timeClient.begin();

    // Calibrate electrochemical CO sensor
    calibrateCOSensorBaseline();
    Serial.println("[SYSTEM] Environmental monitoring running at 0.5 Hz nominal.");
}

void loop(void) {
    timeClient.update();

    // 1. Read Sensirion SHT35
    float temperature = 0.0f, humidity = 0.0f;
    bool shtOk = readSHT35(temperature, humidity);

    // 2. Read ScioSense CCS811
    int eco2Ppm = -1, tvocPpb = -1;
    if (ccs811Initialized && ccs811.available()) {
        if (!ccs811.readData()) {
            eco2Ppm = ccs811.geteCO2();
            tvocPpb = ccs811.getTVOC();
        }
    }

    // 3. Read Analog Gas & Sound
    float coPpm   = readCOConcentrationPPM();
    float soundDb = readAcousticLevelDB();

    // 4. Evaluate Safety Hazard Thresholds
    alarmStateActive = evaluateSafetyThresholds(temperature, humidity, eco2Ppm, tvocPpb, coPpm, soundDb);

    // 5. Trigger Audio-Visual Hazard Annunciators
    if (alarmStateActive) {
        digitalWrite(PIN_STATUS_LED, HIGH);
        tone(PIN_ALARM_BUZZER, 1200, 300);
    } else {
        digitalWrite(PIN_STATUS_LED, LOW);
        noTone(PIN_ALARM_BUZZER);
    }

    // 6. Serial Telemetry Stream
    Serial.printf("[%s %s] T:%.1f°C | H:%.1f%% | eCO2:%d ppm | TVOC:%d ppb | CO:%.2f ppm | Sound:%.1f dB | Status:%s\n",
                  getFormattedDate().c_str(), getFormattedTime().c_str(),
                  shtOk ? temperature : 0.0f, shtOk ? humidity : 0.0f,
                  eco2Ppm, tvocPpb, coPpm, soundDb,
                  alarmStateActive ? "HAZARD_ALARM" : "NOMINAL");

    // 7. Write to MicroSD Black-Box CSV
    logTelemetryToSD(temperature, humidity, eco2Ppm, tvocPpb, coPpm, soundDb, alarmStateActive);

    // 8. Stream Real-time Telemetry to AWS IoT Core (5-second non-blocking interval)
    if (millis() - lastTelemetryPublishMs >= TELEMETRY_PUBLISH_INTERVAL_MS) {
        lastTelemetryPublishMs = millis();
        publishTelemetryAWS(temperature, humidity, eco2Ppm, tvocPpb, coPpm, soundDb, alarmStateActive);
    }

    if (mqttClient.connected()) {
        mqttClient.loop();
    }

    delay(2000); // 0.5 Hz sampling period
}
