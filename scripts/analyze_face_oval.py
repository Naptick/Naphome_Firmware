#!/usr/bin/env python3
"""
Analyze NaphomeFace.png to find the oval region and generate LED position mapping.

The oval region represents where LED patterns should be displayed.
LEDs are arranged in two rings:
- Inner ring: 16 LEDs
- Outer ring: 24 LEDs
Total: 50 LEDs
"""

import sys
import os
import math
from PIL import Image
import json

def find_oval_region(image_path):
    """Find the oval region (non-transparent area) in the face image."""
    img = Image.open(image_path)
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    # Get alpha channel
    alpha = img.split()[3]
    pixels = alpha.load()
    width, height = img.size
    
    # Find non-transparent pixels
    x_coords = []
    y_coords = []
    for y in range(height):
        for x in range(width):
            if pixels[x, y] > 0:
                x_coords.append(x)
                y_coords.append(y)
    
    if len(x_coords) == 0:
        return None
    
    # Calculate bounding box and center
    min_x, max_x = min(x_coords), max(x_coords)
    min_y, max_y = min(y_coords), max(y_coords)
    center_x = int(sum(x_coords) / len(x_coords))
    center_y = int(sum(y_coords) / len(y_coords))
    oval_width = max_x - min_x + 1
    oval_height = max_y - min_y + 1
    
    return {
        'center': (center_x, center_y),
        'bbox': (min_x, min_y, max_x, max_y),
        'size': (oval_width, oval_height),
        'image_size': img.size
    }

def generate_led_positions(oval_info, inner_count=16, outer_count=24):
    """
    Generate LED positions mapped to the oval region.
    
    LEDs are arranged in concentric circles:
    - Inner ring: smaller radius, inner_count LEDs
    - Outer ring: larger radius, outer_count LEDs
    """
    center_x, center_y = oval_info['center']
    width, height = oval_info['size']
    
    # Calculate radii (use smaller dimension to ensure fit)
    max_radius = min(width, height) / 2.0
    inner_radius = max_radius * 0.4  # Inner ring at 40% of max radius
    outer_radius = max_radius * 0.85  # Outer ring at 85% of max radius
    
    led_positions = []
    
    # Generate inner ring positions
    for i in range(inner_count):
        angle = 2.0 * math.pi * i / inner_count
        x = int(center_x + inner_radius * math.cos(angle))
        y = int(center_y + inner_radius * math.sin(angle))
        led_positions.append({
            'index': i,
            'ring': 'inner',
            'x': x,
            'y': y,
            'angle': angle
        })
    
    # Generate outer ring positions
    for i in range(outer_count):
        angle = 2.0 * math.pi * i / outer_count
        x = int(center_x + outer_radius * math.cos(angle))
        y = int(center_y + outer_radius * math.sin(angle))
        led_positions.append({
            'index': inner_count + i,
            'ring': 'outer',
            'x': x,
            'y': y,
            'angle': angle
        })
    
    return led_positions

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_face_oval.py <face_image.png> [output.json]")
        sys.exit(1)
    
    image_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None
    
    print(f"Analyzing {image_path}...")
    oval_info = find_oval_region(image_path)
    
    if not oval_info:
        print("Error: No non-transparent region found")
        sys.exit(1)
    
    print(f"Oval center: {oval_info['center']}")
    print(f"Oval bbox: {oval_info['bbox']}")
    print(f"Oval size: {oval_info['size']}")
    
    # Generate LED positions
    led_positions = generate_led_positions(oval_info)
    print(f"\nGenerated {len(led_positions)} LED positions")
    print(f"  Inner ring: 16 LEDs")
    print(f"  Outer ring: 24 LEDs")
    
    # Create output structure
    output = {
        'oval': {
            'center': oval_info['center'],
            'bbox': oval_info['bbox'],
            'size': oval_info['size']
        },
        'leds': led_positions,
        'inner_radius': 0.4,
        'outer_radius': 0.85
    }
    
    # Save to JSON if output path provided
    if output_path:
        with open(output_path, 'w') as f:
            json.dump(output, f, indent=2)
        print(f"\nSaved LED mapping to {output_path}")
    else:
        print("\nLED positions (first 5):")
        for led in led_positions[:5]:
            print(f"  LED {led['index']} ({led['ring']}): ({led['x']}, {led['y']})")
    
    return output

if __name__ == '__main__':
    main()
