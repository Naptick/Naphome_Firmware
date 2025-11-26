#!/usr/bin/env python3
"""
Naptick Voice Assistant Console Dashboard
Monitors serial logs and displays status indicators in the console.
"""

import serial
import re
import sys
from datetime import datetime
from collections import deque

# ANSI color codes
class Colors:
    GREEN = '\033[92m'
    ORANGE = '\033[93m'
    RED = '\033[91m'
    CYAN = '\033[96m'
    BLUE = '\033[94m'
    GRAY = '\033[90m'
    RESET = '\033[0m'
    BOLD = '\033[1m'

# Status tracking
class StatusTracker:
    def __init__(self):
        self.wifi_status = "Unknown"
        self.spotify_status = "Unknown"
        self.aws_status = "Disabled"
        self.wake_word_detected = False
        self.last_wake_time = None
        self.muted = False
        self.audio_playing = False
        self.stats = {
            'wake_events': 0,
            'wifi_connects': 0,
            'errors': 0,
        }

def parse_log_line(line, tracker):
    """Parse a log line and update status tracker."""
    if not line or len(line.strip()) == 0:
        return
    
    line_lower = line.lower()
    
    # Wi-Fi status
    if 'wifi' in line_lower:
        if 'connected' in line_lower and 'the chateau' in line_lower:
            tracker.wifi_status = "Connected"
            tracker.stats['wifi_connects'] += 1
        elif 'disconnect' in line_lower:
            tracker.wifi_status = "Disconnected"
        elif 'connect' in line_lower and 'ssid' in line_lower:
            tracker.wifi_status = "Connecting"
    
    # Spotify status
    if 'spotify' in line_lower:
        if 'init failed' in line_lower or 'failed to start' in line_lower:
            tracker.spotify_status = "Error"
        elif 'client init' in line_lower and 'failed' not in line_lower:
            tracker.spotify_status = "Ready"
        elif 'play' in line_lower or 'resume' in line_lower:
            tracker.spotify_status = "Playing"
        elif 'pause' in line_lower or 'stop' in line_lower:
            tracker.spotify_status = "Paused"
    
    # Wake word detection
    if 'wake' in line_lower and ('detected' in line_lower or 'energy' in line_lower or 'simulated' in line_lower):
        tracker.wake_word_detected = True
        tracker.last_wake_time = datetime.now()
        tracker.stats['wake_events'] += 1
    
    # Mute status
    if 'muted' in line_lower or 'mute' in line_lower:
        tracker.muted = 'muted' in line_lower or 'true' in line_lower
    
    # Audio playback
    if 'audio playback' in line_lower or 'tts' in line_lower:
        if 'start' in line_lower:
            tracker.audio_playing = True
        elif 'stop' in line_lower:
            tracker.audio_playing = False
    
    # Errors
    if 'error' in line_lower or 'failed' in line_lower:
        if 'stack overflow' not in line_lower:
            tracker.stats['errors'] += 1

def get_status_color(status):
    """Get ANSI color for a status."""
    status_lower = status.lower()
    if 'connected' in status_lower or 'ready' in status_lower or 'playing' in status_lower:
        return Colors.GREEN
    elif 'connecting' in status_lower or 'reconnecting' in status_lower:
        return Colors.ORANGE
    elif 'disconnected' in status_lower or 'error' in status_lower:
        return Colors.RED
    elif 'paused' in status_lower:
        return Colors.CYAN
    elif 'disabled' in status_lower:
        return Colors.GRAY
    else:
        return Colors.GRAY

