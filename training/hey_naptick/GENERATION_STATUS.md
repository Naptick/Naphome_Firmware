# Training Data Generation Status

## Current Status

Training data generation has been started for "Hey, Naptick".

## What's Happening

The script `generate_with_gtts.py` is generating:
- **200 positive samples** - "Hey, Naptick" variations using Google TTS
- **300 negative samples** - Other phrases for negative training

## Check Progress

```bash
cd training/hey_naptick

# Count generated files
find data/positive -name "*.wav" | wc -l
find data/negative -name "*.wav" | wc -l

# List recent files
ls -lt data/positive/ | head -10
ls -lt data/negative/ | head -10
```

## Expected Timeline

- **Total time**: 30-60 minutes
- **Positive samples**: ~20-30 minutes (200 samples)
- **Negative samples**: ~20-30 minutes (300 samples)

## If Generation Fails

If you see errors or generation stops:

1. **Check internet connection** (gTTS requires internet)
2. **Check for rate limiting** (Google may throttle requests)
3. **Resume generation** - Script skips existing files
4. **Use alternative method** - See options below

## Alternative Methods

### Option 1: Use Placeholder Files
```bash
cd training/hey_naptick
echo "1" | python3 generate_data_simple.py
```
Creates silent placeholder files (replace with real recordings later)

### Option 2: Record Real Samples
- Record "Hey, Naptick" 100-200 times
- Save as WAV (16kHz, mono, 16-bit)
- Place in `data/positive/`
- See `COLLECT_DATA.md` for details

### Option 3: Use Different TTS
- Try offline TTS: `pip3 install pyttsx3`
- Use cloud TTS services (AWS Polly, Azure TTS)
- Generate samples manually

## After Generation Completes

Once you have training data:

```bash
cd training/hey_naptick
python3 train_hey_naptick.py
```

This will train your "Hey, Naptick" model!

## Files Location

- Positive samples: `training/hey_naptick/data/positive/hey_naptick_XXX.wav`
- Negative samples: `training/hey_naptick/data/negative/negative_XXX.wav`
- Model output: `hey_naptick.tflite` (in project root after training)