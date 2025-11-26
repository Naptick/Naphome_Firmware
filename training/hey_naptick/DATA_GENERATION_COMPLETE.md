# Training Data Generation - Status

## Current Status

✅ **500 WAV files created** (200 positive + 300 negative)

## File Locations

- **Positive samples**: `training/hey_naptick/data/positive/hey_naptick_XXX.wav`
- **Negative samples**: `training/hey_naptick/data/negative/negative_XXX.wav`

## Important Note

The current files may be **placeholder files** (silent WAV files created for structure).

For **real TTS-generated data**, the generation script is running in the background.

## Check Generation Status

```bash
cd training/hey_naptick
./check_generation_progress.sh
```

## Verify File Quality

Real TTS files will be:
- **Larger**: >100KB (vs ~63KB for placeholders)
- **Have audio content**: Can play them to verify
- **Different sizes**: TTS files vary slightly

Check file sizes:
```bash
ls -lh data/positive/*.wav | head -5
ls -lh data/negative/*.wav | head -5
```

## Next Steps

Once you have real training data (or if using placeholders):

1. **Verify data quality**
   ```bash
   # Check file sizes (real TTS files are larger)
   ls -lh data/positive/*.wav | head -10
   ```

2. **Train the model**
   ```bash
   python3 train_hey_naptick.py
   ```

3. **Or replace placeholders with real recordings**
   - Record "Hey, Naptick" samples
   - Replace files in `data/positive/`
   - See `COLLECT_DATA.md` for instructions

## Options

### Option 1: Use Current Files (If TTS Generation Completes)
Wait for TTS generation to finish, then train.

### Option 2: Replace with Real Recordings (Best Quality)
Record actual "Hey, Naptick" samples and replace placeholder files.

### Option 3: Use Placeholders for Testing
Train with placeholders to test the pipeline (won't work well, but tests the process).

## Training Data Requirements

- **Format**: WAV, 16kHz, mono, 16-bit
- **Positive**: 100-200 samples of "Hey, Naptick"
- **Negative**: 200-500 samples of other phrases/noise
- **Current**: 200 positive + 300 negative files created

## Ready to Train?

Once you have real training data:

```bash
cd training/hey_naptick
python3 train_hey_naptick.py
```

The model will be saved as `hey_naptick.tflite` in the project root.