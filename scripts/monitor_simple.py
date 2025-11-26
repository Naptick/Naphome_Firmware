#!/usr/bin/env python3
"""
Simple Naptick Monitor - Shows status and logs
"""

import serial
import time
from datetime import datetime

# Status
wifi = "Unknown"
spotify = "Unknown"
aws = "Disabled"
wake_count = 0
last_wake = None

def update_status(line):
    global wifi, spotify, wake_count, last_wake
    l = line.lower()
    
    if 'wifi' in l:
        if 'connected' in l and 'chateau' in l:
            wifi = "Connected"
        elif 'disconnect' in l:
            wifi = "Disconnected"
        elif 'connect' in l:
            wifi = "Connecting"
    
    if 'spotify' in l:
        if 'init' in l and 'failed' not in l:
            spotify = "Ready"
        elif 'failed' in l:
            spotify = "Error"
    
    if 'wake' in l and ('detected' in l or 'energy' in l):
        wake_count += 1
        last_wake = datetime.now().strftime("%H:%M:%S")

def print_status():
    print("\n" + "="*70)
    print("  NAPTICK VOICE ASSISTANT STATUS")
    print("="*70)
    print(f"  Wi-Fi:    {wifi:15}  Spotify:  {spotify:15}")
    print(f"  AWS IoT:  {aws:15}  Wake Events: {wake_count}")
    if last_wake:
        print(f"  Last Wake: {last_wake}")
    print("="*70 + "\n")

def main():
    import sys
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-110"
    
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"Connected to {port}")
        print("Monitoring... (Press Ctrl+C to exit)\n")
        
        last_status = time.time()
        log_lines = []
        
        while True:
            if ser.in_waiting:
                line = ser.readline()
                text = line.decode('utf-8', errors='ignore').strip()
                if text:
                    update_status(text)
                    log_lines.append(text)
                    if len(log_lines) > 10:
                        log_lines.pop(0)
                    
                    # Show important logs
                    if any(x in text.lower() for x in ['wake', 'naptick', 'wifi', 'connected', 'error', 'ready', 'assistant']):
                        print(text)
            
            # Update status display every 2 seconds
            if time.time() - last_status > 2:
                print_status()
                print("Recent logs:")
                for log in log_lines[-5:]:
                    print(f"  {log}")
                last_status = time.time()
            
            time.sleep(0.1)
            
    except KeyboardInterrupt:
        print("\n\nStopped.")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        if 'ser' in locals():
            ser.close()

if __name__ == "__main__":
    main()
