# OpenWakeWord Quick Start Guide

## TL;DR - Get "Hey, Naptick" Working in 3 Steps

### Step 1: Add TensorFlow Lite Micro (5 minutes)
```bash
cd components
git submodule add https://github.com/espressif/esp-tflite-micro.git
cd esp-tflite-micro && git submodule update --init --recursive
```

### Step 2: Build Demo (10 minutes)
```bash
cd samples/korvo_openwakeword_demo
idf.py set-target esp32s3
idf.py menuconfig  # Enable OpenWakeWord
idf.py build flash monitor
```

### Step 3: Train & Flash Model (1-2 days)
```bash
# Train model
pip install openwakeword
./scripts/train_openwakeword.sh

# Flash model
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
    --chip esp32s3 --port /dev/ttyUSB0 \
    write_flash 0x140000 hey_naptick.tflite
```

## What's Already Done ✅

- ✅ Complete melspectrogram extraction
- ✅ Model loading from SPIFFS/partitions
- ✅ TFLite integration framework
- ✅ Working demo application
- ✅ Integration examples

## What You Need to Do 🚧

1. Add esp-tflite-micro (git submodule)
2. Train "Hey, Naptick" model
3. Flash and test

**That's it!** The hard work is done.

## Alternative: Request Espressif Training

While completing this, request Espressif to train "Hey, Naptick":
```bash
./scripts/request_wake_word.sh
```

Gets you a professionally-trained model in 2-4 weeks with 95-98% accuracy.