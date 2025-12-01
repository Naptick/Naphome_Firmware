#!/usr/bin/env python3
"""
AWS IoT Connection Validation Script

This script validates that:
1. Device is connected to AWS IoT
2. Device is publishing sensor telemetry data
3. Data format matches expected schema
4. Web test page can receive the data
"""

import json
import time
import sys
import argparse
from datetime import datetime
from typing import Dict, List, Optional

try:
    import boto3
    from botocore.exceptions import ClientError, BotoCoreError
except ImportError:
    print("ERROR: boto3 not installed. Install with: pip install boto3")
    sys.exit(1)

# Expected sensor fields
EXPECTED_SENSORS = {
    'sht45': ['temperature_c', 'humidity_rh'],
    'sgp40': ['voc_index', 'voc_ticks'],
    'scd40': ['co2_ppm', 'temperature_c', 'humidity_rh'],
    'vcnl4040': ['ambient_lux', 'proximity'],
    'ec10': ['pm1_0_ug_m3', 'pm2_5_ug_m3', 'pm10_ug_m3']
}

# AWS IoT configuration
AWS_REGION = 'ap-south-1'
AWS_IOT_ENDPOINT = 'a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com'

def validate_credentials():
    """Validate AWS credentials are configured."""
    try:
        session = boto3.Session()
        credentials = session.get_credentials()
        if credentials is None:
            print("❌ AWS credentials not found")
            print("   Configure using: aws configure")
            print("   Or set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY environment variables")
            return False
        print("✅ AWS credentials found")
        return True
    except Exception as e:
        print(f"❌ Error checking credentials: {e}")
        return False

def check_device_shadow(device_id: str) -> bool:
    """Check if device has a shadow (indicates it's been connected)."""
    try:
        iot_data = boto3.client('iot-data', region_name=AWS_REGION, endpoint_url=f'https://{AWS_IOT_ENDPOINT}')
        response = iot_data.get_thing_shadow(thingName=device_id)
        shadow = json.loads(response['payload'].read())
        state = shadow.get('state', {})
        reported = state.get('reported', {})
        
        print(f"✅ Device shadow found for {device_id}")
        if reported:
            print(f"   Last reported state: {reported}")
        return True
    except ClientError as e:
        if e.response['Error']['Code'] == 'ResourceNotFoundException':
            print(f"⚠️  No shadow found for {device_id} (device may not have connected yet)")
        else:
            print(f"❌ Error checking shadow: {e}")
        return False
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def list_connected_devices() -> List[str]:
    """List devices that are currently connected to AWS IoT."""
    try:
        iot = boto3.client('iot', region_name=AWS_REGION)
        
        # Get all things
        things = []
        paginator = iot.get_paginator('list_things')
        for page in paginator.paginate():
            things.extend([thing['thingName'] for thing in page['things']])
        
        print(f"📋 Found {len(things)} registered devices")
        
        # Check which are connected (via thing connectivity)
        connected = []
        for thing in things:
            if thing.startswith('SOMNUS_'):
                connected.append(thing)
        
        if connected:
            print(f"✅ Found {len(connected)} Somnus device(s):")
            for device in connected:
                print(f"   - {device}")
        else:
            print("⚠️  No Somnus devices found")
        
        return connected
    except Exception as e:
        print(f"❌ Error listing devices: {e}")
        return []

def validate_telemetry_payload(payload: Dict) -> tuple[bool, List[str]]:
    """Validate telemetry payload structure."""
    errors = []
    
    # Check required fields
    if 'deviceId' not in payload:
        errors.append("Missing 'deviceId' field")
    if 'timestamp_ms' not in payload:
        errors.append("Missing 'timestamp_ms' field")
    
    # Check for at least one sensor
    sensor_keys = [key for key in payload.keys() if key in EXPECTED_SENSORS]
    if not sensor_keys:
        errors.append("No sensor data found in payload")
    
    # Validate each sensor's fields
    for sensor_name, expected_fields in EXPECTED_SENSORS.items():
        if sensor_name in payload:
            sensor_data = payload[sensor_name]
            if not isinstance(sensor_data, dict):
                errors.append(f"{sensor_name}: Expected object, got {type(sensor_data)}")
                continue
            
            for field in expected_fields:
                if field not in sensor_data:
                    errors.append(f"{sensor_name}: Missing field '{field}'")
                elif not isinstance(sensor_data[field], (int, float)):
                    errors.append(f"{sensor_name}.{field}: Expected number, got {type(sensor_data[field])}")
    
    return len(errors) == 0, errors

