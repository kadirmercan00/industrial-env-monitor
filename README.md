# Industrial Multi-Gas, Acoustic & Environmental Monitoring Station

[![License: Proprietary Showcase](https://img.shields.io/badge/License-Proprietary_Portfolio_Showcase-red.svg)](LICENSE)
[![Platform: ESP32](https://img.shields.io/badge/Platform-Espressif_ESP32-E7352C.svg?logo=espressif)](https://www.espressif.com/)
[![Sensors: Sensirion & ScioSense](https://img.shields.io/badge/Sensors-SHT35%20%7C%20CCS811%20%7C%20MQ%20CO-brightgreen.svg)](#sensor-instrumentation)
[![Storage: MicroSD FAT32](https://img.shields.io/badge/Datalogger-SPI_MicroSD_CSV-blue.svg)](#offline-black-box-datalogging)

> [!WARNING]
> **PROPRIETARY PORTFOLIO SHOWCASE NOTICE**  
> This repository is published strictly for technical evaluation, code review, and architectural demonstration as part of **Kadir Mercan's (Mercan Studio)** engineering portfolio.  
> **All rights reserved.** Reproduction, commercial deployment, redistribution, or derivation of this codebase without prior written authorization is strictly prohibited under the terms of the included [LICENSE](LICENSE).

---

## 🎯 Executive Summary

An industrial-grade environmental and occupational safety monitoring station powered by the **Espressif ESP32 SoC**. Engineered for harsh industrial plant environments, the firmware simultaneously samples precision microclimate conditions, air toxicity, combustible/asphyxiating gases, and acoustic noise pollution. Telemetry is timestamped via **SNTP UTC** synchronization and logged offline to an onboard **MicroSD SPI black-box** in FAT32 CSV format while supervising audio-visual safety alarms.

---

## 🔬 Sensor Instrumentation & Signal Processing

| Metric | Sensor Component | Interface | Signal Processing & Calibration |
|:---|:---|:---:|:---|
| **Temperature & Humidity** | **Sensirion SHT35** | Raw I2C (`0x44`) | `0x2400` high-repeatability command + hardware CRC-8 verification polynomial ($x^8 + x^5 + x^4 + 1$) |
| **eCO2 & TVOC** | **ScioSense CCS811** | I2C (`0x5A`) | Metal-oxide (MOX) gas sensor with automatic baseline compensation |
| **Carbon Monoxide (CO)** | **Electrochemical MQ** | 12-bit ADC (`GPIO 32`) | Discrete circular moving average filter ($N=5$) + clean-air baseline voltage offset calibration |
| **Acoustic Noise (dBA)** | **Electret Mic Subsystem** | 12-bit ADC (`GPIO 34`) | High-speed 50 ms peak-to-peak amplitude windowing with logarithmic sound pressure conversion |

---

## 🏛️ System Architecture

```
┌─────────────────────────┐
│   Sensirion SHT35       │ ──► Raw I2C (Clock Stretch Disabled, CRC-8) ──┐
└─────────────────────────┘                                               │
┌─────────────────────────┐                                               │
│   ScioSense CCS811      │ ──► I2C eCO2 / TVOC Gas Analysis ─────────────┼──► ┌──────────────────────────────┐
└─────────────────────────┘                                               │    │      ESP32 Processing Core    │
┌─────────────────────────┐                                               │    │ • Discrete MA Filtering       │
│   Electrochemical CO    │ ──► 12-Bit ADC + Circular Moving Average ─────┼──► │ • Threshold Hazard Logic      │
└─────────────────────────┘                                               │    │ • SNTP UTC Time Sync          │
┌─────────────────────────┐                                               │    └──────────────┬───────────────┘
│   Acoustic dB Sensor    │ ──► 50 ms Peak-to-Peak Amplitude Window ──────┘                   │
                                                                                              │
                                  ┌───────────────────────────────────────────────────────────┴──────────────────────────┐
                                  ▼                                                                                      ▼
                    ┌───────────────────────────┐                                                          ┌───────────────────────────┐
                    │    FAT32 MicroSD Logger   │                                                          │   Audio-Visual Annunciator│
                    │   Daily Rotated CSV Logs  │                                                          │   Piezo PWM + Hazard LED  │
                    └───────────────────────────┘                                                          └───────────────────────────┘
```

---

## 📊 Offline Black-Box Datalogging

To ensure occupational health compliance even during network outages, all metrics are recorded to an industrial MicroSD card:
```csv
Date,Time,Temperature_C,Humidity_RH,eCO2_PPM,TVOC_PPB,CO_PPM,Sound_dBA,Hazard_Alarm
2026-09-26,21:40:12,23.45,45.20,620,45,1.20,62.4,NOMINAL
2026-09-26,21:40:14,52.10,46.10,2650,610,54.30,94.2,CRITICAL_HAZARD
```

---

## 📁 Repository Structure

```
industrial-env-monitor/
├── include/
│   └── monitor_config.h     # Pin mappings, I2C addresses & safety thresholds
├── src/
│   └── main.cpp             # Raw I2C SHT35 driver, analog filters & SD logger
├── platformio.ini           # PlatformIO environment & library dependencies
├── LICENSE                  # Strict Proprietary Portfolio Showcase License
└── README.md                # Instrument specifications & architecture
```

---

## 🛠️ Build & Configuration

1. **Setup PlatformIO:**
   ```bash
   pio run -e esp32dev
   ```
2. **Flash to Target Hardware:**
   ```bash
   pio run -e esp32dev -t upload
   ```

---

## 👨‍💻 Engineering & Contact

**Kadir Mercan**  
*Senior Embedded Systems, Hardware (PCB) & IoT Engineer*  
- **GitHub:** [@kadirmercan00](https://github.com/kadirmercan00)  
- **Brand:** Mercan Studio  
- **Expertise:** Sensor Fusion, Industrial Data Acquisition, High-Precision Analog Front-Ends, RTOS Firmware.
