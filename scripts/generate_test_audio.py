#!/usr/bin/env python3
"""
Generate test audio files using Gemini TTS API for testing STT-LLM-TTS pipeline.
Creates WAV files with voice commands that can be played to the device.
"""

import os
import sys
import json
import base64
import requests
import argparse
from pathlib import Path

# Test commands to generate
TEST_COMMANDS = [
    "Hey, Naptick, turn off lights",
    "Hey, Naptick, turn on lights",
    "Hey, Naptick, what's the temperature",
    "Hey, Naptick, what's the humidity",
    "Hey, Naptick, what's the air quality",
    "Hey, Naptick, play some music",
    "Hey, Naptick, stop the music",
    "Hey, Naptick, increase the volume",
    "Hey, Naptick, decrease the volume",
    "Hey, Naptick, what's your status",
    "Hey, Naptick, are you connected",
    "Hey, Naptick, what time is it",
    "Hey, Naptick, tell me a joke",
    "Hey, Naptick, what can you do",
]

def generate_tts_audio(text: str, api_key: str, voice: str = "en-US-Standard-D", output_file: str = None):
    """
    Generate TTS audio using Google Text-to-Speech API.
    
    Args:
        text: Text to synthesize
        api_key: Google Cloud API key
        voice: Voice name (default: en-US-Standard-D)
        output_file: Output WAV file path (optional, auto-generated if None)
    
    Returns:
        Path to generated WAV file or None on error
    """
    url = "https://texttospeech.googleapis.com/v1/text:synthesize"
    
    payload = {
        "input": {"text": text},
        "voice": {
            "languageCode": "en-US",
            "name": voice
        },
        "audioConfig": {
            "audioEncoding": "LINEAR16",
            "sampleRateHertz": 24000
        }
    }
    
    params = {"key": api_key}
    
    print(f"🔊 Generating TTS for: '{text}'")
    print(f"   Voice: {voice}")
    
    try:
        response = requests.post(url, json=payload, params=params, timeout=30)
        response.raise_for_status()
        
        data = response.json()
        if "audioContent" not in data:
            print(f"❌ Error: No audioContent in response")
            print(f"   Response: {json.dumps(data, indent=2)}")
            return None
        
        # Decode base64 audio
        audio_bytes = base64.b64decode(data["audioContent"])
        
        # Generate output filename if not provided
        if output_file is None:
            # Create safe filename from text
            safe_text = "".join(c if c.isalnum() or c in (' ', '-', '_') else '' for c in text)
            safe_text = safe_text.replace(' ', '_').lower()[:50]
            output_file = f"test_audio_{safe_text}.wav"
        
        # Ensure output directory exists
        output_path = Path(output_file)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        # Write WAV file (Google TTS returns raw PCM, we need to add WAV header)
        # For 24kHz, 16-bit, mono PCM
        sample_rate = 24000
        num_channels = 1
        bits_per_sample = 16
        num_samples = len(audio_bytes) // 2  # 16-bit = 2 bytes per sample
        
        # WAV header
        wav_header = bytearray(44)
        wav_header[0:4] = b'RIFF'
        wav_header[4:8] = (36 + len(audio_bytes)).to_bytes(4, 'little')  # File size - 8
        wav_header[8:12] = b'WAVE'
        wav_header[12:16] = b'fmt '
        wav_header[16:20] = (16).to_bytes(4, 'little')  # fmt chunk size
        wav_header[20:22] = (1).to_bytes(2, 'little')  # audio format (PCM)
        wav_header[22:24] = num_channels.to_bytes(2, 'little')
        wav_header[24:28] = sample_rate.to_bytes(4, 'little')
        wav_header[28:32] = (sample_rate * num_channels * bits_per_sample // 8).to_bytes(4, 'little')  # byte rate
        wav_header[32:34] = (num_channels * bits_per_sample // 8).to_bytes(2, 'little')  # block align
        wav_header[34:36] = bits_per_sample.to_bytes(2, 'little')
        wav_header[36:40] = b'data'
        wav_header[40:44] = len(audio_bytes).to_bytes(4, 'little')  # data chunk size
        
        # Write WAV file
        with open(output_path, 'wb') as f:
            f.write(wav_header)
            f.write(audio_bytes)
        
        file_size = output_path.stat().st_size
        duration = num_samples / sample_rate
        print(f"✅ Generated: {output_path} ({file_size} bytes, {duration:.2f}s)")
        return str(output_path)
        
    except requests.exceptions.RequestException as e:
        print(f"❌ HTTP error: {e}")
        if hasattr(e, 'response') and e.response is not None:
            try:
                error_data = e.response.json()
                print(f"   Error details: {json.dumps(error_data, indent=2)}")
            except:
                print(f"   Response: {e.response.text}")
        return None
    except Exception as e:
        print(f"❌ Error: {e}")
        import traceback
        traceback.print_exc()
        return None

def main():
    parser = argparse.ArgumentParser(description="Generate test audio files using Gemini TTS")
    parser.add_argument("--api-key", help="Google Cloud API key (or set GEMINI_API_KEY env var)")
    parser.add_argument("--voice", default="en-US-Standard-D", help="Voice name (default: en-US-Standard-D)")
    parser.add_argument("--output-dir", default="test_audio", help="Output directory for audio files")
    parser.add_argument("--commands", nargs="+", help="Custom commands (otherwise uses default test commands)")
    parser.add_argument("--list-voices", action="store_true", help="List available voices and exit")
    
    args = parser.parse_args()
    
    # Get API key
    api_key = args.api_key or os.getenv("GEMINI_API_KEY")
    if not api_key:
        print("❌ Error: API key required. Set GEMINI_API_KEY env var or use --api-key")
        sys.exit(1)
    
    # List voices if requested
    if args.list_voices:
        print("Available voices (sample):")
        print("  en-US-Standard-A (Female)")
        print("  en-US-Standard-B (Male)")
        print("  en-US-Standard-C (Female)")
        print("  en-US-Standard-D (Male)")
        print("  en-US-Standard-E (Female)")
        print("  en-US-Standard-F (Female)")
        print("  en-US-Standard-G (Female)")
        print("  en-US-Standard-H (Female)")
        print("  en-US-Standard-I (Male)")
        print("  en-US-Standard-J (Male)")
        print("\nFor more voices, see: https://cloud.google.com/text-to-speech/docs/voices")
        return
    
    # Get commands
    commands = args.commands if args.commands else TEST_COMMANDS
    
    # Create output directory
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"🎵 Generating {len(commands)} test audio files...")
    print(f"   Output directory: {output_dir}")
    print(f"   Voice: {args.voice}")
    print()
    
    generated_files = []
    failed = []
    
    for i, command in enumerate(commands, 1):
        print(f"[{i}/{len(commands)}] ", end="")
        output_file = output_dir / f"test_{i:02d}_{command.replace(' ', '_').replace(',', '').lower()[:40]}.wav"
        result = generate_tts_audio(command, api_key, args.voice, str(output_file))
        if result:
            generated_files.append(result)
        else:
            failed.append(command)
        print()
    
    # Summary
    print("=" * 60)
    print(f"✅ Successfully generated: {len(generated_files)}/{len(commands)} files")
    if generated_files:
        print(f"\nGenerated files:")
        for f in generated_files:
            print(f"  - {f}")
    
    if failed:
        print(f"\n❌ Failed to generate: {len(failed)} files")
        for cmd in failed:
            print(f"  - {cmd}")
    
    # Create manifest file
    manifest = {
        "voice": args.voice,
        "sample_rate": 24000,
        "format": "WAV (PCM, 16-bit, mono)",
        "files": [
            {
                "file": os.path.basename(f),
                "text": commands[generated_files.index(f)] if f in generated_files else None
            }
            for f in generated_files
        ]
    }
    
    manifest_file = output_dir / "manifest.json"
    with open(manifest_file, 'w') as f:
        json.dump(manifest, f, indent=2)
    
    print(f"\n📋 Manifest saved to: {manifest_file}")
    print(f"\n💡 To test these files, use: python3 scripts/test_gemini_pipeline.py --audio-dir {output_dir}")

if __name__ == "__main__":
    main()
