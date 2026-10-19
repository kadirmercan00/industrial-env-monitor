"""
AWS Lambda Telemetry Normalizer & Safety Evaluator
Part of Industrial Environmental & Multi-Gas Monitoring Platform
Author: Kadir Mercan (Mercan Studio)

Trigger: AWS IoT Core Topic Rule ('industrial/env/+/+/telemetry')
Destinations:
  1. Amazon DynamoDB (Real-time telemetry timeseries & latest station state)
  2. Amazon S3 (Raw JSON/Parquet cold-storage data lake archive)
  3. Amazon SNS (Instant alert dispatch on OSHA hazardous threshold breach)
"""

import json
import os
import time
import logging
from decimal import Decimal
import boto3
from botocore.exceptions import ClientError

logger = logging.getLogger()
logger.setLevel(logging.INFO)

# AWS SDK Resources
dynamodb = boto3.resource('dynamodb')
s3 = boto3.client('s3')
sns = boto3.client('sns')

# Environment Variables (Configured in Terraform / CloudFormation)
TABLE_TELEMETRY = os.environ.get('TABLE_TELEMETRY', 'IndustrialEnv_Telemetry')
TABLE_LATEST = os.environ.get('TABLE_LATEST', 'IndustrialEnv_StationLatest')
S3_ARCHIVE_BUCKET = os.environ.get('S3_ARCHIVE_BUCKET', 'industrial-env-telemetry-archive')
SNS_ALERT_TOPIC_ARN = os.environ.get('SNS_ALERT_TOPIC_ARN', 'arn:aws:sns:eu-central-1:123456789012:PlantSafetyAlarms')

# OSHA / EU-OSHA Occupational Exposure Limits (OEL)
OSHA_THRESHOLDS = {
    'co_ppm_critical': 50.0,       # 8-hr Time-Weighted Average (TWA) limit
    'co_ppm_warning': 25.0,
    'eco2_ppm_critical': 2500,     # Extreme ventilation deficit
    'eco2_ppm_warning': 1500,
    'tvoc_ppb_critical': 600,
    'sound_dba_critical': 85.0,    # OSHA Action level for hearing protection
    'sound_dba_danger': 90.0
}


def parse_floats_to_decimal(obj):
    """DynamoDB requires Decimal types instead of standard Python floats."""
    if isinstance(obj, list):
        return [parse_floats_to_decimal(v) for v in obj]
    elif isinstance(obj, dict):
        return {k: parse_floats_to_decimal(v) for k, v in obj.items()}
    elif isinstance(obj, float):
        return Decimal(str(obj))
    return obj


def evaluate_safety_compliance(metrics: dict) -> dict:
    """Evaluates sensor readings against industrial health & safety guidelines."""
    violations = []
    severity = "NOMINAL"

    co_ppm = metrics.get('co_ppm', 0.0)
    sound_dba = metrics.get('sound_dba', 0.0)
    eco2_ppm = metrics.get('eco2_ppm', 0)
    tvoc_ppb = metrics.get('tvoc_ppb', 0)

    if co_ppm >= OSHA_THRESHOLDS['co_ppm_critical']:
        violations.append(f"CRITICAL: Toxic Carbon Monoxide level ({co_ppm} ppm) exceeds 50 ppm limit!")
        severity = "DANGER"
    elif co_ppm >= OSHA_THRESHOLDS['co_ppm_warning']:
        violations.append(f"WARNING: Elevated CO level detected ({co_ppm} ppm)")
        if severity != "DANGER":
            severity = "WARNING"

    if sound_dba >= OSHA_THRESHOLDS['sound_dba_danger']:
        violations.append(f"CRITICAL: Acoustic noise ({sound_dba} dBA) requires immediate ear defense gear.")
        severity = "DANGER"
    elif sound_dba >= OSHA_THRESHOLDS['sound_dba_critical']:
        violations.append(f"WARNING: Acoustic noise ({sound_dba} dBA) exceeds 85 dBA action limit.")
        if severity != "DANGER":
            severity = "WARNING"

    if eco2_ppm >= OSHA_THRESHOLDS['eco2_ppm_critical']:
        violations.append(f"WARNING: Poor air ventilation (eCO2: {eco2_ppm} ppm).")

    return {
        "severity": severity,
        "is_safe": severity == "NOMINAL",
        "violations": violations
    }


