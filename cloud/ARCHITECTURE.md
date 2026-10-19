# Cloud Architecture: Enterprise Serverless Telemetry Pipeline

This document details the **AWS Serverless IoT Ingestion Pipeline** supporting the **Industrial Multi-Gas & Environmental Monitoring Station**.

---

## 🏛️ System Architecture Diagram

```mermaid
flowchart TD
    subgraph Edge ["Hardware & Edge Layer"]
        ESP32["ESP32 Environmental Station"]
        SHT35["Sensirion SHT35 (I2C)"]
        CCS811["ScioSense CCS811 (I2C)"]
        CO["MQ Electrochemical CO (ADC)"]
        MIC["Acoustic dB Microphone (ADC)"]
        
        SHT35 --> ESP32
        CCS811 --> ESP32
        CO --> ESP32
        MIC --> ESP32
    end

    subgraph Security ["Transport Security"]
        ESP32 -->|"Mutual TLS 1.2 x.509 (Port 8883)"| AWS_IOT["AWS IoT Core Message Broker"]
    end

    subgraph Rules ["IoT Topic Rules Engine"]
        AWS_IOT -->|"Topic: industrial/env/+/telemetry"| RULE1["Topic Rule: Telemetry Forwarder"]
        AWS_IOT -->|"Topic: industrial/env/+/alert"| RULE2["Topic Rule: Emergency Dispatcher"]
    end

    subgraph Serverless ["Processing & Ingestion Layer"]
        RULE1 -->|"JSON Event Trigger"| LAMBDA["AWS Lambda: Telemetry Normalizer"]
        RULE2 -->|"Instant Push"| SNS_EMERGENCY["Amazon SNS: SMS / Email Emergency Alert"]
    end

    subgraph Storage ["Enterprise Persistence"]
        LAMBDA -->|"PutItem Partitioned"| DYNAMO_TS[("Amazon DynamoDB\nIndustrialEnv_Telemetry (30-day TTL)")]
        LAMBDA -->|"Upsert Key: station_id"| DYNAMO_LATEST[("Amazon DynamoDB\nIndustrialEnv_StationLatest")]
        LAMBDA -->|"Partition: YYYY/MM/DD"| S3_COLD[("Amazon S3 Data Lake\ns3://industrial-env-telemetry-archive/")]
    end

    subgraph Presentation ["Presentation & SCADA"]
        DYNAMO_LATEST --> APIGW["Amazon API Gateway"]
        DYNAMO_TS --> APIGW
        APIGW --> WEB["Industrial SCADA Web Dashboard"]
    end
```

---

## 1. AWS IoT Core Ingestion & Topic Structure

### Topic Hierarchy
The device publishes to structured hierarchical topics:
- **Telemetry Stream (5s interval):**  
  `industrial/env/{facility_id}/{station_id}/telemetry`
- **Critical Safety Alarms (Immediate):**  
  `industrial/env/{facility_id}/{station_id}/alert`
- **Device Lifecycle Status (LWT / Keep-alive):**  
  `industrial/env/{facility_id}/{station_id}/status`

### AWS IoT Topic Rule SQL
```sql
SELECT 
    topic(3) AS facility_id,
    topic(4) AS station_id,
    timestamp,
    uptime_sec,
    metrics,
    status
FROM 'industrial/env/+/+/telemetry'
```

---

## 2. Serverless Normalization & Compliance (AWS Lambda)

The telemetry normalizer (`cloud/lambda/telemetry_normalizer.py`) runs in Python 3.11 with the following responsibilities:
1. **OSHA/EU-OSHA Threshold Evaluation:** Compares raw concentrations against toxic thresholds:
   - **CO Limit:** 50 ppm (Critical Danger), 25 ppm (Warning)
   - **Sound Level:** 85 dBA (OSHA Action Level), 90 dBA (Mandatory Hearing Defense)
   - **eCO2:** 2500 ppm (Ventilation Deficit)
2. **Cold Storage Archiving (Amazon S3):**  
   Stores raw telemetry under partitioned keys:  
   `s3://industrial-env-telemetry-archive/raw/facility={facility}/station={station}/year={YYYY}/month={MM}/day={DD}/{station}_{timestamp}.json`
3. **Hot Cache Persistence (Amazon DynamoDB):**  
   - `IndustrialEnv_Telemetry`: Partition Key: `station_id` (String), Sort Key: `timestamp` (Number), TTL: `ttl_expire` (30 days automatic expiration).
   - `IndustrialEnv_StationLatest`: Key: `station_id` (Instant single-item lookup for dashboard headers).

---

## 3. High-Priority Safety Alarms (Amazon SNS)

When a sensor reading violates maximum permissible safety exposure limits, AWS Lambda immediately dispatches an emergency notification to the plant supervisor on-duty via **Amazon SNS**:
- **SMS alert** to plant emergency response teams.
- **Email report** with instant sensor readings, timestamp, and recommended evacuation or ventilation protocols.
