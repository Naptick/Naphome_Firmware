#!/usr/bin/env python3
"""
Generate training data using gTTS + ffmpeg for "Hey, Naptick"
Uses ffmpeg directly instead of pydub (works with Python 3.13)
"""

import os
import sys
import subprocess
from pathlib import Path
import time

# Fix Python path for gTTS
import site
user_site = site.getusersitepackages()
site_packages = site.getsitepackages()
for path in site_packages + [user_site]:
    if path and path not in sys.path:
        sys.path.insert(0, path)

SCRIPT_DIR = Path(__file__).parent
DATA_DIR = SCRIPT_DIR / "data"
POSITIVE_DIR = DATA_DIR / "positive"
NEGATIVE_DIR = DATA_DIR / "negative"

POSITIVE_DIR.mkdir(parents=True, exist_ok=True)
NEGATIVE_DIR.mkdir(parents=True, exist_ok=True)

print("=" * 60)
print("Generate 'Hey, Naptick' Training Data")
print("=" * 60)
print("")

# Check for ffmpeg
ffmpeg_available = subprocess.run(["which", "ffmpeg"], capture_output=True).returncode == 0
if not ffmpeg_available:
    print("⚠ ffmpeg not found")
    print("Install with: brew install ffmpeg")
    print("Or use: python3 generate_data_simple.py (creates placeholders)")
    sys.exit(1)

print("✓ ffmpeg available")

# Try to import gTTS
try:
    from gtts import gTTS
    print("✓ gTTS available")
except ImportError:
    print("Installing gTTS...")
    subprocess.run([sys.executable, "-m", "pip", "install", "gtts", "--break-system-packages"])
    from gtts import gTTS
    print("✓ gTTS installed")

print("")

# Configuration
WAKE_WORD = "Hey, Naptick"
NUM_POSITIVE = 200
NUM_NEGATIVE = 300
SAMPLE_RATE = 16000

print(f"Generating {NUM_POSITIVE} positive samples for '{WAKE_WORD}'")
print(f"Generating {NUM_NEGATIVE} negative samples")
print("")
print("This will take 30-60 minutes...")
print("Press Ctrl+C to cancel")
print("")

# Generate positive samples
positive_variations = [
    "Hey, Naptick",
    "Hey Naptick", 
    "Hey, Naptick!",
    "Hey Naptick!",
]

positive_count = 0
print("Generating positive samples...")
for i in range(NUM_POSITIVE):
    output_path = POSITIVE_DIR / f"hey_naptick_{i:03d}.wav"
    
    if output_path.exists():
        # Check if it's a real file (not placeholder)
        if output_path.stat().st_size > 100000:  # Real TTS files are larger
            positive_count += 1
            continue
    
    text = positive_variations[i % len(positive_variations)]
    
    try:
        # Generate with gTTS
        tts = gTTS(text=text, lang='en', slow=False)
        temp_mp3 = output_path.with_suffix('.mp3')
        tts.save(str(temp_mp3))
        
        # Convert MP3 to WAV using ffmpeg
        result = subprocess.run([
            "ffmpeg", "-i", str(temp_mp3),
            "-ar", str(SAMPLE_RATE),
            "-ac", "1",
            "-sample_fmt", "s16",
            "-y",  # Overwrite
            str(output_path)
        ], capture_output=True)
        
        # Clean up temp file
        if temp_mp3.exists():
            temp_mp3.unlink()
        
        if result.returncode == 0:
            positive_count += 1
            if positive_count % 20 == 0:
                print(f"  Generated {positive_count}/{NUM_POSITIVE} positive samples...")
        else:
            print(f"  Error converting {output_path.name}")
        
        time.sleep(0.2)  # Rate limiting
    except Exception as e:
        print(f"  Error generating {output_path.name}: {e}")
        if temp_mp3.exists():
            temp_mp3.unlink()
        continue

print(f"✓ Generated {positive_count} positive samples")
print("")

# Generate negative samples
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
print("Generating negative samples...")
for i in range(NUM_NEGATIVE):
    output_path = NEGATIVE_DIR / f"negative_{i:03d}.wav"
    
    if output_path.exists():
        if output_path.stat().st_size > 100000:
            negative_count += 1
            continue
    
    text = negative_texts[i % len(negative_texts)]
    
    try:
        tts = gTTS(text=text, lang='en', slow=False)
        temp_mp3 = output_path.with_suffix('.mp3')
        tts.save(str(temp_mp3))
        
        result = subprocess.run([
            "ffmpeg", "-i", str(temp_mp3),
            "-ar", str(SAMPLE_RATE),
            "-ac", "1",
            "-sample_fmt", "s16",
            "-y",
            str(output_path)
        ], capture_output=True)
        
        if temp_mp3.exists():
            temp_mp3.unlink()
        
        if result.returncode == 0:
            negative_count += 1
            if negative_count % 30 == 0:
                print(f"  Generated {negative_count}/{NUM_NEGATIVE} negative samples...")
        
        time.sleep(0.2)
    except Exception as e:
        print(f"  Error generating {output_path.name}: {e}")
        if temp_mp3.exists():
            temp_mp3.unlink()
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
print("")