#!/usr/bin/env python3
"""
Simple test for Somnus BLE protocols - one command at a time
"""

import asyncio
import json
from bleak import BleakScanner, BleakClient

NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
DEVICE_NAME = "rpi-gatt-server"

responses = []

def notification_handler(sender, data):
    try:
        text = data.decode('utf-8')
        print(f"[RX] {text}")
        responses.append(text)
    except:
        print(f"[RX] {data.hex()}")

async def send_command_direct(client, rx_char_obj, tx_char_obj, cmd_json, wait_time=3, notification_handler=None):
    """Send command - reconnect if services invalidated"""
    cmd_str = json.dumps(cmd_json)
    print(f"[TX] {cmd_str}")
    
    try:
        # Try using the characteristic object
        await client.write_gatt_char(rx_char_obj, cmd_str.encode('utf-8'))
    except Exception as e:
        # If services are invalidated, reconnect and rediscover
        if "Service Discovery" in str(e) or "services" in str(e).lower():
            print(f"  Services invalidated, reconnecting...")
            await client.disconnect()
            await asyncio.sleep(0.5)
            await client.connect()
            # Rediscover services
            services = list(client.services)
            for s in services:
                if NUS_SERVICE_UUID.lower() in s.uuid.lower():
                    for char in s.characteristics:
                        if NUS_RX_CHAR_UUID.lower() in char.uuid.lower():
                            rx_char_obj = char
                        elif NUS_TX_CHAR_UUID.lower() in char.uuid.lower():
                            tx_char_obj = char
            # Resubscribe to notifications (only if not already subscribed)
            if notification_handler:
                try:
                    await client.start_notify(tx_char_obj, notification_handler)
                    await asyncio.sleep(0.3)
                except ValueError as ve:
                    if "already started" in str(ve):
                        # Already subscribed, that's fine
                        pass
                    else:
                        raise
            # Retry write
            await client.write_gatt_char(rx_char_obj, cmd_str.encode('utf-8'))
        else:
            raise
    await asyncio.sleep(wait_time)
    return rx_char_obj, tx_char_obj  # Return updated char objects

async def main():
    print("Scanning for device...")
    devices = await BleakScanner.discover(timeout=10)
    device = None
    for d in devices:
        if d.name and DEVICE_NAME.lower() in d.name.lower():
            device = d
            break
    
    if not device:
        print("Device not found!")
        return
    
    print(f"Found: {device.name}")
    
    client = BleakClient(device)
    await client.connect()
    print("Connected!")
    
    # Get services and characteristics
    services = list(client.services)
    nus_service = None
    for s in services:
        if NUS_SERVICE_UUID.lower() in s.uuid.lower():
            nus_service = s
            break
    
    if not nus_service:
        print("NUS service not found!")
        await client.disconnect()
        return
    
    # Find characteristics
    rx_char = None
    tx_char = None
    for char in nus_service.characteristics:
        if NUS_RX_CHAR_UUID.lower() in char.uuid.lower():
            rx_char = char
        elif NUS_TX_CHAR_UUID.lower() in char.uuid.lower():
            tx_char = char
    
    if not rx_char or not tx_char:
        print("Characteristics not found!")
        await client.disconnect()
        return
    
    print(f"RX: {rx_char.uuid} (handle: {rx_char.handle})")
    print(f"TX: {tx_char.uuid} (handle: {tx_char.handle})")
    
    # Subscribe using UUID
    await client.start_notify(NUS_TX_CHAR_UUID, notification_handler)
    await asyncio.sleep(0.5)
    print("✓ Subscribed to notifications")
    
    try:
        # Test SCAN
        print("\n=== Testing SCAN ===")
        responses.clear()
        rx_char, tx_char = await send_command_direct(client, rx_char, tx_char, {"action": "SCAN"}, wait_time=10, notification_handler=notification_handler)
        print(f"\n✓ Got {len(responses)} responses")
        for i, r in enumerate(responses, 1):
            print(f"  [{i}] {r[:150]}{'...' if len(r) > 150 else ''}")
        
        # Test READ_SENSORS
        print("\n=== Testing READ_SENSORS ===")
        responses.clear()
        rx_char, tx_char = await send_command_direct(client, rx_char, tx_char, {"action": "READ_SENSORS"}, wait_time=5, notification_handler=notification_handler)
        print(f"\n✓ Got {len(responses)} responses")
        for i, r in enumerate(responses, 1):
            print(f"  [{i}] {r[:150]}{'...' if len(r) > 150 else ''}")
        
        # Test device command
        print("\n=== Testing Device Command (LED) ===")
        responses.clear()
        rx_char, tx_char = await send_command_direct(client, rx_char, tx_char, {"Action": "LED", "Data": {"Color": "#FF0000", "Brightness": 50}}, wait_time=3, notification_handler=notification_handler)
        print(f"\n✓ Got {len(responses)} responses")
        for i, r in enumerate(responses, 1):
            print(f"  [{i}] {r}")
    finally:
        await client.disconnect()
        print("\n✓ Disconnected")

if __name__ == "__main__":
    asyncio.run(main())
