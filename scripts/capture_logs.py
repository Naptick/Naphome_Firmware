#!/usr/bin/env python3
"""
Capture ESP32-S3 logs with device reset
Resets the device and captures serial output to a file for review.
"""

import serial
import serial.tools.list_ports
import time
import sys
import argparse
from datetime import datetime
from pathlib import Path

def find_serial_port(port_hint=None):
    """Find the serial port to use."""
    if port_hint:
        # Check if the hint is a valid port
        try:
            ser = serial.Serial(port_hint, 115200, timeout=1)
            ser.close()
            return port_hint
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
            ser = serial.Serial(port, 115200, timeout=1)
            ser.close()
            return port
        except (serial.SerialException, OSError):
            continue
    
    # List all available ports
    ports = serial.tools.list_ports.comports()
    if ports:
        print("Available serial ports:")
        for p in ports:
            print(f"  {p.device} - {p.description}")
        return ports[0].device
    
    return None

def reset_device(ser, method='rts'):
    """Reset the ESP32 device using RTS or DTR."""
    print(f"Resetting device via {method.upper()}...")
    
    if method == 'rts':
        # RTS reset: pull low then high
        ser.setRTS(True)
        time.sleep(0.1)
        ser.setRTS(False)
        time.sleep(0.1)
        ser.setRTS(True)
        time.sleep(0.1)
    elif method == 'dtr':
        # DTR reset: pull low then high
        ser.setDTR(False)
        time.sleep(0.1)
        ser.setDTR(True)
        time.sleep(0.1)
    elif method == 'both':
        # Both RTS and DTR
        ser.setRTS(False)
        ser.setDTR(False)
        time.sleep(0.1)
        ser.setRTS(True)
        ser.setDTR(True)
        time.sleep(0.1)
    
    print("Reset complete, waiting for boot...")
    time.sleep(2)  # Wait for device to boot

def capture_logs(port, duration=30, output_file=None, baudrate=115200, reset=True, reset_method='rts'):
    """Capture logs from the serial port."""
    
    # Determine output file
    if output_file is None:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_file = f"logs/capture_{timestamp}.log"
    
    # Create logs directory if needed
    log_path = Path(output_file)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    
    print(f"Connecting to {port} at {baudrate} baud...")
    
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            timeout=1,
            rtscts=False,
            dsrdtr=False
        )
        
        print(f"Connected to {port}")
        print(f"Capturing logs for {duration} seconds...")
        print(f"Output file: {output_file}")
        print("-" * 80)
        
        # Reset device if requested
        if reset:
            reset_device(ser, reset_method)
        
        # Open output file
        with open(output_file, 'w', encoding='utf-8') as f:
            f.write(f"Log capture started: {datetime.now().isoformat()}\n")
            f.write(f"Port: {port}, Baudrate: {baudrate}\n")
            f.write(f"Duration: {duration} seconds\n")
            f.write("-" * 80 + "\n\n")
            
            start_time = time.time()
            last_activity = time.time()
            line_count = 0
            byte_count = 0
            
            # Also print to console
            print(f"\n[{datetime.now().strftime('%H:%M:%S')}] Starting capture...\n")
            
            while True:
                elapsed = time.time() - start_time
                
                # Check duration
                if elapsed >= duration:
                    print(f"\n[{datetime.now().strftime('%H:%M:%S')}] Duration reached ({duration}s)")
                    break
                
                # Read from serial
                if ser.in_waiting > 0:
                    try:
                        data = ser.read(ser.in_waiting)
                        byte_count += len(data)
                        text = data.decode('utf-8', errors='replace')
                        
                        # Write to file
                        f.write(text)
                        f.flush()
                        
                        # Print to console (with timestamp for important lines)
                        lines = text.split('\n')
                        for line in lines:
                            if line.strip():
                                line_count += 1
                                last_activity = time.time()
                                
                                # Print important lines to console
                                line_lower = line.lower().strip()
                                is_important = any(keyword in line_lower for keyword in [
                                    'error', 'warn', 'mic level', 'led', 'wake word', 
                                    'afe', 'es7210', 'i2s', 'first', 'sample', 'level'
                                ])
                                
                                if is_important or line_count % 50 == 0:
                                    timestamp = datetime.now().strftime('%H:%M:%S')
                                    print(f"[{timestamp}] {line}")
                    except serial.SerialException as e:
                        print(f"Serial error: {e}")
                        break
                
                # Check for inactivity timeout (optional)
                if time.time() - last_activity > 60 and line_count > 0:
                    print(f"\n[{datetime.now().strftime('%H:%M:%S')}] No activity for 60s, stopping...")
                    break
                
                time.sleep(0.01)  # Small delay to avoid CPU spinning
            
            # Write summary
            elapsed = time.time() - start_time
            summary = f"\n\n{'='*80}\n"
            summary += f"Capture Summary\n"
            summary += f"{'='*80}\n"
            summary += f"Duration: {elapsed:.1f} seconds\n"
            summary += f"Lines captured: {line_count}\n"
            summary += f"Bytes captured: {byte_count}\n"
            summary += f"End time: {datetime.now().isoformat()}\n"
            summary += f"{'='*80}\n"
            
            f.write(summary)
            print(summary)
        
        print(f"\nLogs saved to: {output_file}")
        return output_file
        
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        return None
    except KeyboardInterrupt:
        print("\n\nCapture interrupted by user")
        if 'ser' in locals():
            ser.close()
        return output_file if 'output_file' in locals() else None
    except Exception as e:
        print(f"Error: {e}")
        return None
    finally:
        if 'ser' in locals():
            ser.close()
            print("Serial port closed")

def main():
    parser = argparse.ArgumentParser(
        description='Capture ESP32-S3 logs with device reset',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Capture 30 seconds of logs with auto-reset
  python capture_logs.py

  # Capture 60 seconds to a specific file
  python capture_logs.py -d 60 -o my_log.txt

  # Capture without reset
  python capture_logs.py --no-reset

  # Use specific port
  python capture_logs.py -p /dev/cu.usbserial-110
        """
    )
    
    parser.add_argument(
        '-p', '--port',
        help='Serial port (default: auto-detect)',
        default=None
    )
    
    parser.add_argument(
        '-d', '--duration',
        type=int,
        help='Capture duration in seconds (default: 30)',
        default=30
    )
    
    parser.add_argument(
        '-o', '--output',
        help='Output file (default: logs/capture_TIMESTAMP.log)',
        default=None
    )
    
    parser.add_argument(
        '-b', '--baudrate',
        type=int,
        help='Serial baudrate (default: 115200)',
        default=115200
    )
    
    parser.add_argument(
        '--no-reset',
        action='store_true',
        help='Do not reset the device before capturing'
    )
    
    parser.add_argument(
        '--reset-method',
        choices=['rts', 'dtr', 'both'],
        default='rts',
        help='Reset method: rts, dtr, or both (default: rts)'
    )
    
    args = parser.parse_args()
    
    # Find serial port
    port = args.port or find_serial_port()
    
    if not port:
        print("Error: No serial port found")
        print("Please specify a port with -p/--port or connect a device")
        sys.exit(1)
    
    # Capture logs
    output_file = capture_logs(
        port=port,
        duration=args.duration,
        output_file=args.output,
        baudrate=args.baudrate,
        reset=not args.no_reset,
        reset_method=args.reset_method
    )
    
    if output_file:
        print(f"\n✓ Capture complete: {output_file}")
        sys.exit(0)
    else:
        print("\n✗ Capture failed")
        sys.exit(1)

if __name__ == "__main__":
    main()
