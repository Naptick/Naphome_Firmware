#!/usr/bin/env python3
"""
Generate training data using gTTS for "Hey, Naptick"
Simplified version that handles path issues
"""

import os
import sys
from pathlib import Path

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
print("Generate 'Hey, Naptick' Training Data with gTTS")
print("=" * 60)
print("")

# Try to import gTTS
try:
    from gtts import gTTS
    print("✓ gTTS available")
except ImportError:
    print("Error: gTTS not found")
    print("Installing...")
    import subprocess
    result = subprocess.run([sys.executable, "-m", "pip", "install", "gtts", "pydub", "--break-system-packages"],
                           capture_output=True, text=True)
    if result.returncode == 0:
        from gtts import gTTS
        print("✓ gTTS installed")
    else:
        print("Failed to install gTTS")
        print("Try: python3 -m pip install gtts pydub --break-system-packages")
        sys.exit(1)

try:
    from pydub import AudioSegment
    print("✓ pydub available")
except ImportError:
    print("Installing pydub...")
    import subprocess
    subprocess.run([sys.executable, "-m", "pip", "install", "pydub", "--break-system-packages"])
    from pydub import AudioSegment
    print("✓ pydub installed")

print("")

# Configuration
WAKE_WORD = "Hey, Naptick"
NUM_POSITIVE = 200
NUM_NEGATIVE = 300
SAMPLE_RATE = 16000

print(f"Generating {NUM_POSITIVE} positive samples for '{WAKE_WORD}'")
print(f"Generating {NUM_NEGATIVE} negative samples")
print("")
print("This will take 30-60 minutes depending on internet speed...")
print("Press Ctrl+C to cancel")
print("")

import time

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
        continue
    
    text = positive_variations[i % len(positive_variations)]
    
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
        temp_mp3.unlink()  # Delete temp file
        
        positive_count += 1
        if positive_count % 20 == 0:
            print(f"  Generated {positive_count}/{NUM_POSITIVE} positive samples...")
        
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