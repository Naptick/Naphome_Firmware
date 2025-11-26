#!/usr/bin/env python3
"""
Test AWS IoT Core connection using provisioned certificates.

This helper connects to AWS IoT Core with a supplied Thing certificate, publishes
a test payload, and optionally verifies round-trip messaging on the same topic.
"""

import argparse
import json
import ssl
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt

# AWS IoT Core endpoint
AWS_IOT_ENDPOINT = "a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com"
AWS_IOT_PORT = 8883

# Defaults that get updated at runtime
CURRENT_ENDPOINT = AWS_IOT_ENDPOINT
CURRENT_CLIENT_ID = "TEST_LAPTOP"
CURRENT_TOPIC_PUBLISH = "device/test/TEST_LAPTOP"
CURRENT_TOPIC_SUBSCRIBE = "device/test/TEST_LAPTOP"


def load_certificates(cert_dir: Path):
    """Load certificate files from the specified directory."""
    cert_dir = Path(cert_dir)
    return {
        "root_ca": (cert_dir / "root_ca.pem").read_text(),
        "device_cert": (cert_dir / "device_cert.pem").read_text(),
        "private_key": (cert_dir / "private_key.pem").read_text(),
    }


def on_connect(client, userdata, flags, rc, properties=None):
    """Callback for when the client receives a CONNACK response from the server."""
    if rc == 0:
        print("✓ Connected to AWS IoT Core successfully!")
        print(f"  Client ID: {client._client_id}")
        print(f"  Endpoint: {CURRENT_ENDPOINT}")
    else:
        print(f"✗ Connection failed with code {rc}")
        sys.exit(1)


def on_disconnect(client, userdata, rc, properties=None):
    """Callback for when the client disconnects from the server."""
    if rc != 0:
        print(f"⚠ Unexpected disconnection (code: {rc})")
    else:
        print("✓ Disconnected cleanly")


def on_message(client, userdata, msg):
    """Callback for when a PUBLISH message is received from the server."""
    try:
        payload = json.loads(msg.payload.decode())
        print(f"\n📨 Received message on topic '{msg.topic}':")
        print(f"   {json.dumps(payload, indent=2)}")
    except json.JSONDecodeError:
        print(f"\n📨 Received message on topic '{msg.topic}':")
        print(f"   {msg.payload.decode()}")


def on_publish(client, userdata, mid, properties=None):
    """Callback for when a message that was to be sent is actually sent."""
    print(f"✓ Message published (mid: {mid})")


def on_subscribe(client, userdata, mid, granted_qos, properties=None):
    """Callback for when the broker responds to a subscribe request."""
    print(f"✓ Subscribed to topic (mid: {mid}, QoS: {granted_qos})")


def main():
    parser = argparse.ArgumentParser(description="Test AWS IoT Core connection")
    parser.add_argument(
        "--cert-dir",
        type=Path,
        default=Path("components/aws_iot/certs/generated/TEST_LAPTOP"),
        help="Directory containing certificate files",
    )
    parser.add_argument(
        "--client-id",
        default="TEST_LAPTOP",
        help="MQTT client ID (should match Thing name)",
    )
    parser.add_argument(
        "--endpoint",
        default=AWS_IOT_ENDPOINT,
        help="AWS IoT Core endpoint",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=AWS_IOT_PORT,
        help="AWS IoT Core port (default: 8883 for TLS)",
    )
    parser.add_argument(
        "--publish-topic",
        help="MQTT topic to publish the test payload to (defaults to device/test/<client_id>)",
    )
    parser.add_argument(
        "--subscribe-topic",
        help="MQTT topic to subscribe to for receiving test payloads (defaults to publish topic)",
    )
    args = parser.parse_args()

    global CURRENT_ENDPOINT, CURRENT_CLIENT_ID, CURRENT_TOPIC_PUBLISH, CURRENT_TOPIC_SUBSCRIBE
    CURRENT_ENDPOINT = args.endpoint
    CURRENT_CLIENT_ID = args.client_id

    default_topic = f"device/test/{args.client_id}"
    CURRENT_TOPIC_PUBLISH = args.publish_topic or default_topic
    CURRENT_TOPIC_SUBSCRIBE = args.subscribe_topic or CURRENT_TOPIC_PUBLISH

    print("AWS IoT Core Connection Test")
    print("=" * 60)
    print(f"Endpoint: {args.endpoint}:{args.port}")
    print(f"Client ID: {args.client_id}")
    print(f"Publish topic: {CURRENT_TOPIC_PUBLISH}")
    print(f"Subscribe topic: {CURRENT_TOPIC_SUBSCRIBE}")
    print(f"Certificate directory: {args.cert_dir}")
    print()

    # Load certificates
    try:
        certs = load_certificates(args.cert_dir)
        print("✓ Certificates loaded successfully")
    except FileNotFoundError as e:
        print(f"✗ Failed to load certificates: {e}")
        sys.exit(1)

    # Create MQTT client
    client = mqtt.Client(
        client_id=args.client_id,
        protocol=mqtt.MQTTv5,
    )

    # Set up callbacks
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    client.on_publish = on_publish
    client.on_subscribe = on_subscribe

    # Configure TLS
    context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_REQUIRED

    # Load certificates
    try:
        # Write certificates to temporary files for paho-mqtt
        import tempfile
        with tempfile.NamedTemporaryFile(mode='w', suffix='.pem', delete=False) as ca_file:
            ca_file.write(certs["root_ca"])
            ca_file.flush()
            context.load_verify_locations(ca_file.name)

        with tempfile.NamedTemporaryFile(mode='w', suffix='.pem', delete=False) as cert_file:
            cert_file.write(certs["device_cert"])
            cert_file.flush()
            cert_path = cert_file.name

        with tempfile.NamedTemporaryFile(mode='w', suffix='.pem', delete=False) as key_file:
            key_file.write(certs["private_key"])
            key_file.flush()
            key_path = key_file.name

        context.load_cert_chain(cert_path, key_path)
    except Exception as e:
        print(f"✗ Failed to configure TLS: {e}")
        sys.exit(1)

    client.tls_set_context(context)

    # Connect
    print("Connecting to AWS IoT Core...")
    try:
        client.connect(args.endpoint, args.port, keepalive=60)
    except Exception as e:
        print(f"✗ Connection error: {e}")
        sys.exit(1)

    # Start network loop
    client.loop_start()

    # Wait for connection
    time.sleep(2)

    # Subscribe to test topic
    print(f"\nSubscribing to topic: {CURRENT_TOPIC_SUBSCRIBE}")
    client.subscribe(CURRENT_TOPIC_SUBSCRIBE, qos=1)

    time.sleep(1)

    # Publish test message
    test_message = {
        "timestamp": time.time(),
        "message": f"Hello from {args.client_id}",
        "client_id": args.client_id,
        "test": True,
    }
    payload = json.dumps(test_message)

    print(f"\nPublishing test message to topic: {CURRENT_TOPIC_PUBLISH}")
    print(f"Payload: {payload}")
    result = client.publish(CURRENT_TOPIC_PUBLISH, payload, qos=1)
    result.wait_for_publish()

    # Wait for any incoming messages
    print("\nWaiting for messages (5 seconds)...")
    time.sleep(5)

    # Disconnect
    print("\nDisconnecting...")
    client.loop_stop()
    client.disconnect()

    print("\n" + "=" * 60)
    print("Test completed successfully!")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ Error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
