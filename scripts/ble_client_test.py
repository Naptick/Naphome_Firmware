#!/usr/bin/env python3
"""
BLE Client Test Script - Connects to Somnus BLE device exactly like the Flutter mobile app.

This script replicates the Flutter app's BLE connection behavior:
1. Scans for "rpi-gatt-server" device
2. Connects to the Nordic UART Service
3. Subscribes to TX characteristic (notifications)
4. Can write JSON commands to RX characteristic
5. Handles chunked Wi-Fi list messages (WIFI_LIST_START/END/ERROR)

Usage:
    python scripts/ble_client_test.py [--device-name "rpi-gatt-server"] [--scan-timeout 10]
    python scripts/ble_client_test.py --scan-wifi          # Scan for Wi-Fi networks
    python scripts/ble_client_test.py --connect-wifi SSID PASSWORD TOKEN  # Connect to Wi-Fi
"""

import asyncio
import argparse
import json
import sys
from typing import Optional, List

try:
    from bleak import BleakClient, BleakScanner
    from bleak.backends.characteristic import BleakGATTCharacteristic
    from bleak.backends.device import BLEDevice
except ImportError:
    print("ERROR: bleak library not installed")
    print("Install with: pip install bleak")
    sys.exit(1)

# Nordic UART Service UUIDs (matches Flutter app)
NORDIC_UART_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NORDIC_UART_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # Notify
NORDIC_UART_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # Write

# Wi-Fi list markers (matches Flutter app)
WIFI_LIST_START = "WIFI_LIST_START"
WIFI_LIST_END = "WIFI_LIST_END"
WIFI_LIST_ERROR = "WIFI_LIST_ERROR"

DEFAULT_DEVICE_NAME = "rpi-gatt-server"


class BLEConnection:
    """Manages BLE connection state, matching Flutter app behavior."""
    
    def __init__(self):
        self.wifi_networks: List[str] = []  # Chunked Wi-Fi list messages
        self.collecting_wifi_list = False
        self.wifi_list_complete = asyncio.Event()
        self.parsed_wifi_list: List[dict] = []
    
    def reset_wifi_collection(self):
        """Reset Wi-Fi list collection state."""
        self.wifi_networks.clear()
        self.collecting_wifi_list = False
        self.wifi_list_complete.clear()
        self.parsed_wifi_list.clear()


# Global connection state
connection_state = BLEConnection()


async def scan_for_device(device_name: str, timeout: float = 10.0) -> Optional[BLEDevice]:
    """Scan for BLE device by name (matches Flutter app behavior)."""
    print(f"[SCAN] Looking for device: '{device_name}' (timeout: {timeout}s)...")
    
    devices = await BleakScanner.discover(timeout=timeout)
    
    print(f"[SCAN] Found {len(devices)} device(s):")
    for device in devices:
        name = device.name or "Unknown"
        print(f"  - {name} ({device.address})")
        if device.name and device_name.lower() in device.name.lower():
            print(f"[SCAN] ✓ Matched device: {device.name} ({device.address})")
            return device
    
    print(f"[SCAN] ✗ Device '{device_name}' not found")
    return None