def print_status(tracker):
    """Print current status dashboard."""
    # Clear screen and move cursor to top
    sys.stdout.write('\033[2J\033[H')
    
    print(f"{Colors.BOLD}╔════════════════════════════════════════════════════════════╗{Colors.RESET}")
    print(f"{Colors.BOLD}║{Colors.RESET}  {Colors.BOLD}Naptick Voice Assistant Status Dashboard{Colors.RESET}              {Colors.BOLD}║{Colors.RESET}")
    print(f"{Colors.BOLD}╠════════════════════════════════════════════════════════════╣{Colors.RESET}")
    
    # Wi-Fi Status
    wifi_color = get_status_color(tracker.wifi_status)
    print(f"{Colors.BOLD}║{Colors.RESET}  Wi-Fi:    {wifi_color}●{Colors.RESET} {wifi_color}{tracker.wifi_status:15}{Colors.RESET}                    {Colors.BOLD}║{Colors.RESET}")
    
    # Spotify Status
    spotify_color = get_status_color(tracker.spotify_status)
    print(f"{Colors.BOLD}║{Colors.RESET}  Spotify:  {spotify_color}●{Colors.RESET} {spotify_color}{tracker.spotify_status:15}{Colors.RESET}                    {Colors.BOLD}║{Colors.RESET}")
    
    # AWS Status
    aws_color = get_status_color(tracker.aws_status)
    print(f"{Colors.BOLD}║{Colors.RESET}  AWS IoT:  {aws_color}●{Colors.RESET} {aws_color}{tracker.aws_status:15}{Colors.RESET}                    {Colors.BOLD}║{Colors.RESET}")
    
    print(f"{Colors.BOLD}╠════════════════════════════════════════════════════════════╣{Colors.RESET}")
    
    # Wake Word
    if tracker.wake_word_detected:
        elapsed = (datetime.now() - tracker.last_wake_time).total_seconds() if tracker.last_wake_time else 0
        if elapsed < 2:
            wake_status = f"{Colors.ORANGE}DETECTED!{Colors.RESET}"
        else:
            wake_status = f"{Colors.GRAY}Last: {elapsed:.1f}s ago{Colors.RESET}"
    else:
        wake_status = f"{Colors.GRAY}Not detected{Colors.RESET}"
    print(f"{Colors.BOLD}║{Colors.RESET}  Wake Word: {wake_status:40} {Colors.BOLD}║{Colors.RESET}")
    
    # Mute
    mute_status = f"{Colors.RED}Muted{Colors.RESET}" if tracker.muted else f"{Colors.GRAY}Not muted{Colors.RESET}"
    print(f"{Colors.BOLD}║{Colors.RESET}  Mute:      {mute_status:40} {Colors.BOLD}║{Colors.RESET}")
    
    # Audio
    audio_status = f"{Colors.BLUE}Playing{Colors.RESET}" if tracker.audio_playing else f"{Colors.GRAY}Idle{Colors.RESET}"
    print(f"{Colors.BOLD}║{Colors.RESET}  Audio:     {audio_status:40} {Colors.BOLD}║{Colors.RESET}")
    
    print(f"{Colors.BOLD}╠════════════════════════════════════════════════════════════╣{Colors.RESET}")
    print(f"{Colors.BOLD}║{Colors.RESET}  Statistics:                                      {Colors.BOLD}║{Colors.RESET}")
    print(f"{Colors.BOLD}║{Colors.RESET}    Wake Events: {Colors.CYAN}{tracker.stats['wake_events']:3}{Colors.RESET}  "
          f"Wi-Fi Connects: {Colors.CYAN}{tracker.stats['wifi_connects']:3}{Colors.RESET}  "
          f"Errors: {Colors.RED}{tracker.stats['errors']:3}{Colors.RESET}     {Colors.BOLD}║{Colors.RESET}")
    print(f"{Colors.BOLD}╠════════════════════════════════════════════════════════════╣{Colors.RESET}")
    print(f"{Colors.BOLD}║{Colors.RESET}  Recent Logs (last 10 lines):                    {Colors.BOLD}║{Colors.RESET}")
    print(f"{Colors.BOLD}╚════════════════════════════════════════════════════════════╝{Colors.RESET}")
    print()

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="Naptick Voice Assistant Console Dashboard")
    parser.add_argument("--port", "-p", default="/dev/cu.usbserial-110",
                       help="Serial port (default: /dev/cu.usbserial-110)")
    parser.add_argument("--baud", "-b", type=int, default=115200,
                       help="Baud rate (default: 115200)")
    
    args = parser.parse_args()
    
    tracker = StatusTracker()
    log_buffer = deque(maxlen=10)
    
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
        print(f"{Colors.GREEN}Connected to {args.port} at {args.baud} baud{Colors.RESET}")
        print(f"{Colors.GRAY}Press Ctrl+C to exit{Colors.RESET}\n")
        
        while True:
            if ser.in_waiting:
                line = ser.readline()
                try:
                    text = line.decode('utf-8', errors='ignore').strip()
                    if text:
                        log_buffer.append(text)
                        parse_log_line(text, tracker)
                        print_status(tracker)
                        # Print recent logs
                        for log_line in log_buffer:
                            # Color code log lines
                            if 'error' in log_line.lower() or 'failed' in log_line.lower():
                                print(f"{Colors.RED}{log_line}{Colors.RESET}")
                            elif 'wake' in log_line.lower() or 'detected' in log_line.lower():
                                print(f"{Colors.ORANGE}{log_line}{Colors.RESET}")
                            elif 'connected' in log_line.lower() or 'ready' in log_line.lower():
                                print(f"{Colors.GREEN}{log_line}{Colors.RESET}")
                            else:
                                print(f"{Colors.GRAY}{log_line}{Colors.RESET}")
                except:
                    pass
            else:
                import time
                time.sleep(0.1)
                # Update display periodically even without new data
                if tracker.wake_word_detected:
                    print_status(tracker)
                    for log_line in log_buffer:
                        print(f"{Colors.GRAY}{log_line}{Colors.RESET}")
                    tracker.wake_word_detected = False
                
    except KeyboardInterrupt:
        print(f"\n{Colors.GREEN}Dashboard stopped.{Colors.RESET}")
    except Exception as e:
        print(f"{Colors.RED}Error: {e}{Colors.RESET}")
    finally:
        if 'ser' in locals():
            ser.close()

if __name__ == "__main__":
    main()
