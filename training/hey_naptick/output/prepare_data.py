#!/usr/bin/env python3
"""
Prepare training data for OpenWakeWord
"""

import os
from pathlib import Path

# Data paths
positive_dir = Path("/Users/danielmcshan/GitHub/Naphome-Firmware/training/hey_naptick/data/positive")
negative_dir = Path("/Users/danielmcshan/GitHub/Naphome-Firmware/training/hey_naptick/data/negative")

# List all files
positive_files = sorted(positive_dir.glob("*.wav"))
negative_files = sorted(negative_dir.glob("*.wav"))

print(f"Positive samples: {len(positive_files)}")
print(f"Negative samples: {len(negative_files)}")

# Create file lists for training
with open("positive_files.txt", "w") as f:
    for file in positive_files:
        f.write(f"{file}\n")

with open("negative_files.txt", "w") as f:
    for file in negative_files:
        f.write(f"{file}\n")

print("Created file lists: positive_files.txt, negative_files.txt")