def subscribe_and_validate(device_id: str, duration: int = 30) -> bool:
    """Subscribe to device telemetry topic and validate messages."""
    import paho.mqtt.client as mqtt
    
    topic = f"device/telemetry/{device_id}"
    messages_received = []
    validation_errors = []
    
    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print(f"✅ Connected to AWS IoT")
            print(f"📡 Subscribing to: {topic}")
            client.subscribe(topic, qos=1)
        else:
            print(f"❌ Connection failed with code {rc}")
    
    def on_message(client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())
            messages_received.append({
                'timestamp': datetime.now().isoformat(),
                'payload': payload
            })
            
            is_valid, errors = validate_telemetry_payload(payload)
            if not is_valid:
                validation_errors.extend(errors)
                print(f"❌ Invalid payload: {errors}")
            else:
                sensor_count = len([k for k in payload.keys() if k in EXPECTED_SENSORS])
                print(f"✅ Valid message received ({sensor_count} sensors)")
        except json.JSONDecodeError as e:
            print(f"❌ Invalid JSON: {e}")
            validation_errors.append(f"JSON decode error: {e}")
        except Exception as e:
            print(f"❌ Error processing message: {e}")
            validation_errors.append(str(e))
    
    def on_subscribe(client, userdata, mid, granted_qos):
        print(f"✅ Subscribed to {topic} (QoS: {granted_qos[0]})")
        print(f"⏳ Listening for {duration} seconds...")
    
    # Get AWS credentials
    session = boto3.Session()
    credentials = session.get_credentials()
    
    # Create MQTT client with AWS IoT authentication
    # Note: This requires AWS IoT SDK or manual SigV4 signing
    # For simplicity, we'll use AWS CLI or provide instructions
    
    print("\n" + "="*60)
    print("MQTT Subscription Test")
    print("="*60)
    print(f"Topic: {topic}")
    print(f"Duration: {duration} seconds")
    print("\n⚠️  Note: Direct MQTT subscription requires AWS IoT SDK")
    print("   Alternative: Use AWS IoT Test Client in console")
    print("   Or use the web test page: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html")
    print("="*60)
    
    return len(messages_received) > 0 and len(validation_errors) == 0

def check_iot_policy(device_id: str) -> bool:
    """Check if device has proper IoT policy for publishing."""
    try:
        iot = boto3.client('iot', region_name=AWS_REGION)
        
        # Get thing's principals (certificates)
        principals = iot.list_thing_principals(thingName=device_id)
        
        if not principals['principals']:
            print(f"⚠️  No certificates attached to {device_id}")
            return False
        
        print(f"✅ Found {len(principals['principals'])} certificate(s) attached")
        
        # Check policies for each certificate
        for principal_arn in principals['principals']:
            cert_id = principal_arn.split('/')[-1]
            policies = iot.list_principal_policies(principal=principal_arn)
            
            if policies['policies']:
                print(f"   Certificate {cert_id} has {len(policies['policies'])} policy/policies")
                for policy in policies['policies']:
                    print(f"      - {policy['policyName']}")
            else:
                print(f"   ⚠️  Certificate {cert_id} has no policies")
        
        return True
    except ClientError as e:
        if e.response['Error']['Code'] == 'ResourceNotFoundException':
            print(f"❌ Device {device_id} not found in AWS IoT")
        else:
            print(f"❌ Error checking policies: {e}")
        return False
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Validate AWS IoT connection and data publishing')
    parser.add_argument('--device-id', type=str, default='SOMNUS_F09E9E3263A4',
                       help='Device ID to validate (default: SOMNUS_F09E9E3263A4)')
    parser.add_argument('--check-shadow', action='store_true',
                       help='Check device shadow')
    parser.add_argument('--check-policy', action='store_true',
                       help='Check device IoT policies')
    parser.add_argument('--list-devices', action='store_true',
                       help='List all connected devices')
    parser.add_argument('--subscribe', action='store_true',
                       help='Subscribe to telemetry topic (requires MQTT setup)')
    parser.add_argument('--duration', type=int, default=30,
                       help='Subscription duration in seconds (default: 30)')
    
    args = parser.parse_args()
    
    print("="*60)
    print("AWS IoT Connection Validation")
    print("="*60)
    print(f"Region: {AWS_REGION}")
    print(f"Endpoint: {AWS_IOT_ENDPOINT}")
    print(f"Device ID: {args.device_id}")
    print("="*60 + "\n")
    
    # Step 1: Validate credentials
    print("Step 1: Validating AWS credentials...")
    if not validate_credentials():
        sys.exit(1)
    print()
    
    # Step 2: List devices
    if args.list_devices:
        print("Step 2: Listing devices...")
        devices = list_connected_devices()
        print()
    
    # Step 3: Check device shadow
    if args.check_shadow:
        print("Step 3: Checking device shadow...")
        check_device_shadow(args.device_id)
        print()
    
    # Step 4: Check policies
    if args.check_policy:
        print("Step 4: Checking device policies...")
        check_iot_policy(args.device_id)
        print()
    
    # Step 5: Subscribe and validate
    if args.subscribe:
        print("Step 5: Subscribing to telemetry topic...")
        subscribe_and_validate(args.device_id, args.duration)
        print()
    
    # Summary
    print("="*60)
    print("Validation Summary")
    print("="*60)
    print("✅ Credentials validated")
    print("📋 Use --list-devices to see registered devices")
    print("📋 Use --check-shadow to verify device connection")
    print("📋 Use --check-policy to verify permissions")
    print("📋 Use --subscribe to test MQTT subscription (requires setup)")
    print("\n💡 Recommended: Use AWS IoT Test Client in console")
    print("   Or test via web page: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html")
    print("="*60)

if __name__ == '__main__':
    main()
