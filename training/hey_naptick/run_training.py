#!/usr/bin/env python3
"""
Run training for "Hey, Naptick" wake word
Uses OpenWakeWord's training capabilities
"""

import os
import sys
from pathlib import Path

# Fix Python path
import site
site_packages = site.getsitepackages()
user_site = site.getusersitepackages()
for path in site_packages + [user_site]:
    if path and path not in sys.path:
        sys.path.insert(0, path)

SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent

print("=" * 60)
print("Training 'Hey, Naptick' Wake Word Model")
print("=" * 60)
print("")

# Import OpenWakeWord
try:
    from openwakeword import train_custom_verifier, Model
    print("✓ OpenWakeWord available")
except ImportError as e:
    print(f"Error: {e}")
    print("Install: python3 -m pip install openwakeword --break-system-packages")
    sys.exit(1)

# Configuration
DATA_DIR = SCRIPT_DIR / "data"
POSITIVE_DIR = DATA_DIR / "positive"
NEGATIVE_DIR = DATA_DIR / "negative"
OUTPUT_DIR = SCRIPT_DIR / "output"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# Get real training files (filter placeholders)
positive_files = [f for f in POSITIVE_DIR.glob("*.wav") if f.stat().st_size > 40000]
negative_files = [f for f in NEGATIVE_DIR.glob("*.wav") if f.stat().st_size > 40000]

print(f"Training data:")
print(f"  Positive: {len(positive_files)} samples")
print(f"  Negative: {len(negative_files)} samples")
print("")

if len(positive_files) < 10:
    print("⚠ Warning: Need at least 10 positive samples")
    print("   Current: {len(positive_files)}")
    print("")
    print("   Options:")
    print("   1. Wait for TTS generation to complete")
    print("   2. Record more samples")
    sys.exit(1)

# Note about training
print("=" * 60)
print("Important: Training Approach")
print("=" * 60)
print("")
print("OpenWakeWord's `train_custom_verifier` trains a VERIFIER model,")
print("not the main wake word detection model.")
print("")
print("The verifier is used AFTER wake word detection to confirm")
print("it was spoken by a specific voice/user.")
print("")
print("For training the MAIN wake word model ('Hey, Naptick'), you need:")
print("  1. The full OpenWakeWord repository")
print("  2. Their training pipeline")
print("  3. See: https://github.com/dscripka/openWakeWord")
print("")
print("However, we can:")
print("  ✓ Train a custom verifier (voice-specific)")
print("  ✓ Validate your training data")
print("  ✓ Prepare data for main model training")
print("")

response = input("Continue with verifier training? (y/n): ").strip().lower()
if response != 'y':
    print("")
    print("To train the main wake word model:")
    print("  1. Clone: https://github.com/dscripka/openWakeWord")
    print("  2. Follow their training guide")
    print("  3. Use your data in: {DATA_DIR}")
    sys.exit(0)

print("")
print("Training custom verifier...")
print("")

# Train custom verifier
try:
    verifier_output = OUTPUT_DIR / "hey_naptick_verifier.joblib"
    
    # Use a pre-trained base model
    base_model_name = "alexa_v0.1"  # Or another pre-trained model
    
    print(f"Using base model: {base_model_name}")
    print(f"Training verifier...")
    print("")
    
    train_custom_verifier(
        positive_reference_clips=str(POSITIVE_DIR),
        negative_reference_clips=str(NEGATIVE_DIR),
        output_path=str(verifier_output),
        model_name=base_model_name
    )
    
    print("")
    print("=" * 60)
    print("Verifier Training Complete!")
    print("=" * 60)
    print("")
    print(f"✓ Verifier saved: {verifier_output}")
    print("")
    print("Note: This is a VERIFIER model, not the main wake word model.")
    print("For the main 'Hey, Naptick' detection model, you still need")
    print("to train using the full OpenWakeWord repository.")
    print("")
    
except Exception as e:
    print(f"Error during training: {e}")
    print("")
    print("This might be because:")
    print("  1. Need more training samples")
    print("  2. Base model not available")
    print("  3. Data format issues")
    print("")
    print("For main wake word training, use OpenWakeWord repository:")
    print("  https://github.com/dscripka/openWakeWord")
    sys.exit(1)

print("Next steps:")
print("  1. Train main wake word model (OpenWakeWord repo)")
print("  2. Use verifier for voice-specific confirmation")
print("  3. Export to TFLite")
print("  4. Flash to ESP32")
print("")