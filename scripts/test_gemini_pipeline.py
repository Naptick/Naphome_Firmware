#!/usr/bin/env python3
"""
Test Gemini STT-LLM-TTS pipeline by playing pre-generated audio files.
Monitors device logs to validate the complete pipeline.
"""

import os
import sys
import json
import serial
import time
import argparse
import re
from pathlib import Path
from typing import List, Dict, Optional
from collections import deque
import wave

# ANSI colors
class Colors:
    RESET = '\033[0m'
    BOLD = '\033[1m'
    GREEN = '\033[32m'
    YELLOW = '\033[33m'
    RED = '\033[31m'
    BLUE = '\033[34m'
    CYAN = '\033[36m'
    MAGENTA = '\033[35m'

class PipelineTester:
    def __init__(self, port: str, audio_dir: str, baud: int = 115200):
        self.port = port
        self.baud = baud
        self.audio_dir = Path(audio_dir)
        self.ser: Optional[serial.Serial] = None
        self.running = False
        self.log_buffer = deque(maxlen=500)
        self.test_results = []
        
        # Patterns to track
        self.patterns = {
            'stt_transcript': re.compile(r'\[Gemini STT\].*transcript.*: "([^"]+)"', re.IGNORECASE),
            'stt_sending': re.compile(r'\[Gemini STT\].*Sending audio', re.IGNORECASE),
            'llm_response': re.compile(r'\[Gemini LLM\].*Response: "([^"]+)"', re.IGNORECASE),
            'llm_function_call': re.compile(r'\[Gemini LLM\].*Function call.*: (\w+)', re.IGNORECASE),
            'tts_generating': re.compile(r'\[Gemini TTS\].*Generating speech', re.IGNORECASE),
            'tts_success': re.compile(r'\[Gemini TTS\].*Success.*(\d+) bytes', re.IGNORECASE),
            'tts_playing': re.compile(r'Playing TTS audio', re.IGNORECASE),
            'error': re.compile(r'❌|ERROR|Error|error', re.IGNORECASE),
        }
    
    def connect_serial(self):
        """Connect to device serial port."""
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=1)
            print(f"{Colors.GREEN}✅ Connected to {self.port}{Colors.RESET}")
            return True
        except Exception as e:
            print(f"{Colors.RED}❌ Failed to connect: {e}{Colors.RESET}")
            return False
    
    def read_logs(self, duration: float = 30.0):
        """Read logs from serial port for specified duration."""
        if not self.ser:
            return
        
        start_time = time.time()
        while time.time() - start_time < duration:
            if self.ser.in_waiting:
                try:
                    line = self.ser.readline()
                    text = line.decode('utf-8', errors='ignore').strip()
                    if text and len(text) < 1000:
                        self.log_buffer.append({
                            'time': time.time(),
                            'text': text
                        })
                        # Print relevant lines
                        if any(p.search(text) for p in self.patterns.values()):
                            print(f"{Colors.CYAN}[LOG]{Colors.RESET} {text}")
                except:
                    pass
            time.sleep(0.01)
    
    def analyze_logs(self, start_time: float, end_time: float) -> Dict:
        """Analyze logs for pipeline events."""
        events = {
            'stt_detected': False,
            'stt_text': None,
            'llm_detected': False,
            'llm_response': None,
            'llm_function_call': None,
            'tts_detected': False,
            'tts_bytes': None,
            'tts_playing': False,
            'errors': []
        }
        
        for log in self.log_buffer:
            if start_time <= log['time'] <= end_time:
                text = log['text']
                
                # STT
                match = self.patterns['stt_transcript'].search(text)
                if match:
                    events['stt_detected'] = True
                    events['stt_text'] = match.group(1)
                
                if self.patterns['stt_sending'].search(text):
                    events['stt_detected'] = True
                
                # LLM
                match = self.patterns['llm_response'].search(text)
                if match:
                    events['llm_detected'] = True
                    events['llm_response'] = match.group(1)
                
                match = self.patterns['llm_function_call'].search(text)
                if match:
                    events['llm_detected'] = True
                    events['llm_function_call'] = match.group(1)
                
                # TTS
                if self.patterns['tts_generating'].search(text):
                    events['tts_detected'] = True
                
                match = self.patterns['tts_success'].search(text)
                if match:
                    events['tts_detected'] = True
                    events['tts_bytes'] = int(match.group(1))
                
                if self.patterns['tts_playing'].search(text):
                    events['tts_playing'] = True
                
                # Errors
                if self.patterns['error'].search(text):
                    events['errors'].append(text)
        
        return events
    
    def play_audio_file(self, audio_file: Path) -> bool:
        """Play audio file to device (simulated - would need actual audio injection)."""
        print(f"{Colors.YELLOW}📢 Playing: {audio_file.name}{Colors.RESET}")
        print(f"{Colors.YELLOW}   Note: Audio injection not implemented. Please play manually or use audio loopback.{Colors.RESET}")
        
        # In a real implementation, you would:
        # 1. Read the WAV file
        # 2. Convert to PCM samples
        # 3. Inject into device's audio input (via I2S or audio loopback)
        # For now, we'll just wait and monitor logs
        
        try:
            with wave.open(str(audio_file), 'rb') as wf:
                duration = wf.getnframes() / wf.getframerate()
                print(f"   Duration: {duration:.2f}s")
        except:
            duration = 3.0  # Default
        
        return duration
    
    def test_command(self, audio_file: Path, expected_text: str) -> Dict:
        """Test a single command."""
        print(f"\n{Colors.BOLD}{'='*60}{Colors.RESET}")
        print(f"{Colors.BOLD}Testing: {expected_text}{Colors.RESET}")
        print(f"{Colors.BOLD}File: {audio_file.name}{Colors.RESET}")
        
        start_time = time.time()
        
        # Play audio (simulated)
        duration = self.play_audio_file(audio_file)
        
        # Wait for audio to finish + processing time
        wait_time = duration + 10.0  # Extra time for processing
        print(f"{Colors.BLUE}⏳ Monitoring logs for {wait_time:.1f}s...{Colors.RESET}")
        self.read_logs(wait_time)
        
        end_time = time.time()
        
        # Analyze results
        events = self.analyze_logs(start_time, end_time)
        
        # Print results
        print(f"\n{Colors.BOLD}Results:{Colors.RESET}")
        print(f"  STT: {'✅' if events['stt_detected'] else '❌'} {events['stt_text'] or 'Not detected'}")
        print(f"  LLM: {'✅' if events['llm_detected'] else '❌'} {events['llm_response'] or events['llm_function_call'] or 'Not detected'}")
        print(f"  TTS: {'✅' if events['tts_detected'] else '❌'} {events['tts_bytes'] or 'Not generated'}")
        print(f"  Playing: {'✅' if events['tts_playing'] else '❌'}")
        
        if events['errors']:
            print(f"  {Colors.RED}Errors: {len(events['errors'])}{Colors.RESET}")
            for err in events['errors'][:3]:
                print(f"    - {err[:100]}")
        
        # Determine success
        success = (
            events['stt_detected'] and
            events['llm_detected'] and
            events['tts_detected']
        )
        
        result = {
            'file': audio_file.name,
            'expected_text': expected_text,
            'success': success,
            'events': events,
            'duration': end_time - start_time
        }
        
        self.test_results.append(result)
        
        status = f"{Colors.GREEN}✅ PASS{Colors.RESET}" if success else f"{Colors.RED}❌ FAIL{Colors.RESET}"
        print(f"\n{status}")
        
        return result
    
    def run_tests(self, audio_files: List[Path], manifest: Dict):
        """Run tests for all audio files."""
        print(f"{Colors.BOLD}{'='*60}{Colors.RESET}")
        print(f"{Colors.BOLD}Gemini Pipeline Test Suite{Colors.RESET}")
        print(f"{Colors.BOLD}{'='*60}{Colors.RESET}")
        print(f"Audio directory: {self.audio_dir}")
        print(f"Test files: {len(audio_files)}")
        print()
        
        if not self.connect_serial():
            return
        
        # Wait for device to be ready
        print(f"{Colors.BLUE}⏳ Waiting for device to initialize...{Colors.RESET}")
        time.sleep(3)
        self.read_logs(5.0)
        
        # Run tests
        for i, audio_file in enumerate(audio_files, 1):
            # Find expected text from manifest
            expected_text = None
            for file_info in manifest.get('files', []):
                if file_info.get('file') == audio_file.name:
                    expected_text = file_info.get('text')
                    break
            
            if not expected_text:
                expected_text = audio_file.stem.replace('test_', '').replace('_', ' ')
            
            print(f"\n{Colors.BOLD}[{i}/{len(audio_files)}]{Colors.RESET}")
            self.test_command(audio_file, expected_text)
            
            # Brief pause between tests
            if i < len(audio_files):
                time.sleep(2)
        
        # Print summary
        self.print_summary()
    
    def print_summary(self):
        """Print test summary."""
        print(f"\n{Colors.BOLD}{'='*60}{Colors.RESET}")
        print(f"{Colors.BOLD}Test Summary{Colors.RESET}")
        print(f"{Colors.BOLD}{'='*60}{Colors.RESET}")
        
        total = len(self.test_results)
        passed = sum(1 for r in self.test_results if r['success'])
        failed = total - passed
        
        print(f"Total tests: {total}")
        print(f"{Colors.GREEN}Passed: {passed}{Colors.RESET}")
        print(f"{Colors.RED}Failed: {failed}{Colors.RESET}")
        print(f"Success rate: {passed/total*100:.1f}%")
        
        if failed > 0:
            print(f"\n{Colors.RED}Failed tests:{Colors.RESET}")
            for result in self.test_results:
                if not result['success']:
                    print(f"  - {result['expected_text']}")
                    if not result['events']['stt_detected']:
                        print(f"    ❌ STT not detected")
                    if not result['events']['llm_detected']:
                        print(f"    ❌ LLM not detected")
                    if not result['events']['tts_detected']:
                        print(f"    ❌ TTS not detected")
    
    def close(self):
        """Close serial connection."""
        if self.ser:
            self.ser.close()

