#!/usr/bin/env python3
"""
ESP32-S3 Reboot and Initialization Verification Script
Reboots the device, monitors serial output, and verifies all systems are initialized.
"""

import serial
import serial.tools.list_ports
import time
import sys
import re
from datetime import datetime
from typing import Optional, Dict, List, Tuple

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

class DeviceVerifier:
    def __init__(self, port: Optional[str] = None, baud: int = 115200, verbose: bool = False):
        self.port = port
        self.baud = baud
        self.ser: Optional[serial.Serial] = None
        self.verbose = verbose
        self.initialization_status = {
            'bootloader': False,
            'wifi': False,
            'ip_address': None,
            'led_controller': False,
            'audio': False,
            'voice_pipeline': False,
            'openai_realtime': False,
            'websocket_connected': False,
            'session_created': False,
            'wakenet': False,
            'spotify': False,
            'aws_iot': False,
            'errors': [],
            'warnings': [],
        }
        self.boot_time = None
        self.ready_time = None
        
        # Patterns to detect initialization
        self.patterns = {
            'bootloader': [
                re.compile(r'ESP-IDF v[\d.]+'),
                re.compile(r'2nd stage bootloader'),
                re.compile(r'ESP-ROM:'),
            ],
            'wifi_connected': re.compile(r'Wi-Fi connected|connected with.*aid =|sta ip: ([\d.]+)'),
            'ip_address': re.compile(r'IP:([\d.]+)|sta ip: ([\d.]+)|IP address'),
            'led_controller': re.compile(r'LED controller initialized|WS2812 strip|led_controller:'),
            'audio': re.compile(r'Korvo-1 microphone initialized|I2S driver installed|korvo_audio:'),
            'voice_pipeline': re.compile(r'Starting continuous realtime audio streaming|Pipeline: I2S|voice_pipeline:'),
            'openai_realtime': re.compile(r'OpenAI Realtime API started|openai_client:.*Realtime'),
            'websocket_connected': re.compile(r'WebSocket connected to OpenAI Realtime API|websocket_client:.*connected'),
            'session_created': re.compile(r'Session created: ([^\s]+)'),
            'assistant_ready': re.compile(r'Assistant ready:'),
            'wakenet': re.compile(r'WakeNet.*enabled|WakeNet9l|WakeNet.*local control'),
            'spotify': re.compile(r'Spotify.*initialized|spotify_client: Init'),
            'aws_iot': re.compile(r'AWS IoT.*connected|aws_iot.*initialized'),
            'error': re.compile(r'\[0;31mE \(|E \([0-9]+\) .*:.*error|failed|Failed'),
            'warning': re.compile(r'\[0;33mW \(|W \([0-9]+\) .*:.*warn|Warning'),
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
    
    def reset_device(self, method: str = 'rts') -> bool:
        """Reset the ESP32 device using RTS or DTR."""
        if not self.ser or not self.ser.is_open:
            print(f"{Colors.RED}Error: Not connected to serial port{Colors.RESET}")
            return False
        
        print(f"{Colors.YELLOW}Resetting device via {method.upper()}...{Colors.RESET}")
        
        try:
            if method == 'rts':
                # RTS reset: pull low then high
                self.ser.setRTS(True)
                time.sleep(0.1)
                self.ser.setRTS(False)
                time.sleep(0.1)
                self.ser.setRTS(True)
                time.sleep(0.1)
            elif method == 'dtr':
                # DTR reset: pull low then high
                self.ser.setDTR(False)
                time.sleep(0.1)
                self.ser.setDTR(True)
                time.sleep(0.1)
            elif method == 'both':
                # Both RTS and DTR
                self.ser.setRTS(False)
                self.ser.setDTR(False)
                time.sleep(0.1)
                self.ser.setRTS(True)
                self.ser.setDTR(True)
                time.sleep(0.1)
            
            print(f"{Colors.GREEN}Reset complete, waiting for boot...{Colors.RESET}")
            time.sleep(2)  # Wait for device to boot
            return True
        except Exception as e:
            print(f"{Colors.RED}Reset failed: {e}{Colors.RESET}")
            return False
    
    def connect(self) -> bool:
        """Connect to the serial port."""
        port = self.find_serial_port()
        if not port:
            print(f"{Colors.RED}Error: No serial port found{Colors.RESET}")
            return False
        
        try:
            self.ser = serial.Serial(port, self.baud, timeout=2)
            self.port = port
            print(f"{Colors.GREEN}Connected to {port} at {self.baud} baud{Colors.RESET}")
            return True
        except Exception as e:
            print(f"{Colors.RED}Error connecting to {port}: {e}{Colors.RESET}")
            return False
    
    def parse_log_line(self, line: str) -> Dict:
        """Parse a log line and extract initialization information."""
        result = {'matched': False, 'type': None, 'data': {}}
        
        # Check each pattern
        for pattern_type, patterns in self.patterns.items():
            if isinstance(patterns, list):
                for pattern in patterns:
                    match = pattern.search(line)
                    if match:
                        result['matched'] = True
                        result['type'] = pattern_type
                        if match.groups():
                            result['data'] = {'groups': match.groups()}
                        break
            else:
                match = patterns.search(line)
                if match:
                    result['matched'] = True
                    result['type'] = pattern_type
                    if match.groups():
                        result['data'] = {'groups': match.groups()}
                    break
        
        return result
    
    def monitor_boot(self, timeout: float = 60.0) -> bool:
        """Monitor the boot process and detect initialization."""
        if not self.ser or not self.ser.is_open:
            print(f"{Colors.RED}Error: Not connected to serial port{Colors.RESET}")
            return False
        
        print(f"{Colors.CYAN}Monitoring boot process (timeout: {timeout}s)...{Colors.RESET}")
        print(f"{Colors.GRAY}{'='*80}{Colors.RESET}")
        print(f"{Colors.GRAY}Waiting for device to boot and send logs...{Colors.RESET}")
        
        start_time = time.time()
        boot_detected = False
        last_log_time = start_time
        log_buffer = []
        line_count = 0
        
        # Reset initialization status
        self.initialization_status = {
            'bootloader': False,
            'wifi': False,
            'ip_address': None,
            'led_controller': False,
            'audio': False,
            'voice_pipeline': False,
            'openai_realtime': False,
            'websocket_connected': False,
            'session_created': False,
            'wakenet': False,
            'spotify': False,
            'aws_iot': False,
            'errors': [],
            'warnings': [],
        }
        
        try:
            # Flush any existing data
            self.ser.reset_input_buffer()
            
            while time.time() - start_time < timeout:
                if self.ser.in_waiting:
                    line = self.ser.readline()
                    try:
                        text = line.decode('utf-8', errors='ignore').strip()
                        if text:
                            last_log_time = time.time()
                            line_count += 1
                            log_buffer.append((time.time() - start_time, text))
                            
                            # Keep only last 100 lines
                            if len(log_buffer) > 100:
                                log_buffer.pop(0)
                            
                            # Parse the line
                            parsed = self.parse_log_line(text)
                            
                            if parsed['matched']:
                                ptype = parsed['type']
                                
                                # Update status based on detected patterns
                                if ptype == 'bootloader' and not boot_detected:
                                    boot_detected = True
                                    self.boot_time = time.time() - start_time
                                    self.initialization_status['bootloader'] = True
                                    print(f"{Colors.GREEN}[{self.boot_time:.1f}s] Bootloader detected{Colors.RESET}")
                                
                                elif ptype == 'wifi_connected':
                                    self.initialization_status['wifi'] = True
                                    print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] Wi-Fi connected{Colors.RESET}")
                                
                                elif ptype == 'ip_address':
                                    groups = parsed['data'].get('groups', [])
                                    ip = groups[0] if groups and groups[0] else (groups[1] if len(groups) > 1 and groups[1] else None)
                                    if ip:
                                        self.initialization_status['ip_address'] = ip
                                        print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] IP address: {ip}{Colors.RESET}")
                                
                                elif ptype == 'led_controller':
                                    self.initialization_status['led_controller'] = True
                                    print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] LED controller initialized{Colors.RESET}")
                                
                                elif ptype == 'audio':
                                    self.initialization_status['audio'] = True
                                    print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] Audio system initialized{Colors.RESET}")
                                
                                elif ptype == 'voice_pipeline':
                                    self.initialization_status['voice_pipeline'] = True
                                    print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] Voice pipeline started{Colors.RESET}")
                                
                                elif ptype == 'openai_realtime':
                                    self.initialization_status['openai_realtime'] = True
                                    print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] OpenAI Realtime API started{Colors.RESET}")
                                
                                elif ptype == 'websocket_connected':
                                    self.initialization_status['websocket_connected'] = True
                                    print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] WebSocket connected{Colors.RESET}")
                                
                                elif ptype == 'session_created':
                                    groups = parsed['data'].get('groups', [])
                                    session_id = groups[0] if groups else None
                                    if session_id:
                                        self.initialization_status['session_created'] = True
                                        print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] Session created: {session_id[:20]}...{Colors.RESET}")
                                
                                elif ptype == 'assistant_ready':
                                    self.ready_time = time.time() - start_time
                                    print(f"{Colors.GREEN}{Colors.BOLD}[{self.ready_time:.1f}s] Assistant ready!{Colors.RESET}")
                                
                                elif ptype == 'wakenet':
                                    self.initialization_status['wakenet'] = True
                                    print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] WakeNet enabled{Colors.RESET}")
                                
                                elif ptype == 'spotify':
                                    self.initialization_status['spotify'] = True
                                    print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] Spotify initialized{Colors.RESET}")
                                
                                elif ptype == 'aws_iot':
                                    self.initialization_status['aws_iot'] = True
                                    print(f"{Colors.GREEN}[{time.time() - start_time:.1f}s] AWS IoT initialized{Colors.RESET}")
                                
                                elif ptype == 'error':
                                    error_text = text[:100] if len(text) > 100 else text
                                    self.initialization_status['errors'].append(error_text)
                                    print(f"{Colors.RED}[{time.time() - start_time:.1f}s] ERROR: {error_text}{Colors.RESET}")
                                
                                elif ptype == 'warning':
                                    warning_text = text[:100] if len(text) > 100 else text
                                    self.initialization_status['warnings'].append(warning_text)
                                    print(f"{Colors.YELLOW}[{time.time() - start_time:.1f}s] WARNING: {warning_text}{Colors.RESET}")
                            
                            # Show non-matched lines based on verbosity
                            if self.verbose:
                                # Verbose mode: show all logs except debug
                                if 'D (' not in text:
                                    clean_text = text.replace('[0;32m', '').replace('[0;31m', '').replace('[0;33m', '').replace('[0m', '')
                                    print(f"{Colors.GRAY}[{time.time() - start_time:.1f}s] {clean_text[:120]}{Colors.RESET}")
                            elif line_count < 100 or any(x in text.lower() for x in ['assistant ready', 'initialized', 'started', 'connected', 'pipeline', 'afe', 'openai', 'vad', 'energy', 'websocket', 'session', 'error', 'warn']):
                                # Only show if it's not too verbose
                                if len(text) < 150 and not any(x in text for x in ['D (', 'heap_init', 'nvs_get', 'wifi:eb is']):
                                    clean_text = text.replace('[0;32m', '').replace('[0;31m', '').replace('[0;33m', '').replace('[0m', '')
                                    # Show first 100 lines always, then filter for important
                                    if line_count < 100 or any(x in clean_text.lower() for x in ['i (', 'e (', 'w (']):
                                        print(f"{Colors.GRAY}[{time.time() - start_time:.1f}s] {clean_text[:100]}{Colors.RESET}")
                    except Exception as decode_err:
                        pass
                
                # Check for timeout (no logs for 10 seconds after boot)
                elif boot_detected and (time.time() - last_log_time > 10):
                    print(f"{Colors.YELLOW}No logs for 10 seconds, assuming boot complete{Colors.RESET}")
                    break
                
                else:
                    time.sleep(0.1)
        
        except KeyboardInterrupt:
            print(f"\n{Colors.YELLOW}Monitoring interrupted by user{Colors.RESET}")
        except Exception as e:
            print(f"{Colors.RED}Monitoring error: {e}{Colors.RESET}")
        
        print(f"{Colors.GRAY}{'='*80}{Colors.RESET}")
        return True
    
    def print_summary(self):
        """Print a summary of initialization status."""
        print(f"\n{Colors.BOLD}{'='*80}{Colors.RESET}")
        print(f"{Colors.BOLD}Initialization Summary{Colors.RESET}")
        print(f"{Colors.BOLD}{'='*80}{Colors.RESET}\n")
        
        # Core systems
        print(f"{Colors.BOLD}Core Systems:{Colors.RESET}")
        systems = [
            ('Bootloader', self.initialization_status['bootloader']),
            ('Wi-Fi', self.initialization_status['wifi']),
            ('IP Address', self.initialization_status['ip_address']),
            ('LED Controller', self.initialization_status['led_controller']),
            ('Audio System', self.initialization_status['audio']),
        ]
        
        for name, status in systems:
            icon = f"{Colors.GREEN}✓{Colors.RESET}" if status else f"{Colors.RED}✗{Colors.RESET}"
            value = self.initialization_status.get('ip_address', 'N/A') if name == 'IP Address' and status else ('Yes' if status else 'No')
            print(f"  {icon} {name}: {value}")
        
        # Voice pipeline
        print(f"\n{Colors.BOLD}Voice Pipeline:{Colors.RESET}")
        voice_systems = [
            ('Voice Pipeline', self.initialization_status['voice_pipeline']),
            ('OpenAI Realtime', self.initialization_status['openai_realtime']),
            ('WebSocket Connected', self.initialization_status['websocket_connected']),
            ('Session Created', self.initialization_status['session_created']),
            ('WakeNet', self.initialization_status['wakenet']),
        ]
        
        for name, status in voice_systems:
            icon = f"{Colors.GREEN}✓{Colors.RESET}" if status else f"{Colors.RED}✗{Colors.RESET}"
            print(f"  {icon} {name}: {'Yes' if status else 'No'}")
        
        # Optional services
        print(f"\n{Colors.BOLD}Optional Services:{Colors.RESET}")
        optional = [
            ('Spotify', self.initialization_status['spotify']),
            ('AWS IoT', self.initialization_status['aws_iot']),
        ]
        
        for name, status in optional:
            icon = f"{Colors.GREEN}✓{Colors.RESET}" if status else f"{Colors.YELLOW}○{Colors.RESET}"
            print(f"  {icon} {name}: {'Yes' if status else 'No (optional)'}")
        
        # Timing
        if self.boot_time:
            print(f"\n{Colors.BOLD}Timing:{Colors.RESET}")
            print(f"  Boot detected: {self.boot_time:.1f}s")
            if self.ready_time:
                print(f"  Assistant ready: {self.ready_time:.1f}s")
                print(f"  Total boot time: {self.ready_time:.1f}s")
        
        # Errors and warnings
        if self.initialization_status['errors']:
            print(f"\n{Colors.RED}{Colors.BOLD}Errors ({len(self.initialization_status['errors'])}):{Colors.RESET}")
            for error in self.initialization_status['errors'][:10]:  # Show first 10
                print(f"  • {error}")
            if len(self.initialization_status['errors']) > 10:
                print(f"  ... and {len(self.initialization_status['errors']) - 10} more")
        
        if self.initialization_status['warnings']:
            print(f"\n{Colors.YELLOW}{Colors.BOLD}Warnings ({len(self.initialization_status['warnings'])}):{Colors.RESET}")
            for warning in self.initialization_status['warnings'][:10]:  # Show first 10
                print(f"  • {warning}")
            if len(self.initialization_status['warnings']) > 10:
                print(f"  ... and {len(self.initialization_status['warnings']) - 10} more")
        
        # Overall status
        print(f"\n{Colors.BOLD}{'='*80}{Colors.RESET}")
        core_ready = all([
            self.initialization_status['bootloader'],
            self.initialization_status['wifi'],
            self.initialization_status['led_controller'],
            self.initialization_status['audio'],
        ])
        
        voice_ready = all([
            self.initialization_status['voice_pipeline'],
            self.initialization_status['openai_realtime'],
            self.initialization_status['websocket_connected'],
        ])
        
        if core_ready and voice_ready:
            print(f"{Colors.GREEN}{Colors.BOLD}✓ Device fully initialized and ready!{Colors.RESET}")
        elif core_ready:
            print(f"{Colors.YELLOW}{Colors.BOLD}⚠ Core systems ready, but voice pipeline incomplete{Colors.RESET}")
        else:
            print(f"{Colors.RED}{Colors.BOLD}✗ Device initialization incomplete{Colors.RESET}")
        print(f"{Colors.BOLD}{'='*80}{Colors.RESET}\n")
    
    def run_full_test(self, reset_method: str = 'rts', monitor_timeout: float = 60.0, verbose: bool = False) -> bool:
        self.verbose = verbose
        """Run the full reboot and verification test."""
        print(f"{Colors.BOLD}{'='*80}{Colors.RESET}")
        print(f"{Colors.BOLD}ESP32-S3 Reboot and Verification Test{Colors.RESET}")
        print(f"{Colors.BOLD}{'='*80}{Colors.RESET}\n")
        
        # Step 1: Connect
        if not self.connect():
            return False
        
        # Step 2: Reset device
        if not self.reset_device(reset_method):
            return False
        
        # Step 3: Monitor boot
        self.monitor_boot(monitor_timeout)
        
        # Step 4: Print summary
        self.print_summary()
        
        # Cleanup
        if self.ser:
            self.ser.close()
        
        return True


