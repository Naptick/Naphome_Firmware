#!/usr/bin/env python3
"""
Train "Hey, Naptick" wake word model using TensorFlow
Creates a simple CNN-based wake word detector
"""

import os
import sys
from pathlib import Path
import numpy as np

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
print("Train 'Hey, Naptick' Wake Word Model (TensorFlow)")
print("=" * 60)
print("")

# Check dependencies
try:
    import tensorflow as tf
    print(f"✓ TensorFlow {tf.__version__}")
except ImportError:
    print("Error: TensorFlow not installed")
    print("Install: python3 -m pip install tensorflow --break-system-packages")
    sys.exit(1)

try:
    import librosa
    print("✓ librosa available")
except ImportError:
    print("Error: librosa not installed")
    print("Install: python3 -m pip install librosa --break-system-packages")
    sys.exit(1)

try:
    from sklearn.model_selection import train_test_split
    print("✓ scikit-learn available")
except ImportError:
    print("Installing scikit-learn...")
    import subprocess
    subprocess.run([sys.executable, "-m", "pip", "install", "scikit-learn", "--break-system-packages"])
    from sklearn.model_selection import train_test_split

print("")

# Configuration
DATA_DIR = SCRIPT_DIR / "data"
POSITIVE_DIR = DATA_DIR / "positive"
NEGATIVE_DIR = DATA_DIR / "negative"
OUTPUT_DIR = SCRIPT_DIR / "output"
MODEL_OUTPUT = PROJECT_ROOT / "hey_naptick.tflite"

OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# Load training data
print("Loading training data...")
positive_files = sorted([f for f in POSITIVE_DIR.glob("*.wav") if f.stat().st_size > 40000])
negative_files = sorted([f for f in NEGATIVE_DIR.glob("*.wav") if f.stat().st_size > 40000])

print(f"  Positive samples: {len(positive_files)}")
print(f"  Negative samples: {len(negative_files)}")
print("")

if len(positive_files) < 10:
    print("Error: Need at least 10 positive samples")
    sys.exit(1)

# Extract features (melspectrogram)
def extract_features(file_path, sr=16000, n_mels=40, hop_length=160):
    """Extract melspectrogram features"""
    try:
        y, _ = librosa.load(file_path, sr=sr, mono=True, duration=2.0)
        
        # Pad or truncate to fixed length
        target_length = sr * 2  # 2 seconds
        if len(y) < target_length:
            y = np.pad(y, (0, target_length - len(y)))
        else:
            y = y[:target_length]
        
        # Extract melspectrogram
        mel = librosa.feature.melspectrogram(
            y=y, sr=sr, n_mels=n_mels, hop_length=hop_length, n_fft=512
        )
        
        # Convert to log scale
        mel_db = librosa.power_to_db(mel, ref=np.max)
        
        # Normalize
        mel_db = (mel_db - mel_db.min()) / (mel_db.max() - mel_db.min() + 1e-8)
        
        return mel_db.T  # Transpose: (time, freq)
    except Exception as e:
        print(f"  Error loading {file_path.name}: {e}")
        return None

print("Extracting features...")
X = []
y = []

for file_path in positive_files:
    features = extract_features(file_path)
    if features is not None:
        X.append(features)
        y.append(1)  # Positive class
    if len(X) % 20 == 0:
        print(f"  Processed {len(X)} samples...")

for file_path in negative_files:
    features = extract_features(file_path)
    if features is not None:
        X.append(features)
        y.append(0)  # Negative class
    if len(X) % 20 == 0:
        print(f"  Processed {len(X)} samples...")

print(f"✓ Extracted features from {len(X)} samples")
print("")

if len(X) < 20:
    print("Error: Not enough valid samples")
    sys.exit(1)

# Convert to numpy arrays
X = np.array(X)
y = np.array(y)

# Add channel dimension for CNN
X = np.expand_dims(X, axis=-1)  # (samples, time, freq, 1)

print(f"Feature shape: {X.shape}")
print(f"Labels: {np.sum(y==1)} positive, {np.sum(y==0)} negative")
print("")

# Split data
X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.2, random_state=42, stratify=y
)

print(f"Training set: {len(X_train)} samples")
print(f"Test set: {len(X_test)} samples")
print("")

# Build model
print("Building model...")
model = tf.keras.Sequential([
    tf.keras.layers.Conv2D(32, (3, 3), activation='relu', input_shape=X_train.shape[1:]),
    tf.keras.layers.MaxPooling2D((2, 2)),
    tf.keras.layers.Conv2D(64, (3, 3), activation='relu'),
    tf.keras.layers.MaxPooling2D((2, 2)),
    tf.keras.layers.Conv2D(64, (3, 3), activation='relu'),
    tf.keras.layers.GlobalAveragePooling2D(),
    tf.keras.layers.Dense(64, activation='relu'),
    tf.keras.layers.Dropout(0.5),
    tf.keras.layers.Dense(1, activation='sigmoid')
])

model.compile(
    optimizer='adam',
    loss='binary_crossentropy',
    metrics=['accuracy']
)

print("Model architecture:")
model.summary()
print("")

# Train model
print("=" * 60)
print("Training Model")
print("=" * 60)
print("")

epochs = 50
batch_size = 16

history = model.fit(
    X_train, y_train,
    batch_size=batch_size,
    epochs=epochs,
    validation_data=(X_test, y_test),
    verbose=1
)

# Evaluate
print("")
print("=" * 60)
print("Evaluation")
print("=" * 60)
print("")

test_loss, test_acc = model.evaluate(X_test, y_test, verbose=0)
print(f"Test accuracy: {test_acc:.4f}")
print(f"Test loss: {test_loss:.4f}")
print("")

# Convert to TFLite
print("Converting to TFLite...")
converter = tf.lite.TFLiteConverter.from_keras_model(model)

# Optimize for size
converter.optimizations = [tf.lite.Optimize.DEFAULT]

# Convert
tflite_model = converter.convert()

# Save
with open(MODEL_OUTPUT, 'wb') as f:
    f.write(tflite_model)

print(f"✓ Model saved: {MODEL_OUTPUT}")
print(f"  Model size: {len(tflite_model) / 1024:.2f} KB")
print("")

# Save training history
import json
history_dict = {k: [float(v) for v in vals] for k, vals in history.history.items()}
with open(OUTPUT_DIR / "training_history.json", "w") as f:
    json.dump(history_dict, f, indent=2)

print("=" * 60)
print("Training Complete!")
print("=" * 60)
print("")
print(f"Model: {MODEL_OUTPUT}")
print(f"Training history: {OUTPUT_DIR / 'training_history.json'}")
print("")
print("Next: Flash model to ESP32")
print("  See: docs/OPENWAKEWORD_GETTING_STARTED.md")
print("")