def notification_handler(sender: BleakGATTCharacteristic, data: bytearray):
    """
    Handle incoming BLE notifications (matches Flutter app's _characteristicListener).
    
    Handles:
    - WIFI_LIST_START: Start collecting Wi-Fi list chunks
    - WIFI_LIST_END: End collection, parse and display Wi-Fi list
    - WIFI_LIST_ERROR: Error occurred
    - Other messages: Add to Wi-Fi list chunks or display directly
    """
    try:
        text = data.decode('utf-8', errors='replace')
        
        if text == WIFI_LIST_START:
            print(f"[BLE TX] {WIFI_LIST_START} - Starting Wi-Fi list collection")
            connection_state.reset_wifi_collection()
            connection_state.collecting_wifi_list = True
        elif text == WIFI_LIST_END:
            print(f"[BLE TX] {WIFI_LIST_END} - Wi-Fi list complete")
            connection_state.collecting_wifi_list = False
            connection_state.wifi_list_complete.set()
            # Parse and display Wi-Fi list
            parse_and_display_wifi_list()
        elif text == WIFI_LIST_ERROR:
            print(f"[BLE TX] {WIFI_LIST_ERROR} - Error occurred during Wi-Fi scan")
            connection_state.collecting_wifi_list = False
            connection_state.wifi_list_complete.set()
        elif connection_state.collecting_wifi_list:
            # Collect chunked Wi-Fi list messages
            connection_state.wifi_networks.append(text)
            print(f"[BLE TX] Wi-Fi chunk [{len(connection_state.wifi_networks)}]: {text[:80]}...")
        else:
            # Regular notification
            print(f"[BLE TX] Received: {text}")
    except Exception as e:
        print(f"[BLE TX] Received (hex): {data.hex()}")
        print(f"[BLE TX] Decode error: {e}")


def parse_and_display_wifi_list():
    """Parse collected Wi-Fi list chunks and display them (matches Flutter app logic)."""
    if not connection_state.wifi_networks:
        print("[WIFI] No Wi-Fi networks found")
        return
    
    # Concatenate all chunks (matches Flutter app: wifiJsonString += wifi)
    wifi_json_string = "".join(connection_state.wifi_networks)
    
    try:
        wifi_json = json.loads(wifi_json_string)
        if isinstance(wifi_json, list):
            connection_state.parsed_wifi_list = wifi_json
            print(f"\n[WIFI] Found {len(wifi_json)} networks:")
            print("-" * 60)
            for i, network in enumerate(wifi_json, 1):
                ssid = network.get("ssid", "Unknown")
                rssi = network.get("rssi", 0)
                auth = network.get("auth", "Unknown")
                print(f"  {i:2d}. {ssid:30s} (RSSI: {rssi:4d}, Auth: {auth})")
            print("-" * 60)
        else:
            print(f"[WIFI] Unexpected JSON format: {type(wifi_json)}")
    except json.JSONDecodeError as e:
        print(f"[WIFI] Failed to parse Wi-Fi list JSON: {e}")
        print(f"[WIFI] Raw data: {wifi_json_string[:200]}...")


async def send_command(client: BleakClient, command: str):
    """Send a command to the RX characteristic (matches Flutter app's writeCharacteristic)."""
    try:
        rx_char = client.services.get_characteristic(NORDIC_UART_RX_CHAR_UUID)
        if not rx_char:
            print(f"[ERROR] RX characteristic not found: {NORDIC_UART_RX_CHAR_UUID}")
            return False
        
        data = command.encode('utf-8')
        print(f"[BLE RX] Sending: {command} ({len(data)} bytes)")
        # Flutter app uses withResponse, but we use withoutResponse for compatibility
        await client.write_gatt_char(rx_char, data, response=False)
        print(f"[BLE RX] ✓ Command sent successfully")
        return True
    except Exception as e:
        print(f"[ERROR] Failed to send command: {e}")
        return False


async def send_json_command(client: BleakClient, json_data: dict):
    """Send a JSON command (matches Flutter app's encodeMessage)."""
    json_string = json.dumps(json_data)
    return await send_command(client, json_string)


async def scan_wifi(client: BleakClient):
    """Request Wi-Fi scan (matches Flutter app's fetchWifiNetworksList)."""
    print("\n[WIFI] Requesting Wi-Fi scan...")
    connection_state.reset_wifi_collection()
    
    # Send SCAN command (matches Flutter app)
    await send_json_command(client, {"action": "SCAN"})
    
    # Wait for Wi-Fi list to complete (with timeout)
    try:
        await asyncio.wait_for(connection_state.wifi_list_complete.wait(), timeout=30.0)
    except asyncio.TimeoutError:
        print("[WIFI] Timeout waiting for Wi-Fi list")
        connection_state.collecting_wifi_list = False


