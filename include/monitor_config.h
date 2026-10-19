/**
 * @file monitor_config.h
 * @brief Industrial Environmental Monitor Pinout & Threshold Definitions
 * @author Kadir Mercan (Mercan Studio)
 */

#ifndef MONITOR_CONFIG_H
#define MONITOR_CONFIG_H

// ==================== I2C BUS & SENSOR ADDRESSES ====================
#define PIN_I2C_SDA                 21
#define PIN_I2C_SCL                 22
#define SHT35_I2C_ADDR              0x44    ///< Sensirion SHT35 Precision Temp/Hum
#define CCS811_I2C_ADDR             0x5A    ///< ScioSense eCO2 / TVOC Gas Sensor

// ==================== ANALOG SENSORS & GPIO ====================
#define PIN_ANALOG_CO               32      ///< Carbon Monoxide MQ Electrochemical Analog Input
#define PIN_ANALOG_SOUND            34      ///< Peak-to-Peak Noise Level Microphone
#define PIN_ALARM_BUZZER            25      ///< Piezo Alarm Buzzer
#define PIN_STATUS_LED              26      ///< Facility Warning LED Indicator
#define PIN_SD_CHIP_SELECT          5       ///< SPI Bus Chip Select for MicroSD Datalogger

// ==================== SAFETY THRESHOLD LIMITS ====================
#define THRESHOLD_TEMP_MAX_C        50.0f   ///< Maximum ambient temperature limit
#define THRESHOLD_HUM_MAX_RH        80.0f   ///< Relative humidity condensation limit
#define THRESHOLD_ECO2_PPM          2500    ///< Carbon dioxide equivalent threshold
#define THRESHOLD_TVOC_PPB          600     ///< Total volatile organic compounds limit
#define THRESHOLD_CO_PPM            50.0f   ///< Carbon monoxide occupational exposure limit
#define THRESHOLD_SOUND_DB          90.0f   ///< Continuous acoustic noise threshold

// ==================== SAMPLING & FILTERING ====================
#define CO_MOVING_AVG_SAMPLES       5
#define SOUND_SAMPLE_WINDOW_MS      50
#define TELEMETRY_PUBLISH_INTERVAL_MS 5000  ///< 5-second cloud streaming interval

// ==================== DEVICE IDENTITY & AWS IOT CORE ====================
#define FIRMWARE_VERSION            "v2.1.0-aws-iot"
#define FACILITY_ID                 "PLANT_01"
#define STATION_ID                  "ENV_STATION_001"

#define AWS_IOT_ENDPOINT            "xxxxxxxxxxxxxx-ats.iot.eu-central-1.amazonaws.com"
#define AWS_IOT_PORT                8883
#define MQTT_KEEPALIVE_SEC          60
#define MQTT_BUFFER_SIZE            1024

// Topic format: industrial/env/{facility}/{station}/{channel}
#define MQTT_TOPIC_TELEMETRY        "industrial/env/" FACILITY_ID "/" STATION_ID "/telemetry"
#define MQTT_TOPIC_ALERT            "industrial/env/" FACILITY_ID "/" STATION_ID "/alert"
#define MQTT_TOPIC_STATUS           "industrial/env/" FACILITY_ID "/" STATION_ID "/status"

#endif // MONITOR_CONFIG_H