def lambda_handler(event, context):
    """Entry point for incoming MQTT telemetry payloads forwarded by AWS IoT Core."""
    logger.info(f"Received raw telemetry event: {json.dumps(event)}")

    station_id = event.get('station_id', 'UNKNOWN_STATION')
    facility_id = event.get('facility_id', 'PLANT_DEFAULT')
    timestamp = event.get('timestamp', int(time.time()))
    metrics = event.get('metrics', {})
    status = event.get('status', {})

    # 1. Safety Compliance Check
    compliance = evaluate_safety_compliance(metrics)

    # 2. Prepare Normalized Record
    normalized_record = {
        "station_id": station_id,
        "timestamp": timestamp,
        "facility_id": facility_id,
        "uptime_sec": event.get('uptime_sec', 0),
        "metrics": metrics,
        "status": status,
        "compliance": compliance,
        "ttl_expire": timestamp + (30 * 86400)  # DynamoDB TTL auto-prune 30 days
    }

    # 3. Write Timeseries to DynamoDB
    table_telemetry = dynamodb.Table(TABLE_TELEMETRY)
    table_latest = dynamodb.Table(TABLE_LATEST)
    dynamo_payload = parse_floats_to_decimal(normalized_record)

    try:
        table_telemetry.put_item(Item=dynamo_payload)
        logger.info(f"DynamoDB timeseries record committed for station {station_id}")
    except ClientError as e:
        logger.error(f"Failed to write to DynamoDB Telemetry table: {e}")

    # 4. Upsert Latest Station State
    try:
        table_latest.put_item(Item=dynamo_payload)
        logger.info(f"DynamoDB latest status updated for station {station_id}")
    except ClientError as e:
        logger.error(f"Failed to write to DynamoDB Latest table: {e}")

    # 5. Cold Storage Archive in S3 (Partitioned by date: YYYY/MM/DD/HH)
    time_struct = time.gmtime(timestamp)
    s3_key = (
        f"raw/facility={facility_id}/station={station_id}/"
        f"year={time_struct.tm_year}/month={time_struct.tm_mon:02d}/"
        f"day={time_struct.tm_mday:02d}/{station_id}_{timestamp}.json"
    )

    try:
        s3.put_object(
            Bucket=S3_ARCHIVE_BUCKET,
            Key=s3_key,
            Body=json.dumps(event),
            ContentType='application/json'
        )
        logger.info(f"S3 archive committed -> s3://{S3_ARCHIVE_BUCKET}/{s3_key}")
    except ClientError as e:
        logger.error(f"Failed to write S3 archive: {e}")

    # 6. Dispatch SNS Notification on Danger Event
    if compliance["severity"] == "DANGER":
        alarm_subject = f"🚨 [OSHA HAZARD ALERT] Plant {facility_id} - Station {station_id}"
        alarm_message = (
            f"CRITICAL SAFETY LIMIT BREACH DETECTED:\n\n"
            f"Station ID: {station_id}\n"
            f"Facility: {facility_id}\n"
            f"Timestamp: {time.ctime(timestamp)} UTC\n\n"
            f"Violations:\n" + "\n".join(f"- {v}" for v in compliance['violations']) + "\n\n"
            f"Current Readings:\n"
            f"  • Carbon Monoxide (CO): {metrics.get('co_ppm')} ppm\n"
            f"  • Noise Level: {metrics.get('sound_dba')} dBA\n"
            f"  • eCO2: {metrics.get('eco2_ppm')} ppm\n"
            f"  • Ambient Temp: {metrics.get('temperature_c')} °C\n\n"
            f"Inspect emergency ventilation and personal protective equipment immediately."
        )
        try:
            sns.publish(
                TopicArn=SNS_ALERT_TOPIC_ARN,
                Subject=alarm_subject,
                Message=alarm_message
            )
            logger.info(f"SNS Emergency dispatch broadcasted for {station_id}")
        except ClientError as e:
            logger.error(f"Failed to publish SNS hazard dispatch: {e}")

    return {
        "statusCode": 200,
        "body": json.dumps({
            "status": "SUCCESS",
            "station_id": station_id,
            "compliance_severity": compliance["severity"]
        })
    }
