#!/usr/bin/env python3
"""
Interactive Voice Assistant Test Script
Prompts user to speak and monitors ESP32 logs for STT, GPT, TTS, and WakeNet events.
"""

import serial
import serial.tools.list_ports
import time
import sys
import re
import threading
from datetime import datetime
from collections import deque
from typing import Optional, Dict, List

# ANSI color codes
class Colors:
    RESET = '\033[0m'
    BOLD = '\033[1m'
    RED = '\033[31m'
    GREEN = '\033[32m'
    YELLOW = '\033[33m'
    BLUE = '\033[34m'
    MAGENTA = '\033[35m'
    CYAN = '\033[36m'
    GRAY = '\033[90m'
    ORANGE = '\033[38;5;208m'

class VoiceInteractionTester:
    def __init__(self, port: Optional[str] = None, baud: int = 115200):
        self.port = port
        self.baud = baud
        self.ser: Optional[serial.Serial] = None
        self.running = False
        self.log_buffer = deque(maxlen=200)  # Keep last 200 log lines
        self.events = []  # Track important events
        self.current_transcription = ""
        self.last_event_time = time.time()
        
        # Event patterns to track
        self.patterns = {
            'stt_final': re.compile(r'OpenAI STT \(FINAL\): "([^"]+)"'),
            'stt_partial': re.compile(r'OpenAI STT \(partial\): "([^"]+)"'),
            'gpt_response': re.compile(r'GPT Chat Response: "([^"]+)"'),
            'tts_generated': re.compile(r'TTS generated (\d+) bytes'),
            'wake_word': re.compile(r'\*\*\* WAKE WORD DETECTED \(local control\): ([^(]+)'),
            'session_created': re.compile(r'Session created: ([^\s]+)'),
            'websocket_connected': re.compile(r'WebSocket connected to OpenAI Realtime API'),
            'websocket_error': re.compile(r'OpenAI error:|Realtime API error:'),
            'gpt_sending': re.compile(r'Sending to GPT-4o chat: "([^"]+)"'),
            'pipeline_ready': re.compile(r'Assistant ready:'),
        }
    
    def find_serial_port(self) -> Optional[str]:
        """Find the serial port to use."""
        if self.port:
            try:
                ser = serial.Serial(self.port, self.baud, timeout=1)
                ser.close()
                return self.port
            except (serial.SerialException, OSError):
                pass
        
        # Try common ports
        common_ports = [
            "/dev/cu.usbserial-110",
            "/dev/cu.usbserial-0001",
            "/dev/cu.SLAB_USBtoUART",
            "/dev/ttyUSB0",
            "/dev/ttyACM0",
        ]
        
        for port in common_ports:
            try:
                ser = serial.Serial(port, self.baud, timeout=1)
                ser.close()
                return port
            except (serial.SerialException, OSError):
                continue
        
        # List all available ports
        ports = serial.tools.list_ports.comports()
        if ports:
            print(f"{Colors.CYAN}Available serial ports:{Colors.RESET}")
            for p in ports:
                print(f"  {p.device} - {p.description}")
            if ports:
                return ports[0].device
        
        return None
    
    def parse_log_line(self, line: str) -> Dict:
        """Parse a log line and extract important information."""
        event = {
            'time': datetime.now(),
            'raw': line,
            'type': None,
            'data': {}
        }
        
        # Check each pattern
        for event_type, pattern in self.patterns.items():
            match = pattern.search(line)
            if match:
                event['type'] = event_type
                if match.groups():
                    # For numeric values (like TTS bytes), use 'value', otherwise 'text'
                    if event_type in ['tts_generated']:
                        event['data'] = {'value': match.group(1)}
                    else:
                        event['data'] = {'text': match.group(1)}
                else:
                    event['data'] = {}
                break
        
        return event
    
    def format_log_line(self, line: str, event: Optional[Dict] = None) -> str:
        """Format a log line with appropriate colors."""
        if not event or not event.get('type'):
            # Check for error/warning keywords
            if any(x in line.lower() for x in ['error', 'failed', 'fail']):
                return f"{Colors.RED}{line}{Colors.RESET}"
            elif 'warn' in line.lower():
                return f"{Colors.YELLOW}{line}{Colors.RESET}"
            else:
                return f"{Colors.GRAY}{line}{Colors.RESET}"
        
        event_type = event['type']
        
        if event_type == 'stt_final':
            text = event['data'].get('text', '')
            return f"{Colors.GREEN}{Colors.BOLD}>>> STT FINAL: \"{text}\"{Colors.RESET}"
        elif event_type == 'stt_partial':
            text = event['data'].get('text', '')
            return f"{Colors.CYAN}>>> STT partial: \"{text}\"{Colors.RESET}"
        elif event_type == 'gpt_sending':
            text = event['data'].get('text', '')
            return f"{Colors.BLUE}>>> Sending to GPT: \"{text}\"{Colors.RESET}"
        elif event_type == 'gpt_response':
            text = event['data'].get('text', '')
            return f"{Colors.MAGENTA}{Colors.BOLD}>>> GPT Response: \"{text}\"{Colors.RESET}"
        elif event_type == 'tts_generated':
            bytes_val = event['data'].get('value', event['data'].get('text', '0'))
            return f"{Colors.YELLOW}>>> TTS generated: {bytes_val} bytes{Colors.RESET}"
        elif event_type == 'wake_word':
            word = event['data'].get('text', '').strip()
            return f"{Colors.ORANGE}{Colors.BOLD}>>> WAKE WORD: {word}{Colors.RESET}"
        elif event_type == 'session_created':
            session_id = event['data'].get('text', '')
            return f"{Colors.GREEN}>>> Session created: {session_id}{Colors.RESET}"
        elif event_type == 'websocket_connected':
            return f"{Colors.GREEN}>>> WebSocket connected{Colors.RESET}"
        elif event_type == 'websocket_error':
            return f"{Colors.RED}{Colors.BOLD}>>> ERROR: {line}{Colors.RESET}"
        elif event_type == 'pipeline_ready':
            return f"{Colors.GREEN}{Colors.BOLD}>>> Assistant ready!{Colors.RESET}"
        else:
            return line
    
    def read_serial_thread(self):
        """Background thread to read from serial port."""
        while self.running:
            try:
                if self.ser and self.ser.in_waiting:
                    line = self.ser.readline()
                    try:
                        text = line.decode('utf-8', errors='ignore').strip()
                        if text:
                            self.log_buffer.append(text)
                            event = self.parse_log_line(text)
                            
                            if event.get('type'):
                                self.events.append(event)
                                self.last_event_time = time.time()
                                
                                # Update current transcription
                                if event['type'] == 'stt_final':
                                    self.current_transcription = event['data'].get('text', '')
                            
                            # Print formatted line
                            formatted = self.format_log_line(text, event)
                            print(formatted)
                    except Exception as e:
                        pass
                else:
                    time.sleep(0.01)
            except Exception as e:
                if self.running:
                    print(f"{Colors.RED}Serial read error: {e}{Colors.RESET}")
                time.sleep(0.1)
    
    def connect(self) -> bool:
        """Connect to the serial port."""
        port = self.find_serial_port()
        if not port:
            print(f"{Colors.RED}Error: No serial port found{Colors.RESET}")
            return False
        
        try:
            self.ser = serial.Serial(port, self.baud, timeout=1)
            self.port = port
            print(f"{Colors.GREEN}Connected to {port} at {self.baud} baud{Colors.RESET}")
            return True
        except Exception as e:
            print(f"{Colors.RED}Error connecting to {port}: {e}{Colors.RESET}")
            return False
    
    def start_monitoring(self):
        """Start monitoring the serial port."""
        if not self.ser or not self.ser.is_open:
            print(f"{Colors.RED}Error: Not connected to serial port{Colors.RESET}")
            return False
        
        self.running = True
        self.events.clear()
        self.log_buffer.clear()
        
        # Start reading thread
        read_thread = threading.Thread(target=self.read_serial_thread, daemon=True)
        read_thread.start()
        
        return True
    
    def stop_monitoring(self):
        """Stop monitoring."""
        self.running = False
        time.sleep(0.5)  # Give thread time to finish
    
    def wait_for_events(self, timeout: float = 30.0, min_events: int = 1) -> List[Dict]:
        """Wait for events with a timeout."""
        start_time = time.time()
        initial_event_count = len(self.events)
        
        while time.time() - start_time < timeout:
            if len(self.events) >= initial_event_count + min_events:
                break
            time.sleep(0.1)
        
        # Return new events
        return self.events[initial_event_count:]
    
    def print_summary(self, events: List[Dict]):
        """Print a summary of events."""
        if not events:
            print(f"{Colors.YELLOW}No events captured{Colors.RESET}")
            return
        
        print(f"\n{Colors.BOLD}{'='*80}{Colors.RESET}")
        print(f"{Colors.BOLD}Event Summary ({len(events)} events){Colors.RESET}")
        print(f"{Colors.BOLD}{'='*80}{Colors.RESET}\n")
        
        stt_finals = [e for e in events if e.get('type') == 'stt_final']
        gpt_responses = [e for e in events if e.get('type') == 'gpt_response']
        wake_words = [e for e in events if e.get('type') == 'wake_word']
        tts_events = [e for e in events if e.get('type') == 'tts_generated']
        
        if stt_finals:
            print(f"{Colors.GREEN}STT Transcriptions ({len(stt_finals)}):{Colors.RESET}")
            for e in stt_finals:
                text = e['data'].get('text', '')
                print(f"  • \"{text}\"")
        
        if gpt_responses:
            print(f"\n{Colors.MAGENTA}GPT Responses ({len(gpt_responses)}):{Colors.RESET}")
            for e in gpt_responses:
                text = e['data'].get('text', '')
                print(f"  • \"{text}\"")
        
        if wake_words:
            print(f"\n{Colors.ORANGE}Wake Word Detections ({len(wake_words)}):{Colors.RESET}")
            for e in wake_words:
                word = e['data'].get('text', '').strip()
                print(f"  • {word}")
        
        if tts_events:
            print(f"\n{Colors.YELLOW}TTS Audio Generated ({len(tts_events)}):{Colors.RESET}")
            for e in tts_events:
                bytes_val = e['data'].get('value', e['data'].get('text', '0'))
                print(f"  • {bytes_val} bytes")
        
        print(f"\n{Colors.BOLD}{'='*80}{Colors.RESET}\n")
    
    def run_continuous_monitoring(self):
        """Run continuous monitoring without prompts."""
        if not self.connect():
            return
        
        print(f"\n{Colors.BOLD}{'='*80}{Colors.RESET}")
        print(f"{Colors.BOLD}Voice Assistant Continuous Monitoring{Colors.RESET}")
        print(f"{Colors.BOLD}{'='*80}{Colors.RESET}\n")
        print(f"{Colors.CYAN}Monitoring logs from {self.port}...{Colors.RESET}")
        print(f"{Colors.YELLOW}Say something or trigger wake word 'hi esp' for local control{Colors.RESET}")
        print(f"{Colors.GRAY}Press Ctrl+C to exit, 's' + Enter for summary{Colors.RESET}\n")
        
        if not self.start_monitoring():
            return
        
        # Wait a bit for initial logs
        time.sleep(2)
        
        try:
            last_summary_time = time.time()
            last_event_count = 0
            
            while self.running:
                time.sleep(0.1)
                
                # Check for new events and print summary when significant events occur
                if len(self.events) > last_event_count:
                    # Significant new events - show a quick summary
                    new_events = self.events[last_event_count:]
                    important_events = [e for e in new_events if e.get('type') in 
                                       ['stt_final', 'gpt_response', 'wake_word', 'tts_generated']]
                    
                    if important_events:
                        print(f"\n{Colors.CYAN}--- New Events Detected ---{Colors.RESET}")
                        self.print_summary(important_events)
                    
                    last_event_count = len(self.events)
                
                # Auto-summary every 60 seconds if there are events
                if time.time() - last_summary_time > 60 and self.events:
                    current_time = time.time()
                    recent_events = [e for e in self.events 
                                   if isinstance(e['time'], datetime) and 
                                   (current_time - e['time'].timestamp()) < 60]
                    if recent_events:
                        print(f"\n{Colors.CYAN}--- Last 60 seconds summary ---{Colors.RESET}")
                        self.print_summary(recent_events)
                        last_summary_time = time.time()
                
        except KeyboardInterrupt:
            print(f"\n{Colors.YELLOW}Interrupted by user{Colors.RESET}")
        finally:
            self.stop_monitoring()
            if self.ser:
                self.ser.close()
            print(f"\n{Colors.GREEN}Disconnected{Colors.RESET}")
            
            # Final summary
            if self.events:
                print(f"\n{Colors.BOLD}Final Summary:{Colors.RESET}")
                self.print_summary(self.events)
    
    def run_interactive_test(self):
        """Run an interactive test session with prompts."""
        if not self.connect():
            return
        
        print(f"\n{Colors.BOLD}{'='*80}{Colors.RESET}")
        print(f"{Colors.BOLD}Voice Assistant Interactive Test{Colors.RESET}")
        print(f"{Colors.BOLD}{'='*80}{Colors.RESET}\n")
        print(f"{Colors.CYAN}Monitoring logs from {self.port}...{Colors.RESET}")
        print(f"{Colors.GRAY}Press Ctrl+C to exit{Colors.RESET}\n")
        
        if not self.start_monitoring():
            return
        
        # Wait a bit for initial logs
        time.sleep(2)
        
        try:
            while True:
                # Clear screen for better visibility
                print(f"\n{Colors.BOLD}{'─'*80}{Colors.RESET}")
                print(f"{Colors.CYAN}Ready for voice interaction{Colors.RESET}")
                print(f"{Colors.YELLOW}Say something or trigger wake word 'hi esp' for local control{Colors.RESET}")
                print(f"{Colors.GRAY}Press Enter to start listening, 'q' to quit, 's' for summary{Colors.RESET}")
                
                user_input = input().strip().lower()
                
                if user_input == 'q':
                    break
                elif user_input == 's':
                    self.print_summary(self.events)
                    continue
                
                # Clear previous events for this interaction
                event_count_before = len(self.events)
                
                print(f"\n{Colors.GREEN}Listening... (waiting up to 30 seconds for response){Colors.RESET}")
                print(f"{Colors.GRAY}{'─'*80}{Colors.RESET}\n")
                
                # Wait for events
                new_events = self.wait_for_events(timeout=30.0, min_events=0)
                
                if new_events:
                    print(f"\n{Colors.GREEN}Captured {len(new_events)} new events{Colors.RESET}")
                    self.print_summary(new_events)
                else:
                    print(f"{Colors.YELLOW}No new events captured in 30 seconds{Colors.RESET}")
                
        except KeyboardInterrupt:
            print(f"\n{Colors.YELLOW}Interrupted by user{Colors.RESET}")
        finally:
            self.stop_monitoring()
            if self.ser:
                self.ser.close()
            print(f"\n{Colors.GREEN}Disconnected{Colors.RESET}")
            
            # Final summary
            if self.events:
                print(f"\n{Colors.BOLD}Final Summary:{Colors.RESET}")
                self.print_summary(self.events)


def main():
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Interactive Voice Assistant Test - Monitor STT, GPT, TTS, and WakeNet events",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Interactive mode with prompts
  %(prog)s
  
  # Continuous monitoring mode
  %(prog)s --continuous
  
  # Specify serial port
  %(prog)s --port /dev/cu.usbserial-110
        """
    )
    parser.add_argument(
        "--port", "-p",
        help="Serial port (default: auto-detect)"
    )
    parser.add_argument(
        "--baud", "-b",
        type=int,
        default=115200,
        help="Baud rate (default: 115200)"
    )
    parser.add_argument(
        "--continuous", "-c",
        action="store_true",
        help="Run in continuous monitoring mode (no prompts, just watch logs)"
    )
    
    args = parser.parse_args()
    
    tester = VoiceInteractionTester(port=args.port, baud=args.baud)
    
    if args.continuous:
        tester.run_continuous_monitoring()
    else:
        tester.run_interactive_test()


if __name__ == "__main__":
    main()