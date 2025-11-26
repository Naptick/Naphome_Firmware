#!/usr/bin/env python3
"""
Flash and test OpenAI Realtime interface
"""
import serial
import time
import subprocess
import sys
import glob
import argparse

def find_serial_port():
    """Find the ESP32 serial port"""
    ports = glob.glob('/dev/cu.*')
    usb_ports = [p for p in ports if 'usb' in p.lower() or 'serial' in p.lower()]
    
    # Try common port from previous sessions
    if not usb_ports and '/dev/cu.usbserial-110' in ports:
        usb_ports = ['/dev/cu.usbserial-110']
    
    if not usb_ports:
        usb_ports = [p for p in ports if 'Bluetooth' not in p and 'debug' not in p]
    
    return usb_ports[0] if usb_ports else None

def flash_firmware(port):
    """Flash the firmware to the device"""
    print("\n" + "="*80)
    print("FLASHING FIRMWARE")
    print("="*80)
    
    try:
        result = subprocess.run(
            ['idf.py', '-p', port, 'flash'],
            cwd='/Users/danielmcshan/GitHub/Naphome-Firmware/samples/korvo_voice_assistant',
            capture_output=True,
            text=True,
            timeout=120
        )
        
        if result.returncode == 0:
            print("✅ Flash successful")
            return True
        else:
            print(f"⚠️ Flash completed with warnings (code {result.returncode})")
            # Show last part of output
            output = result.stdout + result.stderr
            if output:
                print(output[-1000:])
            return result.returncode == 0
    except subprocess.TimeoutExpired:
        print("⏱️ Flash timed out")
        return False
    except Exception as e:
        print(f"❌ Flash error: {e}")
        return False

def monitor_realtime_interface(port, duration=45):
    """Monitor the OpenAI Realtime interface"""
    print("\n" + "="*80)
    print("TESTING OPENAI REALTIME INTERFACE")
    print("="*80)
    print(f"Monitoring for {duration} seconds...")
    print("")
    
    try:
        ser = serial.Serial(port, 115200, timeout=2)
        ser.reset_input_buffer()
        
        # Wait a bit for boot
        time.sleep(2)
        
        start = time.time()
        events = {
            'websocket': False,
            'session_update_sent': False,
            'session_created': False,
            'audio_sent': 0,
            'send_failed': 0,
            'transcript': 0,
            'error': 0,
            'all_events': [],
            'unhandled_events': []
        }
        
        while time.time() - start < duration:
            if ser.in_waiting:
                line = ser.readline()
                try:
                    text = line.decode('utf-8', errors='ignore').strip()
                    if text:
                        clean = text.replace('[0;32m', '').replace('[0;31m', '').replace('[0;33m', '').replace('[0m', '')
                        
                        # Key events
                        if 'WebSocket connected' in clean:
                            events['websocket'] = True
                            print(f'✅ WebSocket connected')
                        elif 'Sending session.update' in clean:
                            events['session_update_sent'] = True
                            print(f'📤 {clean[:120]}')
                        elif 'Session created:' in clean or 'session.created' in clean:
                            events['session_created'] = True
                            print(f'✅ {clean[:120]}')
                        elif 'Audio sent to OpenAI' in clean:
                            events['audio_sent'] += 1
                            if events['audio_sent'] <= 5:
                                print(f'📤 {clean[:120]}')
                        elif 'Received OpenAI event' in clean:
                            events['all_events'].append(clean)
                            if len(events['all_events']) <= 20:
                                print(f'📨 {clean[:120]}')
                        elif 'response.audio_transcript' in clean:
                            events['transcript'] += 1
                            if events['transcript'] <= 5:
                                print(f'💬 {clean[:120]}')
                        elif 'Failed to send audio' in clean:
                            events['send_failed'] += 1
                            if events['send_failed'] <= 3:
                                print(f'❌ {clean[:120]}')
                        elif 'OpenAI error' in clean or ('error' in clean.lower() and 'openai' in clean.lower()):
                            events['error'] += 1
                            if events['error'] <= 5:
                                print(f'⚠️ {clean[:120]}')
                        elif 'Unhandled event type' in clean:
                            events['unhandled_events'].append(clean)
                            if len(events['unhandled_events']) <= 5:
                                print(f'🔍 {clean[:120]}')
                except Exception as e:
                    pass
            else:
                time.sleep(0.05)
        
        ser.close()
        return events
        
    except Exception as e:
        print(f"❌ Error monitoring: {e}")
        return None

def print_summary(events):
    """Print test summary"""
    if not events:
        return
    
    print("\n" + "="*80)
    print("TEST RESULTS: OpenAI Realtime Interface")
    print("="*80)
    print(f"WebSocket connected: {events['websocket']}")
    print(f"Session update sent: {events['session_update_sent']}")
    print(f"Session created: {events['session_created']}")
    print(f"Audio sent confirmations: {events['audio_sent']}")
    print(f"Send failed attempts: {events['send_failed']}")
    print(f"Transcript events: {events['transcript']}")
    print(f"Errors: {events['error']}")
    print(f"Total events received: {len(events['all_events'])}")
    print(f"Unhandled events: {len(events['unhandled_events'])}")
    print("")
    
    # Determine status
    if events['session_created']:
        print("✅ SUCCESS - Session created! OpenAI Realtime interface is open")
        if events['audio_sent'] > 0:
            print(f"✅ Audio is being sent ({events['audio_sent']} confirmations)")
        else:
            print("⚠️ Session created but no audio sent yet (may be waiting for speech)")
        if events['transcript'] > 0:
            print(f"✅ Transcription working ({events['transcript']} events)")
    elif events['session_update_sent'] and events['websocket']:
        print("⚠️ Session update sent but no session.created received")
        print("   Possible issues:")
        print("   - API key may not have Realtime API access")
        print("   - OpenAI may be rejecting the session.update")
        print("   - Check OpenAI API status")
    elif events['websocket']:
        print("⚠️ WebSocket connected but session.update not sent or failed")
    else:
        print("❌ WebSocket not connected")
        print("   Check Wi-Fi connection and OpenAI API key")
    
    if events['unhandled_events']:
        print(f"\n🔍 Unhandled events ({len(events['unhandled_events'])}):")
        for event in events['unhandled_events'][:5]:
            print(f"   {event[:100]}")

def main():
    parser = argparse.ArgumentParser(description='Flash and test OpenAI Realtime interface')
    parser.add_argument('-p', '--port', help='Serial port (auto-detect if not specified)')
    parser.add_argument('-t', '--test-only', action='store_true', help='Skip flashing, only test')
    parser.add_argument('-d', '--duration', type=int, default=45, help='Test duration in seconds')
    args = parser.parse_args()
    
    # Find port
    port = args.port or find_serial_port()
    if not port:
        print("❌ No USB serial port found")
        print("Available ports:", glob.glob('/dev/cu.*'))
        print("\nPlease connect the ESP32 device and try again")
        sys.exit(1)
    
    print(f"📱 Using device: {port}")
    
    # Flash if not test-only
    if not args.test_only:
        if not flash_firmware(port):
            print("❌ Flash failed, aborting test")
            sys.exit(1)
        
        # Wait for device to boot
        print("\n" + "="*80)
        print("WAITING FOR DEVICE TO BOOT (10 seconds)")
        print("="*80)
        time.sleep(10)
    else:
        print("⏭️ Skipping flash (test-only mode)")
        time.sleep(2)
    
    # Monitor
    events = monitor_realtime_interface(port, args.duration)
    if events:
        print_summary(events)

if __name__ == '__main__':
    main()
