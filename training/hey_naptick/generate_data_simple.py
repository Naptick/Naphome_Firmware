#!/usr/bin/env python3
"""
Simplified training data generator for "Hey, Naptick"
Creates placeholder WAV files and provides instructions for real data collection
"""

import os
import sys
from pathlib import Path
import numpy as np
import wave
import struct

SCRIPT_DIR = Path(__file__).parent
DATA_DIR = SCRIPT_DIR / "data"
POSITIVE_DIR = DATA_DIR / "positive"
NEGATIVE_DIR = DATA_DIR / "negative"

# Create directories
POSITIVE_DIR.mkdir(parents=True, exist_ok=True)
NEGATIVE_DIR.mkdir(parents=True, exist_ok=True)

print("=" * 60)
print("Training Data Generator for 'Hey, Naptick'")
print("=" * 60)
print("")

# Configuration
WAKE_WORD = "Hey, Naptick"
NUM_POSITIVE = 200
NUM_NEGATIVE = 300
SAMPLE_RATE = 16000
DURATION_SEC = 2.0  # 2 seconds per sample

def create_silent_wav(output_path, duration_sec=2.0, sample_rate=16000):
    """Create a silent WAV file as placeholder"""
    num_samples = int(sample_rate * duration_sec)
    
    with wave.open(str(output_path), 'w') as wav_file:
        wav_file.setnchannels(1)  # Mono
        wav_file.setsampwidth(2)  # 16-bit
        wav_file.setframerate(sample_rate)
        
        # Write silent audio
        silence = b'\x00\x00' * num_samples
        wav_file.writeframes(silence)
    
    return True

def create_tone_wav(output_path, frequency=440, duration_sec=2.0, sample_rate=16000):
    """Create a simple tone WAV file (for testing)"""
    num_samples = int(sample_rate * duration_sec)
    
    samples = []
    for i in range(num_samples):
        t = float(i) / sample_rate
        # Simple sine wave
        value = int(32767 * 0.3 * np.sin(2 * np.pi * frequency * t))
        samples.append(struct.pack('<h', value))
    
    with wave.open(str(output_path), 'w') as wav_file:
        wav_file.setnchannels(1)  # Mono
        wav_file.setsampwidth(2)  # 16-bit
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(b''.join(samples))
    
    return True

print("This script creates placeholder WAV files for training structure.")
print("For actual training, you need real 'Hey, Naptick' audio samples.")
print("")
print("Options:")
print("  1. Create placeholder files (for structure)")
print("  2. Instructions for collecting real data")
print("  3. Try TTS generation (requires gTTS)")
print("")

choice = input("Select option (1-3): ").strip()

if choice == "1":
    print("")
    print("Creating placeholder WAV files...")
    print("(These are silent files - replace with real recordings)")
    print("")
    
    # Create positive samples
    for i in range(NUM_POSITIVE):
        output_path = POSITIVE_DIR / f"hey_naptick_{i:03d}.wav"
        if not output_path.exists():
            create_silent_wav(output_path, DURATION_SEC, SAMPLE_RATE)
        if (i + 1) % 50 == 0:
            print(f"  Created {i + 1}/{NUM_POSITIVE} positive placeholders...")
    
    print(f"✓ Created {NUM_POSITIVE} positive placeholder files")
    
    # Create negative samples
    for i in range(NUM_NEGATIVE):
        output_path = NEGATIVE_DIR / f"negative_{i:03d}.wav"
        if not output_path.exists():
            create_silent_wav(output_path, DURATION_SEC, SAMPLE_RATE)
        if (i + 1) % 50 == 0:
            print(f"  Created {i + 1}/{NUM_NEGATIVE} negative placeholders...")
    
    print(f"✓ Created {NUM_NEGATIVE} negative placeholder files")
    print("")
    print("⚠ IMPORTANT: These are placeholder files!")
    print("Replace them with real audio recordings for training.")
    print("")
    print("Next steps:")
    print("  1. Record 'Hey, Naptick' samples")
    print("  2. Replace files in data/positive/ with real recordings")
    print("  3. Record negative samples")
    print("  4. Replace files in data/negative/ with real recordings")
    print("  5. Run training: python3 train_hey_naptick.py")
    
