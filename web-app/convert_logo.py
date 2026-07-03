#!/usr/bin/env python3
"""
Convert LVGL RGB565 logo_fox.c to PNG.
The file stores uint8_t pairs (little-endian RGB565), 200x200.
"""
import re
import struct
import zlib

INPUT = "/Users/oscariglesias/Documents/Arduino/Aura Weather/Aura-main/aura/logo_fox.c"
OUTPUT = "/Users/oscariglesias/Documents/Arduino/Camara/web-app/fox_logo.png"
WIDTH = 200
HEIGHT = 200

def rgb565_to_rgb888(lo, hi):
    """Convert 2 bytes (little-endian RGB565) to (R, G, B)."""
    val = (hi << 8) | lo
    r = ((val >> 11) & 0x1F) * 255 // 31
    g = ((val >> 5) & 0x3F) * 255 // 63
    b = (val & 0x1F) * 255 // 31
    return (r, g, b)

# Read file
with open(INPUT, 'r') as f:
    content = f.read()

# Extract all 0xff style hex bytes
hex_bytes = re.findall(r'0x([0-9A-Fa-f]{2})', content)
print(f"Found {len(hex_bytes)} bytes = {len(hex_bytes)//2} pixels")

byte_data = [int(h, 16) for h in hex_bytes]

# Convert pairs to RGB
pixels = []
for i in range(0, len(byte_data) - 1, 2):
    pixels.append(rgb565_to_rgb888(byte_data[i], byte_data[i+1]))

print(f"Converted {len(pixels)} pixels (expected {WIDTH*HEIGHT})")

# Make the background black and the fox white for web
# The original has white background (0xFFFF) and dark fox
# We need to INVERT: white fox on black background
inverted_pixels = []
for r, g, b in pixels[:WIDTH*HEIGHT]:
    inverted_pixels.append((255-r, 255-g, 255-b))

# Write PNG
def write_png(filename, w, h, px):
    def chunk(ctype, data):
        c = ctype + data
        crc = struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)
        return struct.pack('>I', len(data)) + c + crc

    sig = b'\x89PNG\r\n\x1a\n'
    ihdr = chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))

    raw = b''
    for y in range(h):
        raw += b'\x00'
        for x in range(w):
            r, g, b = px[y * w + x]
            raw += struct.pack('BBB', r, g, b)

    idat = chunk(b'IDAT', zlib.compress(raw, 9))
    iend = chunk(b'IEND', b'')

    with open(filename, 'wb') as f:
        f.write(sig + ihdr + idat + iend)

# Save inverted (white fox on black)
write_png(OUTPUT, WIDTH, HEIGHT, inverted_pixels)
print(f"Inverted PNG saved: {OUTPUT}")

# Also save original (dark fox on white) for reference
write_png(OUTPUT.replace('.png', '_original.png'), WIDTH, HEIGHT, pixels[:WIDTH*HEIGHT])
print("Original also saved for comparison")

# Generate base64 for HTML embedding
import base64
with open(OUTPUT, 'rb') as f:
    b64 = base64.b64encode(f.read()).decode()

b64_file = OUTPUT.replace('.png', '_base64.txt')
with open(b64_file, 'w') as f:
    f.write(b64)
print(f"Base64: {len(b64)} chars saved to {b64_file}")
