#!/usr/bin/env python3
"""
Generate C header file with LED positions from JSON mapping.
"""

import json
import sys

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 generate_led_positions_header.py <mapping.json> [output.h]")
        sys.exit(1)
    
    json_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else None
    
    with open(json_path, 'r') as f:
        data = json.load(f)
    
    leds = data['leds']
    
    header = """// Auto-generated LED positions for face LED simulator
#pragma once

#include <stdint.h>

#define FACE_LED_COUNT 50
#define FACE_LED_INNER_COUNT 16
#define FACE_LED_OUTER_COUNT 24

typedef struct {
    int16_t x;
    int16_t y;
    float angle;
} face_led_position_t;

static const face_led_position_t face_led_positions[FACE_LED_COUNT] = {
"""
    
    for led in leds:
        header += f"    {{ {led['x']}, {led['y']}, {led['angle']:.6f}f }},  // LED {led['index']} ({led['ring']})\n"
    
    header += "};\n"
    
    if output_path:
        with open(output_path, 'w') as f:
            f.write(header)
        print(f"Generated {output_path}")
    else:
        print(header)

if __name__ == '__main__':
    main()
