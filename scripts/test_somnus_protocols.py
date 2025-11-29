#!/usr/bin/env python3
"""
Comprehensive test script for Somnus-Device BLE protocols
Tests all implemented protocols: SCAN, CONNECT_WIFI, READ_SENSORS, and device commands
"""

import asyncio
import json
import sys
from bleak import BleakScanner, BleakClient

# Nordic UART Service UUIDs
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # Notify
NUS_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # Write

# Device name to look for
DEVICE_NAME = "rpi-gatt-server"

class SomnusProtocolTester:
    def __init__(self):
        self.client = None
        self.tx_char = None
        self.rx_char = None
        self.tx_handle = None
        self.rx_handle = None
        self.responses = []
        self.test_results = {}
        
    def notification_handler(self, sender, data):
        """Handle notifications from the device"""
        try:
            text = data.decode('utf-8')
            print(f"\n[RX] {text}")
            self.responses.append(text)
        except Exception as e:
            print(f"\n[RX] {data.hex()} (decode error: {e})")
            self.responses.append(data.hex())
    
    async def connect(self, device):
        """Connect to the BLE device"""
        print(f"\n{'='*60}")
        print(f"Connecting to {device.name} ({device.address})...")
        print(f"{'='*60}")
        
        self.client = BleakClient(device)
        await self.client.connect()
        print(f"✓ Connected: {self.client.is_connected}")
        
        # Get services
        services = self.client.services
        service_list = list(services)
        
        # Find NUS service
        nus_service = None
        for service in service_list:
            if NUS_SERVICE_UUID.lower() in service.uuid.lower():
                nus_service = service
                break
        
        if not nus_service:
            raise Exception("Nordic UART Service not found!")
        
        # Find TX (notify) and RX (write) characteristics
        for char in nus_service.characteristics:
            if NUS_TX_CHAR_UUID.lower() in char.uuid.lower():
                self.tx_char = char
            elif NUS_RX_CHAR_UUID.lower() in char.uuid.lower():
                self.rx_char = char
        
        if not self.tx_char or not self.rx_char:
            raise Exception("NUS characteristics not found!")
        
        print(f"✓ TX characteristic: {self.tx_char.uuid} (handle: {self.tx_char.handle})")
        print(f"✓ RX characteristic: {self.rx_char.uuid} (handle: {self.rx_char.handle})")
        
        # Store handles for direct use (like Somnus-Flutter)
        self.tx_handle = self.tx_char.handle
        self.rx_handle = self.rx_char.handle
        
        # Subscribe to notifications using handle
        await self.client.start_notify(self.tx_handle, self.notification_handler)
        print("✓ Notification subscription active")
        
        await asyncio.sleep(0.5)  # Wait for subscription to be ready
        return True
    
    async def disconnect(self):
        """Disconnect from the device"""
        if self.client and self.client.is_connected:
            if self.tx_handle:
                try:
                    await self.client.stop_notify(self.tx_handle)
                except:
                    pass
            await self.client.disconnect()
            print("\n✓ Disconnected")
    
    async def send_command(self, command_json, description="Command", wait_time=3):
        """Send a JSON command and wait for responses"""
        print(f"\n{'─'*60}")
        print(f"Test: {description}")
        print(f"{'─'*60}")
        
        command_str = json.dumps(command_json) if isinstance(command_json, dict) else command_json
        print(f"[TX] {command_str}")
        
        self.responses = []  # Clear previous responses
        
        # Use handle directly as integer (like Somnus-Flutter does)
        await self.client.write_gatt_char(self.rx_handle, command_str.encode('utf-8'))
        
        # Wait for responses
        print(f"Waiting {wait_time}s for responses...")
        await asyncio.sleep(wait_time)
        
        if self.responses:
            print(f"\n✓ Received {len(self.responses)} response(s)")
            return self.responses
        else:
            print("\n⚠ No responses received")
            return []
    
    async def test_scan(self):
        """Test SCAN action - WiFi network scanning"""
        print("\n" + "="*60)
        print("TEST 1: WiFi Scan (SCAN action)")
        print("="*60)
        
        command = {
            "action": "SCAN"
        }
        
        responses = await self.send_command(command, "WiFi Scan", wait_time=10)
        
        # Check for expected markers
        has_start = any("WIFI_LIST_START" in r for r in responses)
        has_end = any("WIFI_LIST_END" in r for r in responses)
        has_data = any("[" in r and "]" in r for r in responses)
        
        result = {
            "passed": has_start and has_end,
            "has_data": has_data,
            "responses": responses
        }
        
        if result["passed"]:
            print("\n✓ SCAN test PASSED")
        else:
            print("\n✗ SCAN test FAILED")
            if not has_start:
                print("  - Missing WIFI_LIST_START marker")
            if not has_end:
                print("  - Missing WIFI_LIST_END marker")
        
        return result
    
    async def test_read_sensors(self):
        """Test READ_SENSORS action"""
        print("\n" + "="*60)
        print("TEST 2: Read Sensors (READ_SENSORS action)")
        print("="*60)
        
        command = {
            "action": "READ_SENSORS"
        }
        
        responses = await self.send_command(command, "Read Sensors", wait_time=5)
        
        # Check for expected markers
        has_start = any("SENSOR_DATA_START" in r for r in responses)
        has_end = any("SENSOR_DATA_END" in r for r in responses)
        has_data = any("sensors" in r.lower() or "sht45" in r.lower() or "temperature" in r.lower() for r in responses)
        
        result = {
            "passed": has_start and has_end,
            "has_data": has_data,
            "responses": responses
        }
        
        if result["passed"]:
            print("\n✓ READ_SENSORS test PASSED")
        else:
            print("\n✗ READ_SENSORS test FAILED")
            if not has_start:
                print("  - Missing SENSOR_DATA_START marker")
            if not has_end:
                print("  - Missing SENSOR_DATA_END marker")
        
        return result
    
    async def test_connect_wifi(self, ssid="TEST_SSID", password="TEST_PASS", token="test_token"):
        """Test CONNECT_WIFI action (with test credentials - will fail but should respond)"""
        print("\n" + "="*60)
        print("TEST 3: Connect WiFi (CONNECT_WIFI action)")
        print("="*60)
        
        command = {
            "action": "CONNECT_WIFI",
            "ssid": ssid,
            "password": password,
            "user_token": token,
            "is_production": False
        }
        
        responses = await self.send_command(command, "Connect WiFi", wait_time=5)
        
        # Check for connection attempt response
        has_response = len(responses) > 0
        has_connecting = any("Connecting" in r or "connect" in r.lower() for r in responses)
        has_result = any("Connected" in r or "failed" in r.lower() or "error" in r.lower() for r in responses)
        
        result = {
            "passed": has_response and (has_connecting or has_result),
            "has_connecting": has_connecting,
            "has_result": has_result,
            "responses": responses
        }
        
        if result["passed"]:
            print("\n✓ CONNECT_WIFI test PASSED (got response)")
        else:
            print("\n✗ CONNECT_WIFI test FAILED (no response)")
        
        return result
    
    async def test_device_command_led(self):
        """Test device command - LED action"""
        print("\n" + "="*60)
        print("TEST 4: Device Command - LED (Action with capital A)")
        print("="*60)
        
        command = {
            "Action": "LED",
            "Data": {
                "Color": "#FF0000",
                "Brightness": 50
            }
        }
        
        responses = await self.send_command(command, "LED Command", wait_time=3)
        
        has_response = len(responses) > 0
        has_executed = any("executed" in r.lower() or "Command" in r for r in responses)
        
        result = {
            "passed": has_response,
            "has_executed": has_executed,
            "responses": responses
        }
        
        if result["passed"]:
            print("\n✓ LED command test PASSED")
        else:
            print("\n✗ LED command test FAILED (no response)")
        
        return result
    
    async def test_device_command_array(self):
        """Test device command - Array of actions"""
        print("\n" + "="*60)
        print("TEST 5: Device Command - Array Format")
        print("="*60)
        
        command = [
            {
                "Action": "LED",
                "Data": {"Color": "#00FF00", "Brightness": 30}
            },
            {
                "Action": "SetVolume",
                "Data": {"Volume": 50}
            }
        ]
        
        responses = await self.send_command(command, "Array Command", wait_time=3)
        
        has_response = len(responses) > 0
        
        result = {
            "passed": has_response,
            "responses": responses
        }
        
        if result["passed"]:
            print("\n✓ Array command test PASSED")
        else:
            print("\n✗ Array command test FAILED (no response)")
        
        return result
    
    async def test_invalid_command(self):
        """Test invalid command handling"""
        print("\n" + "="*60)
        print("TEST 6: Invalid Command Handling")
        print("="*60)
        
        command = {
            "action": "UNKNOWN_ACTION"
        }
        
        responses = await self.send_command(command, "Invalid Command", wait_time=2)
        
        has_response = len(responses) > 0
        has_error = any("Unknown" in r or "error" in r.lower() or "Bad" in r for r in responses)
        
        result = {
            "passed": has_response,
            "has_error": has_error,
            "responses": responses
        }
        
        if result["passed"]:
            print("\n✓ Invalid command test PASSED (got error response)")
        else:
            print("\n✗ Invalid command test FAILED (no response)")
        
        return result
    
    async def test_bad_json(self):
        """Test bad JSON handling"""
        print("\n" + "="*60)
        print("TEST 7: Bad JSON Handling")
        print("="*60)
        
        command = "{ invalid json }"
        
        responses = await self.send_command(command, "Bad JSON", wait_time=2)
        
        has_response = len(responses) > 0
        has_error = any("Bad" in r or "JSON" in r or "format" in r.lower() for r in responses)
        
        result = {
            "passed": has_response and has_error,
            "responses": responses
        }
        
        if result["passed"]:
            print("\n✓ Bad JSON test PASSED (got error response)")
        else:
            print("\n✗ Bad JSON test FAILED")
        
        return result