async def connect_wifi(client: BleakClient, ssid: str, password: str, token: str = "", is_production: bool = False):
    """Connect device to Wi-Fi (matches Flutter app's connectRpiToWifi)."""
    print(f"\n[WIFI] Connecting to Wi-Fi: {ssid}")
    
    command = {
        "action": "CONNECT_WIFI",
        "ssid": ssid,
        "password": password,
        "user_token": token,
        "is_production": is_production,
    }
    
    await send_json_command(client, command)
    print("[WIFI] ✓ Connection command sent")


async def interactive_mode(client: BleakClient):
    """Interactive mode - send commands and receive notifications."""
    print("\n" + "="*60)
    print("Interactive BLE Client Mode")
    print("="*60)
    print("Commands:")
    print("  scan          - Request Wi-Fi scan")
    print("  connect <ssid> <password> [token] - Connect to Wi-Fi")
    print("  json <json>   - Send raw JSON command")
    print("  quit/exit     - Disconnect and exit")
    print("="*60 + "\n")
    
    while True:
        try:
            user_input = input("BLE> ").strip()
            if not user_input:
                continue
            
            if user_input.lower() in ['quit', 'exit', 'q']:
                break
            
            parts = user_input.split(maxsplit=1)
            cmd = parts[0].lower()
            
            if cmd == 'scan':
                await scan_wifi(client)
            elif cmd == 'connect':
                if len(parts) < 2:
                    print("[ERROR] Usage: connect <ssid> <password> [token]")
                    continue
                connect_parts = parts[1].split(maxsplit=2)
                if len(connect_parts) < 2:
                    print("[ERROR] Usage: connect <ssid> <password> [token]")
                    continue
                token = connect_parts[2] if len(connect_parts) > 2 else ""
                await connect_wifi(client, connect_parts[0], connect_parts[1], token)
            elif cmd == 'json':
                if len(parts) < 2:
                    print("[ERROR] Usage: json <json_string>")
                    continue
                try:
                    json_data = json.loads(parts[1])
                    await send_json_command(client, json_data)
                except json.JSONDecodeError as e:
                    print(f"[ERROR] Invalid JSON: {e}")
            else:
                print(f"[ERROR] Unknown command: {cmd}")
                print("  Available: scan, connect, json, quit")
            
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"[ERROR] {e}")


