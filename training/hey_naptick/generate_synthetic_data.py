#!/usr/bin/env python3
"""
Generate synthetic training data for "Hey, Naptick"
Uses TTS to generate training samples quickly
"""

import os
import sys
from pathlib import Path
import numpy as np

# Add user site-packages to path for gTTS
import site
user_site = site.getusersitepackages()
site_packages = site.getsitepackages()
# Add all possible package locations
for path in site_packages + [user_site]:
    if path and path not in sys.path:
        sys.path.insert(0, path)

SCRIPT_DIR = Path(__file__).parent
DATA_DIR = SCRIPT_DIR / "data"
POSITIVE_DIR = DATA_DIR / "positive"
NEGATIVE_DIR = DATA_DIR / "negative"

# Create directories
POSITIVE_DIR.mkdir(parents=True, exist_ok=True)
NEGATIVE_DIR.mkdir(parents=True, exist_ok=True)

print("=" * 60)
print("Generate Synthetic Training Data for 'Hey, Naptick'")
print("=" * 60)
print("")

# Check for TTS options
tts_available = False
tts_method = None

# Try gTTS (Google TTS)
try:
    from gtts import gTTS
    tts_available = True
    tts_method = "gtts"
    print("✓ gTTS available (Google Text-to-Speech)")
except ImportError:
    try:
        # Try with user site-packages
        import sys
        import site
        user_site = site.getusersitepackages()
        if user_site not in sys.path:
            sys.path.insert(0, user_site)
        from gtts import gTTS
        tts_available = True
        tts_method = "gtts"
        print("✓ gTTS available (Google Text-to-Speech)")
    except ImportError:
        pass

# Try pyttsx3 (offline)
if not tts_available:
    try:
        import pyttsx3
        tts_available = True
        tts_method = "pyttsx3"
        print("✓ pyttsx3 available (offline TTS)")
    except ImportError:
        pass

if not tts_available:
    print("⚠ No TTS library found")
    print("")
    print("Installing gTTS...")
    import subprocess
    result = subprocess.run([sys.executable, "-m", "pip", "install", "--user", "gtts", "pydub"], 
                           capture_output=True, text=True)
    if result.returncode == 0:
        # Try importing again
        try:
            from gtts import gTTS
            tts_available = True
            tts_method = "gtts"
            print("✓ gTTS installed and available")
        except ImportError:
            print("⚠ Installation succeeded but import failed")
            print("Try: pip3 install gtts pydub")
            sys.exit(1)
    else:
        print("Failed to install gTTS")
        print("Try manually: pip3 install gtts pydub")
        sys.exit(1)

print("")

# Configuration
WAKE_WORD = "Hey, Naptick"
NUM_POSITIVE = 200
NUM_NEGATIVE = 300
SAMPLE_RATE = 16000

print(f"Generating {NUM_POSITIVE} positive samples for '{WAKE_WORD}'")
print(f"Generating {NUM_NEGATIVE} negative samples")
print("")

def generate_with_gtts(text, output_path, lang='en', slow=False):
    """Generate audio using gTTS"""
    try:
        tts = gTTS(text=text, lang=lang, slow=slow)
        # Save to temp file then convert
        temp_path = output_path.with_suffix('.mp3')
        tts.save(str(temp_path))
        
        # Convert MP3 to WAV using ffmpeg or pydub
        try:
            from pydub import AudioSegment
            audio = AudioSegment.from_mp3(str(temp_path))
            audio = audio.set_frame_rate(SAMPLE_RATE)
            audio = audio.set_channels(1)  # Mono
            audio = audio.set_sample_width(2)  # 16-bit
            audio.export(str(output_path), format="wav")
            temp_path.unlink()  # Delete temp file
            return True
        except ImportError:
            print(f"  ⚠ pydub not installed - install with: pip3 install pydub")
            print(f"  Or use ffmpeg to convert {temp_path} to WAV")
            return False
    except Exception as e:
        print(f"  Error generating {output_path}: {e}")
        return False

def generate_with_pyttsx3(text, output_path):
    """Generate audio using pyttsx3"""
    try:
        import pyttsx3
        import wave
        import struct
        
        engine = pyttsx3.init()
        
        # Set properties
        engine.setProperty('rate', 150)  # Speed
        engine.setProperty('volume', 0.9)
        
        # Get available voices
        voices = engine.getProperty('voices')
        if len(voices) > 0:
            engine.setProperty('voice', voices[0].id)
        
        # Save to WAV
        engine.save_to_file(text, str(output_path))
        engine.runAndWait()
        
        return True
    except Exception as e:
        print(f"  Error generating {output_path}: {e}")
        return False

# Generate positive samples
print("Generating positive samples...")
positive_count = 0
for i in range(NUM_POSITIVE):
    output_path = POSITIVE_DIR / f"hey_naptick_{i:03d}.wav"
    
    if output_path.exists():
        continue
    
    # Vary the text slightly
    variations = [
        "Hey, Naptick",
        "Hey Naptick",
        "Hey, Naptick!",
        "Hey Naptick!",
    ]
    text = variations[i % len(variations)]
    
    if tts_method == "gtts":
        success = generate_with_gtts(text, output_path)
    elif tts_method == "pyttsx3":
        success = generate_with_pyttsx3(text, output_path)
    else:
        success = False
    
    if success:
        positive_count += 1
        if (positive_count % 20) == 0:
            print(f"  Generated {positive_count}/{NUM_POSITIVE}...")
    
    # Small delay to avoid rate limiting
    import time
    time.sleep(0.1)

print(f"✓ Generated {positive_count} positive samples")
print("")

# Generate negative samples
print("Generating negative samples...")
negative_texts = [
    "Hey Google",
    "Alexa",
    "Hey Siri",
    "Computer",
    "Hello",
    "Hi there",
    "Good morning",
    "Wake up",
    "Hello world",
    "Testing",
]

negative_count = 0
for i in range(NUM_NEGATIVE):
    output_path = NEGATIVE_DIR / f"negative_{i:03d}.wav"
    
    if output_path.exists():
        continue
    
    text = negative_texts[i % len(negative_texts)]
    
    if tts_method == "gtts":
        success = generate_with_gtts(text, output_path)
    elif tts_method == "pyttsx3":
        success = generate_with_pyttsx3(text, output_path)
    else:
        success = False
    
    if success:
        negative_count += 1
        if (negative_count % 30) == 0:
            print(f"  Generated {negative_count}/{NUM_NEGATIVE}...")
    
    import time
    time.sleep(0.1)

print(f"✓ Generated {negative_count} negative samples")
print("")

print("=" * 60)
print("Synthetic Data Generation Complete")
print("=" * 60)
print("")
print(f"Positive samples: {positive_count} in {POSITIVE_DIR}")
print(f"Negative samples: {negative_count} in {NEGATIVE_DIR}")
print("")
print("Next steps:")
print("  1. Review generated samples")
print("  2. Add more real samples if possible (better accuracy)")
print("  3. Run training: python3 train_hey_naptick.py")
print("")