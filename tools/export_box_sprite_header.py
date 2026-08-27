import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASEPRITE_PATH = ROOT / 'assets' / 'sprites' / 'Box.aseprite'
OUT_PATH = ROOT / 'src' / 'render' / 'box_sprite_data.h'


def read_u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]


def read_u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def extract_first_rgba_cel(path):
    data = path.read_bytes()
    if len(data) < 144:
        raise ValueError('Aseprite file is too small')
    if read_u16(data, 4) != 0xA5E0:
        raise ValueError('Not an Aseprite file')
    width = read_u16(data, 8)
    height = read_u16(data, 10)
    depth = read_u16(data, 12)
    frames = read_u16(data, 6)
    if depth != 32:
        raise ValueError(f'Only 32-bit RGBA Aseprite files are supported, got {depth}')
    if frames < 1:
        raise ValueError('Aseprite file has no frames')

    frame_offset = 128
    if frame_offset + 16 > len(data):
        raise ValueError('Aseprite file has no complete frame header')
    if read_u16(data, frame_offset + 4) != 0xF1FA:
        raise ValueError('Invalid Aseprite frame header')
    old_chunk_count = read_u16(data, frame_offset + 6)
    new_chunk_count = read_u32(data, frame_offset + 12)
    chunk_count = new_chunk_count if new_chunk_count != 0 else old_chunk_count
    chunk_offset = frame_offset + 16
    pixels = [[0 for _ in range(width)] for _ in range(height)]

    for _ in range(chunk_count):
        if chunk_offset + 6 > len(data):
            break
        chunk_size = read_u32(data, chunk_offset)
        chunk_type = read_u16(data, chunk_offset + 4)
        body = data[chunk_offset + 6:chunk_offset + chunk_size]
        if chunk_type == 0x2005:
            cel_type = read_u16(body, 7)
            x = struct.unpack_from('<h', body, 2)[0]
            y = struct.unpack_from('<h', body, 4)[0]
            opacity = body[6]
            if cel_type == 2:
                cel_w = read_u16(body, 16)
                cel_h = read_u16(body, 18)
                raw = zlib.decompress(body[20:])
                if len(raw) != cel_w * cel_h * 4:
                    raise ValueError('Unexpected compressed cel size')
            elif cel_type == 0:
                cel_w = width
                cel_h = height
                raw = body[16:]
                if len(raw) < cel_w * cel_h * 4:
                    raise ValueError('Unexpected raw cel size')
            else:
                chunk_offset += chunk_size
                continue
            for py in range(cel_h):
                dy = y + py
                if dy < 0 or dy >= height:
                    continue
                for px in range(cel_w):
                    dx = x + px
                    if dx < 0 or dx >= width:
                        continue
                    src = (py * cel_w + px) * 4
                    r, g, b, a = raw[src], raw[src + 1], raw[src + 2], raw[src + 3]
                    if opacity != 255:
                        a = (a * opacity + 127) // 255
                    pixels[dy][dx] = (r << 24) | (g << 16) | (b << 8) | a
            return width, height, pixels
        chunk_offset += chunk_size
    raise ValueError('No supported RGBA cel found in first frame')


def horizontal_runs(row):
    runs = []
    start = 0
    color = row[0]
    for x in range(1, len(row) + 1):
        if x == len(row) or row[x] != color:
            if color & 0xFF:
                runs.append((start, x - start, color))
            if x < len(row):
                start = x
                color = row[x]
    return runs


def encode_rects(pixels):
    active = {}
    rects = []
    for y, row in enumerate(pixels):
        row_keys = set()
        for x, w, color in horizontal_runs(row):
            key = (x, w, color)
            row_keys.add(key)
            if key in active:
                active[key][3] += 1
            else:
                active[key] = [x, y, w, 1, color]
        for key in list(active.keys()):
            if key not in row_keys:
                rects.append(tuple(active.pop(key)))
    rects.extend(tuple(rect) for rect in active.values())
    rects.sort(key=lambda rect: (rect[1], rect[0], rect[4]))
    return rects


def write_header(path, width, height, rects):
    lines = [
        '#pragma once',
        '',
        '#include <stdint.h>',
        '',
        'struct BoxSpriteRect {',
        '    uint8_t x;',
        '    uint8_t y;',
        '    uint8_t w;',
        '    uint8_t h;',
        '    uint32_t rgba;',
        '};',
        '',
        f'static constexpr int BOX_SPRITE_WIDTH = {width};',
        f'static constexpr int BOX_SPRITE_HEIGHT = {height};',
        f'static constexpr int BOX_SPRITE_RECT_COUNT = {len(rects)};',
        'static const BoxSpriteRect BOX_SPRITE_RECTS[BOX_SPRITE_RECT_COUNT] = {',
    ]
    for x, y, w, h, rgba in rects:
        lines.append(f'    {{ {x}u, {y}u, {w}u, {h}u, 0x{rgba:08X}u }},')
    lines.append('};')
    lines.append('')
    path.write_text('\n'.join(lines), encoding='ascii')


def main():
    width, height, pixels = extract_first_rgba_cel(ASEPRITE_PATH)
    if width != 40 or height != 40:
        raise ValueError(f'Box.aseprite must be 40x40, got {width}x{height}')
    rects = encode_rects(pixels)
    write_header(OUT_PATH, width, height, rects)
    raw_bytes = width * height * 4
    encoded_bytes = len(rects) * 8
    print(f'{OUT_PATH} ({len(rects)} rect runs, {encoded_bytes}/{raw_bytes} bytes before C syntax)')


if __name__ == '__main__':
    main()