def main():
    import argparse
    
    parser = argparse.ArgumentParser(
        description="Reboot ESP32-S3 and verify initialization",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Basic reboot and verify
  %(prog)s
  
  # Specify port and reset method
  %(prog)s --port /dev/cu.usbserial-110 --reset dtr
  
  # Longer monitoring timeout
  %(prog)s --timeout 90
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
        "--reset", "-r",
        choices=['rts', 'dtr', 'both'],
        default='rts',
        help="Reset method (default: rts)"
    )
    parser.add_argument(
        "--timeout", "-t",
        type=float,
        default=60.0,
        help="Monitoring timeout in seconds (default: 60)"
    )
    parser.add_argument(
        "--no-reset",
        action="store_true",
        help="Skip device reset, just monitor"
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Show all logs (verbose mode)"
    )
    
    args = parser.parse_args()
    
    verifier = DeviceVerifier(port=args.port, baud=args.baud, verbose=args.verbose)
    
    if args.no_reset:
        # Just connect and monitor
        if verifier.connect():
            verifier.monitor_boot(args.timeout)
            verifier.print_summary()
            if verifier.ser:
                verifier.ser.close()
    else:
        # Full reboot and verify
        verifier.run_full_test(reset_method=args.reset, monitor_timeout=args.timeout, verbose=args.verbose)


if __name__ == "__main__":
    main()