#!/usr/bin/env python3
"""
Convert PNG image to RGB565 C array for ESP32 display.

Usage:
    python3 png_to_rgb565.py input.png output.h [variable_name]

The script will:
1. Resize image to fit 240x240 if needed
2. Convert to RGB565 format
3. Generate a C header file with the image data
"""

import sys
import os
from PIL import Image

def rgb888_to_rgb565(r, g, b):
    """Convert RGB888 to RGB565."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def png_to_rgb565(input_path, output_path, var_name=None, max_width=240, max_height=240):
    """Convert PNG to RGB565 C array."""
    
    if not os.path.exists(input_path):
        print(f"Error: Input file '{input_path}' not found", file=sys.stderr)
        return 1
    
    # Load and convert image
    try:
        img = Image.open(input_path)
        img = img.convert('RGB')
        
        # Resize to exact target size (maintain aspect ratio, then crop/center)
        width, height = img.size
        target_width = max_width
        target_height = max_height
        
        # Calculate scaling to fit (maintain aspect ratio)
        ratio = min(target_width / width, target_height / height)
        new_width = int(width * ratio)
        new_height = int(height * ratio)
        
        # Resize maintaining aspect ratio
        img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)
        print(f"Resized image to {new_width}x{new_height} (maintaining aspect ratio)")
        
        # Create target-sized background and center the resized image
        bg = Image.new('RGB', (target_width, target_height), (0, 0, 0))
        x_offset = (target_width - new_width) // 2
        y_offset = (target_height - new_height) // 2
        bg.paste(img, (x_offset, y_offset))
        img = bg
        width, height = target_width, target_height
        print(f"Centered image on {width}x{height} background")
        
        # Generate variable name from filename if not provided
        if var_name is None:
            var_name = os.path.splitext(os.path.basename(input_path))[0]
            var_name = var_name.replace('-', '_').replace(' ', '_')
            var_name = ''.join(c if c.isalnum() or c == '_' else '_' for c in var_name)
            if not var_name[0].isalpha():
                var_name = 'img_' + var_name
        
        # Convert to RGB565
        pixels = img.load()
        rgb565_data = []
        for y in range(height):
            for x in range(width):
                r, g, b = pixels[x, y]
                rgb565 = rgb888_to_rgb565(r, g, b)
                # Swap bytes for little-endian (many SPI displays expect this)
                rgb565_swapped = ((rgb565 & 0xFF) << 8) | ((rgb565 >> 8) & 0xFF)
                rgb565_data.append(rgb565_swapped)
        
        # Generate C header file
        with open(output_path, 'w') as f:
            f.write(f"// Auto-generated from {os.path.basename(input_path)}\n")
            f.write(f"// Image size: {width}x{height} pixels\n")
            f.write(f"// Format: RGB565 (16-bit)\n")
            f.write(f"#pragma once\n\n")
            f.write(f"#include <stdint.h>\n\n")
            f.write(f"#define {var_name.upper()}_WIDTH  {width}\n")
            f.write(f"#define {var_name.upper()}_HEIGHT {height}\n")
            f.write(f"\n")
            f.write(f"static const uint16_t {var_name}_data[{width * height}] = {{\n")
            
            # Write data in rows for readability
            for y in range(height):
                f.write("    ")
                for x in range(width):
                    idx = y * width + x
                    f.write(f"0x{rgb565_data[idx]:04X}")
                    if idx < len(rgb565_data) - 1:
                        f.write(",")
                    if x < width - 1:
                        f.write(" ")
                f.write("\n")
            
            f.write("};\n")
        
        print(f"Successfully converted {input_path} to {output_path}")
        print(f"  Variable name: {var_name}_data")
        print(f"  Size: {width}x{height} pixels")
        print(f"  Total bytes: {len(rgb565_data) * 2}")
        return 0
        
    except Exception as e:
        print(f"Error processing image: {e}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 png_to_rgb565.py input.png output.h [variable_name] [width] [height]", file=sys.stderr)
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    var_name = sys.argv[3] if len(sys.argv) > 3 else None
    max_width = int(sys.argv[4]) if len(sys.argv) > 4 else 240
    max_height = int(sys.argv[5]) if len(sys.argv) > 5 else 240
    
    sys.exit(png_to_rgb565(input_file, output_file, var_name, max_width, max_height))
