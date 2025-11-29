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
    
    async with BleakClient(device) as client:
        print("Connected!")
        
        # Get services
        services = list(client.services)
        nus_service = None
        for s in services:
            if NUS_SERVICE_UUID.lower() in s.uuid.lower():
                nus_service = s
                break
        
        if not nus_service:
            print("NUS service not found!")
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
            return
        
        print(f"RX: {rx_char.uuid} (handle: {rx_char.handle})")
        print(f"TX: {tx_char.uuid} (handle: {tx_char.handle})")
        
        # Store handles for direct use
        rx_handle = rx_char.handle
        tx_handle = tx_char.handle
        
        # Subscribe using handle
        await client.start_notify(tx_handle, notification_handler)
        await asyncio.sleep(0.5)
        
        # Test SCAN - use handle directly (like Somnus-Flutter)
        print("\n=== Testing SCAN ===")
        responses.clear()
        cmd = json.dumps({"action": "SCAN"})
        print(f"[TX] {cmd}")
        # Use handle directly as integer
        await client.write_gatt_char(rx_handle, cmd.encode('utf-8'))
        await asyncio.sleep(10)
        print(f"Got {len(responses)} responses")
        for r in responses:
            print(f"  {r}")
        
        # Test READ_SENSORS
        print("\n=== Testing READ_SENSORS ===")
        responses.clear()
        cmd = json.dumps({"action": "READ_SENSORS"})
        print(f"[TX] {cmd}")
        # Use handle directly
        await client.write_gatt_char(rx_handle, cmd.encode('utf-8'))
        await asyncio.sleep(5)
        print(f"Got {len(responses)} responses")
        for r in responses:
            print(f"  {r}")

if __name__ == "__main__":
    asyncio.run(main())