elif choice == "2":
    print("")
    print("=" * 60)
    print("Instructions for Collecting Real Training Data")
    print("=" * 60)
    print("")
    print("Positive Samples ('Hey, Naptick'):")
    print("  - Record 100-200 samples")
    print("  - Format: WAV, 16kHz, mono, 16-bit")
    print("  - Length: 1-3 seconds")
    print("  - Variations:")
    print("    * Different distances (close, medium, far)")
    print("    * Different environments (quiet, noisy)")
    print("    * Different speakers (if possible)")
    print("    * Different tones (normal, excited, whispered)")
    print("  - Save to: data/positive/hey_naptick_XXX.wav")
    print("")
    print("Negative Samples:")
    print("  - Record 200-500 samples")
    print("  - Other wake words ('Hey Google', 'Alexa')")
    print("  - Background noise")
    print("  - Other speech")
    print("  - Save to: data/negative/negative_XXX.wav")
    print("")
    print("Recording Tools:")
    print("  - arecord (Linux): arecord -r 16000 -f S16_LE -c 1 file.wav")
    print("  - Audacity: Export as WAV, 16kHz, mono, 16-bit")
    print("  - Online tools: Export as WAV with correct format")
    print("")
    print("See COLLECT_DATA.md for detailed instructions")
    
elif choice == "3":
    print("")
    print("Attempting TTS generation...")
    print("")
    
    # Try to import and use gTTS
    try:
        import site
        import sys
        # Add all possible paths
        for path in site.getsitepackages() + [site.getusersitepackages()]:
            if path not in sys.path:
                sys.path.insert(0, path)
        
        from gtts import gTTS
        from pydub import AudioSegment
        
        print("✓ gTTS found! Generating samples...")
        print("This will take 30-60 minutes...")
        print("")
        
        # Generate positive samples
        positive_count = 0
        variations = ["Hey, Naptick", "Hey Naptick", "Hey, Naptick!", "Hey Naptick!"]
        
        for i in range(NUM_POSITIVE):
            output_path = POSITIVE_DIR / f"hey_naptick_{i:03d}.wav"
            if output_path.exists():
                continue
            
            text = variations[i % len(variations)]
            
            try:
                # Generate with gTTS
                tts = gTTS(text=text, lang='en', slow=False)
                temp_mp3 = output_path.with_suffix('.mp3')
                tts.save(str(temp_mp3))
                
                # Convert to WAV
                audio = AudioSegment.from_mp3(str(temp_mp3))
                audio = audio.set_frame_rate(SAMPLE_RATE)
                audio = audio.set_channels(1)  # Mono
                audio = audio.set_sample_width(2)  # 16-bit
                audio.export(str(output_path), format="wav")
                temp_mp3.unlink()
                
                positive_count += 1
                if positive_count % 20 == 0:
                    print(f"  Generated {positive_count}/{NUM_POSITIVE} positive samples...")
                
                import time
                time.sleep(0.2)  # Rate limiting
            except Exception as e:
                print(f"  Error generating {output_path}: {e}")
                continue
        
        print(f"✓ Generated {positive_count} positive samples")
        print("")
        
        # Generate negative samples
        negative_texts = ["Hey Google", "Alexa", "Hey Siri", "Computer", "Hello", 
                         "Hi there", "Good morning", "Wake up", "Hello world", "Testing"]
        negative_count = 0
        
        for i in range(NUM_NEGATIVE):
            output_path = NEGATIVE_DIR / f"negative_{i:03d}.wav"
            if output_path.exists():
                continue
            
            text = negative_texts[i % len(negative_texts)]
            
            try:
                tts = gTTS(text=text, lang='en', slow=False)
                temp_mp3 = output_path.with_suffix('.mp3')
                tts.save(str(temp_mp3))
                
                audio = AudioSegment.from_mp3(str(temp_mp3))
                audio = audio.set_frame_rate(SAMPLE_RATE)
                audio = audio.set_channels(1)
                audio = audio.set_sample_width(2)
                audio.export(str(output_path), format="wav")
                temp_mp3.unlink()
                
                negative_count += 1
                if negative_count % 30 == 0:
                    print(f"  Generated {negative_count}/{NUM_NEGATIVE} negative samples...")
                
                import time
                time.sleep(0.2)
            except Exception as e:
                print(f"  Error generating {output_path}: {e}")
                continue
        
        print(f"✓ Generated {negative_count} negative samples")
        print("")
        print("=" * 60)
        print("Synthetic Data Generation Complete!")
        print("=" * 60)
        print("")
        print(f"Positive samples: {positive_count} in {POSITIVE_DIR}")
        print(f"Negative samples: {negative_count} in {NEGATIVE_DIR}")
        print("")
        print("Next: Train the model")
        print("  python3 train_hey_naptick.py")
        
    except ImportError as e:
        print(f"Error: Could not import gTTS: {e}")
        print("")
        print("Install with:")
        print("  python3 -m pip install gtts pydub --break-system-packages")
        print("")
        print("Or use option 1 to create placeholder files")
        print("Or use option 2 for manual recording instructions")

else:
    print("Invalid option")
    sys.exit(1)