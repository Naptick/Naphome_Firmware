#!/usr/bin/env python3
"""
Simple Gemini Pipeline Test - Monitors device logs to validate STT-LLM-TTS pathway.
Tests the pipeline by monitoring for Gemini activity in logs.
"""

import serial
import time
import re
import argparse
import sys
from datetime import datetime
from collections import deque

class Colors:
    RESET = '\033[0m'
    BOLD = '\033[1m'
    GREEN = '\033[32m'
    YELLOW = '\033[33m'
    RED = '\033[31m'
    BLUE = '\033[34m'
    CYAN = '\033[36m'

class PipelineMonitor:
    def __init__(self, port: str, baud: int = 115200):
        self.port = port
        self.baud = baud
        self.ser = None
        self.events = []
        
        # Patterns to track (Hybrid: OpenAI Realtime STT → Gemini LLM → Gemini TTS)
        self.patterns = {
            # OpenAI Realtime STT (primary STT path)
            'openai_realtime_sending': re.compile(r'\[OpenAI Realtime\].*Sending audio', re.IGNORECASE),
            'openai_realtime_transcript': re.compile(r'\[OpenAI Realtime\].*transcript.*: "([^"]+)"', re.IGNORECASE),
            'openai_realtime_final': re.compile(r'\[Realtime\].*Routing transcription.*: "([^"]+)"', re.IGNORECASE),
            # Gemini Batch STT (fallback path)
            'gemini_stt_sending': re.compile(r'\[Gemini STT\].*Sending audio', re.IGNORECASE),
            'gemini_stt_transcript': re.compile(r'\[Gemini STT\].*transcript.*: "([^"]+)"', re.IGNORECASE),
            # Gemini LLM
            'gemini_llm_request': re.compile(r'\[Gemini LLM\].*Sending HTTP POST', re.IGNORECASE),
            'gemini_llm_response': re.compile(r'\[Gemini LLM\].*Response: "([^"]+)"', re.IGNORECASE),
            'gemini_llm_function': re.compile(r'\[Gemini LLM\].*Function call.*: (\w+)', re.IGNORECASE),
            'gemini_llm_function_detected': re.compile(r'Function.*detected.*: (\w+)', re.IGNORECASE),
            # Gemini TTS
            'gemini_tts_generating': re.compile(r'\[Gemini TTS\].*Generating speech', re.IGNORECASE),
            'gemini_tts_success': re.compile(r'\[Gemini TTS\].*Success.*(\d+) bytes', re.IGNORECASE),
            'gemini_tts_playing': re.compile(r'Playing TTS audio|Playing.*greeting', re.IGNORECASE),
            # General pipeline indicators
            'vad_speech': re.compile(r'Speech (started|ended|detected)', re.IGNORECASE),
            'audio_accumulating': re.compile(r'Accumulating audio', re.IGNORECASE),
        }
    
    def connect(self):
        """Connect to device."""
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=2)
            print(f"{Colors.GREEN}✅ Connected to {self.port}{Colors.RESET}")
            return True
        except Exception as e:
            print(f"{Colors.RED}❌ Failed to connect: {e}{Colors.RESET}")
            return False
    
    def monitor(self, duration: float = 60.0):
        """Monitor logs for pipeline activity."""
        if not self.ser:
            return
        
        print(f"\n{Colors.BOLD}{'='*60}{Colors.RESET}")
        print(f"{Colors.BOLD}Monitoring Gemini Pipeline (STT-LLM-TTS){Colors.RESET}")
        print(f"{Colors.BOLD}{'='*60}{Colors.RESET}")
        print(f"{Colors.BLUE}⏳ Monitoring for {duration} seconds...{Colors.RESET}")
        print(f"{Colors.YELLOW}💡 Speak to the device or wait for voice interactions{Colors.RESET}\n")
        
        start_time = time.time()
        pipeline_events = {
            'stt': [],
            'llm': [],
            'tts': [],
            'function_calls': []
        }
        
        while time.time() - start_time < duration:
            if self.ser.in_waiting:
                try:
                    line = self.ser.readline()
                    text = line.decode('utf-8', errors='ignore').strip()
                    if text and len(text) < 1000:
                        # Check for pipeline events
                        for event_type, pattern in self.patterns.items():
                            match = pattern.search(text)
                            if match:
                                timestamp = datetime.now().strftime("%H:%M:%S")
                                event = {
                                    'time': timestamp,
                                    'type': event_type,
                                    'text': text,
                                    'match': match.group(1) if match.groups() else None
                                }
                                self.events.append(event)
                                
                                # Categorize
                                if 'stt' in event_type or 'realtime' in event_type or 'transcript' in event_type:
                                    pipeline_events['stt'].append(event)
                                    transcript_text = match.group(1) if match.groups() else None
                                    if transcript_text:
                                        print(f"{Colors.CYAN}[{timestamp}] STT: \"{transcript_text}\"{Colors.RESET}")
                                    else:
                                        print(f"{Colors.CYAN}[{timestamp}] STT: {event_type.replace('_', ' ').title()}{Colors.RESET}")
                                elif 'llm' in event_type:
                                    pipeline_events['llm'].append(event)
                                    if 'function' in event_type:
                                        pipeline_events['function_calls'].append(event)
                                        func_name = match.group(1) if match.groups() else 'unknown'
                                        print(f"{Colors.MAGENTA}[{timestamp}] LLM Function Call: {func_name}{Colors.RESET}")
                                    else:
                                        response_text = match.group(1) if match.groups() else None
                                        if response_text:
                                            print(f"{Colors.MAGENTA}[{timestamp}] LLM: \"{response_text[:60]}...\"{Colors.RESET}")
                                        else:
                                            print(f"{Colors.MAGENTA}[{timestamp}] LLM: Request sent{Colors.RESET}")
                                elif 'tts' in event_type:
                                    pipeline_events['tts'].append(event)
                                    if 'success' in event_type:
                                        bytes_val = match.group(1) if match.groups() else '?'
                                        print(f"{Colors.GREEN}[{timestamp}] TTS: Generated {bytes_val} bytes{Colors.RESET}")
                                    elif 'playing' in event_type:
                                        print(f"{Colors.GREEN}[{timestamp}] TTS: Playing audio{Colors.RESET}")
                                    else:
                                        print(f"{Colors.GREEN}[{timestamp}] TTS: Generating{Colors.RESET}")
                                elif 'vad' in event_type or 'audio_accumulating' in event_type:
                                    print(f"{Colors.YELLOW}[{timestamp}] VAD: {event_type.replace('_', ' ').title()}{Colors.RESET}")
                except:
                    pass
            time.sleep(0.05)
        
        # Print summary
        self.print_summary(pipeline_events)
    
    def print_summary(self, pipeline_events):
        """Print test summary."""
        print(f"\n{Colors.BOLD}{'='*60}{Colors.RESET}")
        print(f"{Colors.BOLD}Test Summary{Colors.RESET}")
        print(f"{Colors.BOLD}{'='*60}{Colors.RESET}")
        
        stt_count = len(pipeline_events['stt'])
        llm_count = len(pipeline_events['llm'])
        tts_count = len(pipeline_events['tts'])
        func_count = len(pipeline_events['function_calls'])
        
        print(f"\n{Colors.BOLD}Pipeline Events:{Colors.RESET}")
        print(f"  STT (Speech-to-Text): {Colors.GREEN if stt_count > 0 else Colors.RED}{stt_count}{Colors.RESET}")
        print(f"  LLM (Language Model): {Colors.GREEN if llm_count > 0 else Colors.RED}{llm_count}{Colors.RESET}")
        print(f"  TTS (Text-to-Speech): {Colors.GREEN if tts_count > 0 else Colors.RED}{tts_count}{Colors.RESET}")
        print(f"  Function Calls: {Colors.GREEN if func_count > 0 else Colors.YELLOW}{func_count}{Colors.RESET}")
        
        # Check for complete pipeline
        has_stt = stt_count > 0
        has_llm = llm_count > 0
        has_tts = tts_count > 0
        
        complete_pipeline = has_stt and has_llm and has_tts
        
        print(f"\n{Colors.BOLD}Pipeline Status:{Colors.RESET}")
        if complete_pipeline:
            print(f"  {Colors.GREEN}✅ Complete pipeline detected (STT → LLM → TTS){Colors.RESET}")
        else:
            print(f"  {Colors.YELLOW}⚠️  Partial pipeline activity{Colors.RESET}")
            if not has_stt:
                print(f"    ❌ No STT activity detected")
            if not has_llm:
                print(f"    ❌ No LLM activity detected")
            if not has_tts:
                print(f"    ❌ No TTS activity detected")
        
        # Show recent events
        if self.events:
            print(f"\n{Colors.BOLD}Recent Events (last 10):{Colors.RESET}")
            for event in self.events[-10:]:
                print(f"  [{event['time']}] {event['type']}: {event['text'][:80]}")
    
    def close(self):
        """Close connection."""
        if self.ser:
            self.ser.close()

def main():
    parser = argparse.ArgumentParser(description="Test Gemini STT-LLM-TTS pipeline by monitoring logs")
    parser.add_argument("--port", default="/dev/cu.usbserial-1110", help="Serial port")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--duration", type=float, default=60.0, help="Monitoring duration in seconds")
    
    args = parser.parse_args()
    
    monitor = PipelineMonitor(args.port, args.baud)
    
    try:
        if monitor.connect():
            monitor.monitor(args.duration)
    except KeyboardInterrupt:
        print(f"\n{Colors.YELLOW}⚠️  Test interrupted by user{Colors.RESET}")
    finally:
        monitor.close()

if __name__ == "__main__":
    main()