async def main():
    parser = argparse.ArgumentParser(
        description="BLE Client Test for Somnus device (matches Flutter app behavior)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Interactive mode
  python scripts/ble_client_test.py

  # Scan for Wi-Fi networks
  python scripts/ble_client_test.py --scan-wifi

  # Connect to Wi-Fi
  python scripts/ble_client_test.py --connect-wifi "MyNetwork" "password123" "user_token"

  # Send custom JSON command
  python scripts/ble_client_test.py --json '{"action": "SCAN"}'
        """
    )
    parser.add_argument("--device-name", default=DEFAULT_DEVICE_NAME,
                       help=f"Device name to scan for (default: {DEFAULT_DEVICE_NAME})")
    parser.add_argument("--scan-timeout", type=float, default=10.0,
                       help="Scan timeout in seconds (default: 10.0)")
    parser.add_argument("--address", type=str, default=None,
                       help="Direct device address (skips scanning)")
    parser.add_argument("--scan-wifi", action="store_true",
                       help="Scan for Wi-Fi networks and exit")
    parser.add_argument("--connect-wifi", nargs=3, metavar=("SSID", "PASSWORD", "TOKEN"),
                       help="Connect device to Wi-Fi network")
    parser.add_argument("--json", type=str, default=None,
                       help="Send JSON command and exit")
    
    args = parser.parse_args()
    
    device = None
    
    if args.address:
        print(f"[CONNECT] Connecting directly to {args.address}")
        device = BLEDevice(args.address, args.device_name)
    else:
        device = await scan_for_device(args.device_name, args.scan_timeout)
        if not device:
            print("[ERROR] Device not found")
            return 1
    
    print(f"\n[CONNECT] Connecting to {device.name} ({device.address})...")
    
    try:
        async with BleakClient(device) as client:
            print(f"[CONNECT] ✓ Connected!")
            
            # Discover services (matches Flutter app's discoverGATT)
            print("\n[SERVICES] Discovering services...")
            for service in client.services:
                print(f"  Service: {service.uuid} ({service.description or 'Unknown'})")
                for char in service.characteristics:
                    props = []
                    if "read" in char.properties:
                        props.append("R")
                    if "write" in char.properties:
                        props.append("W")
                    if "notify" in char.properties:
                        props.append("N")
                    if "indicate" in char.properties:
                        props.append("I")
                    props_str = "".join(props) if props else "none"
                    print(f"    Characteristic: {char.uuid} [{props_str}] handle={char.handle}")
            
            # Check for Nordic UART Service
            nordic_service = client.services.get_service(NORDIC_UART_SERVICE_UUID)
            if not nordic_service:
                print(f"\n[ERROR] Nordic UART Service not found: {NORDIC_UART_SERVICE_UUID}")
                print("[ERROR] Available services:")
                for service in client.services:
                    print(f"  - {service.uuid}")
                return 1
            
            print(f"\n[SERVICE] ✓ Found Nordic UART Service")
            
            # Get TX characteristic (notifications) - matches Flutter app
            tx_char = nordic_service.get_characteristic(NORDIC_UART_TX_CHAR_UUID)
            if not tx_char:
                print(f"[ERROR] TX characteristic not found: {NORDIC_UART_TX_CHAR_UUID}")
                return 1
            
            print(f"[CHAR] ✓ Found TX characteristic (notify): {tx_char.uuid}")
            
            # Get RX characteristic (write) - matches Flutter app
            rx_char = nordic_service.get_characteristic(NORDIC_UART_RX_CHAR_UUID)
            if not rx_char:
                print(f"[ERROR] RX characteristic not found: {NORDIC_UART_RX_CHAR_UUID}")
                return 1
            
            print(f"[CHAR] ✓ Found RX characteristic (write): {rx_char.uuid}")
            
            # Subscribe to notifications (matches Flutter app's setCharacteristicNotifyState)
            print(f"\n[SUBSCRIBE] Enabling notifications on TX characteristic...")
            await client.start_notify(tx_char, notification_handler)
            print(f"[SUBSCRIBE] ✓ Notifications enabled - listening for messages...\n")
            
            # Wait a bit for notifications to be fully enabled (matches Flutter app delay)
            await asyncio.sleep(1)
            
            # Handle command-line actions
            if args.scan_wifi:
                await scan_wifi(client)
                await asyncio.sleep(1)  # Wait for final messages
            elif args.connect_wifi:
                ssid, password, token = args.connect_wifi
                await connect_wifi(client, ssid, password, token)
                await asyncio.sleep(2)  # Wait for response
            elif args.json:
                try:
                    json_data = json.loads(args.json)
                    await send_json_command(client, json_data)
                    await asyncio.sleep(2)  # Wait for response
                except json.JSONDecodeError as e:
                    print(f"[ERROR] Invalid JSON: {e}")
                    return 1
            else:
                # Interactive mode
                await interactive_mode(client)
            
            # Stop notifications
            await client.stop_notify(tx_char)
            print("\n[DISCONNECT] Disconnecting...")
    
    except Exception as e:
        print(f"[ERROR] Connection failed: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    print("[DONE] Disconnected")
    return 0


if __name__ == "__main__":
    try:
        exit_code = asyncio.run(main())
        sys.exit(exit_code)
    except KeyboardInterrupt:
        print("\n[INTERRUPT] Exiting...")
        sys.exit(0)