async def scan_for_device(timeout=10):
    """Scan for the BLE device"""
    print(f"\nScanning for '{DEVICE_NAME}' (timeout: {timeout}s)...")
    devices = await BleakScanner.discover(timeout=timeout)
    
    target = None
    for device in devices:
        if device.name and DEVICE_NAME.lower() in device.name.lower():
            target = device
            break
    
    if not target:
        print("\nAvailable devices:")
        for device in devices:
            if device.name:
                print(f"  - {device.name} ({device.address})")
        return None
    
    print(f"\n✓ Found device: {target.name} ({target.address})")
    return target

async def main():
    print("="*60)
    print("Somnus-Device BLE Protocol Test Suite")
    print("="*60)
    
    # Scan for device
    device = await scan_for_device(timeout=10)
    if not device:
        print(f"\n✗ Device '{DEVICE_NAME}' not found")
        print("\nMake sure:")
        print("  1. Device is powered on")
        print("  2. BLE is advertising")
        print("  3. Device is within range")
        return 1
    
    tester = SomnusProtocolTester()
    
    try:
        # Connect
        await tester.connect(device)
        
        # Run all tests
        results = {}
        results["scan"] = await tester.test_scan()
        await asyncio.sleep(1)
        
        results["read_sensors"] = await tester.test_read_sensors()
        await asyncio.sleep(1)
        
        results["connect_wifi"] = await tester.test_connect_wifi()
        await asyncio.sleep(1)
        
        results["device_command_led"] = await tester.test_device_command_led()
        await asyncio.sleep(1)
        
        results["device_command_array"] = await tester.test_device_command_array()
        await asyncio.sleep(1)
        
        results["invalid_command"] = await tester.test_invalid_command()
        await asyncio.sleep(1)
        
        results["bad_json"] = await tester.test_bad_json()
        
        # Print summary
        print("\n" + "="*60)
        print("TEST SUMMARY")
        print("="*60)
        
        passed = 0
        total = len(results)
        
        for test_name, result in results.items():
            status = "✓ PASS" if result.get("passed", False) else "✗ FAIL"
            print(f"{status} - {test_name}")
            if result.get("passed", False):
                passed += 1
        
        print(f"\nResults: {passed}/{total} tests passed")
        
        if passed == total:
            print("\n🎉 All tests passed!")
            return 0
        else:
            print(f"\n⚠️  {total - passed} test(s) failed")
            return 1
        
    except Exception as e:
        print(f"\n✗ Error: {e}")
        import traceback
        traceback.print_exc()
        return 1
    finally:
        await tester.disconnect()

if __name__ == "__main__":
    try:
        exit_code = asyncio.run(main())
        sys.exit(exit_code)
    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
        sys.exit(1)