def main():
    parser = argparse.ArgumentParser(description="Test Gemini STT-LLM-TTS pipeline")
    parser.add_argument("--port", default="/dev/cu.usbserial-1110", help="Serial port")
    parser.add_argument("--audio-dir", default="test_audio", help="Directory with test audio files")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--file", help="Test single audio file")
    
    args = parser.parse_args()
    
    audio_dir = Path(args.audio_dir)
    if not audio_dir.exists():
        print(f"❌ Error: Audio directory not found: {audio_dir}")
        sys.exit(1)
    
    # Load manifest
    manifest_file = audio_dir / "manifest.json"
    manifest = {}
    if manifest_file.exists():
        with open(manifest_file) as f:
            manifest = json.load(f)
    
    # Find audio files
    if args.file:
        audio_files = [Path(args.file)]
    else:
        audio_files = sorted(audio_dir.glob("test_*.wav"))
    
    if not audio_files:
        print(f"❌ Error: No test audio files found in {audio_dir}")
        sys.exit(1)
    
    # Run tests
    tester = PipelineTester(args.port, args.audio_dir, args.baud)
    try:
        tester.run_tests(audio_files, manifest)
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}⚠️  Test interrupted by user{Colors.RESET}")
    finally:
        tester.close()

if __name__ == "__main__":
    main()
