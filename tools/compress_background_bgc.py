from pathlib import Path
import argparse
import heapq
import struct
from collections import Counter

parser = argparse.ArgumentParser(description='Compress a 24/32-bit BMP background to RE:CONFIG BGC format.')
parser.add_argument('input_bmp')
parser.add_argument('-o', '--output', default='assets/ui/main_menu_background.bgc')
parser.add_argument('--scale-down', type=int, default=1, help='Average NxN pixels before compression.')
parser.add_argument('--target-width', type=int, default=0, help='Resize to this width before compression.')
parser.add_argument('--target-height', type=int, default=0, help='Resize to this height before compression.')
parser.add_argument('--rgb-bits', default='8,8,8', help='Channel bits as R,G,B after scaling, e.g. 6,5,4.')
parser.add_argument('--denoise-strength', type=float, default=0.0, help='Blend each source pixel toward its 3x3 average before resizing, 0..1.')
args = parser.parse_args()
if args.scale_down < 1:
    raise SystemExit('--scale-down must be >= 1')
if (args.target_width == 0) != (args.target_height == 0):
    raise SystemExit('--target-width and --target-height must be specified together')
r_bits, g_bits, b_bits = [int(part) for part in args.rgb_bits.split(',')]
if min(r_bits, g_bits, b_bits) < 1 or max(r_bits, g_bits, b_bits) > 8:
    raise SystemExit('--rgb-bits values must be 1..8')
if args.denoise_strength < 0.0 or args.denoise_strength > 1.0:
    raise SystemExit('--denoise-strength must be 0..1')

bmp = Path(args.input_bmp).read_bytes()
if bmp[:2] != b'BM':
    raise SystemExit('not a BMP')
pixel_offset = struct.unpack_from('<I', bmp, 10)[0]
source_w = struct.unpack_from('<i', bmp, 18)[0]
h_signed = struct.unpack_from('<i', bmp, 22)[0]
bpp = struct.unpack_from('<H', bmp, 28)[0]
compression = struct.unpack_from('<I', bmp, 30)[0]
if bpp not in (24, 32) or compression != 0:
    raise SystemExit(f'unsupported BMP bpp={bpp} compression={compression}')
source_h = abs(h_signed)
top_down = h_signed < 0
bytes_per_pixel = bpp // 8
bmp_row_bytes = ((source_w * bytes_per_pixel + 3) // 4) * 4
source_rows = []
for y in range(source_h):
    file_y = y if top_down else source_h - 1 - y
    src = pixel_offset + file_y * bmp_row_bytes
    row = bytearray(source_w * 3)
    for x in range(source_w):
        p = src + x * bytes_per_pixel
        d = x * 3
        row[d + 0] = bmp[p + 2]
        row[d + 1] = bmp[p + 1]
        row[d + 2] = bmp[p + 0]
    source_rows.append(bytes(row))

if args.denoise_strength > 0.0:
    strength = args.denoise_strength
    denoised = []
    for y in range(source_h):
        row = bytearray(source_w * 3)
        for x in range(source_w):
            sr = sg = sb = count = 0
            for yy in range(max(0, y - 1), min(source_h, y + 2)):
                src_row = source_rows[yy]
                for xx in range(max(0, x - 1), min(source_w, x + 2)):
                    p = xx * 3
                    sr += src_row[p + 0]
                    sg += src_row[p + 1]
                    sb += src_row[p + 2]
                    count += 1
            p = x * 3
            row[p + 0] = int(source_rows[y][p + 0] * (1.0 - strength) + (sr / count) * strength + 0.5)
            row[p + 1] = int(source_rows[y][p + 1] * (1.0 - strength) + (sg / count) * strength + 0.5)
            row[p + 2] = int(source_rows[y][p + 2] * (1.0 - strength) + (sb / count) * strength + 0.5)
        denoised.append(bytes(row))
    source_rows = denoised

def quantize(value, bits):
    if bits >= 8:
        return value
    levels = (1 << bits) - 1
    q = (value * levels + 127) // 255
    return (q * 255 + levels // 2) // levels

def sample_bilinear(rows, sx, sy):
    if sx < 0.0:
        sx = 0.0
    if sy < 0.0:
        sy = 0.0
    if sx > source_w - 1:
        sx = source_w - 1.0
    if sy > source_h - 1:
        sy = source_h - 1.0
    x0 = int(sx)
    y0 = int(sy)
    x1 = x0 + 1 if x0 + 1 < source_w else x0
    y1 = y0 + 1 if y0 + 1 < source_h else y0
    fx = sx - x0
    fy = sy - y0
    p00 = y0, x0 * 3
    p10 = y0, x1 * 3
    p01 = y1, x0 * 3
    p11 = y1, x1 * 3
    out = []
    for c in range(3):
        c00 = rows[p00[0]][p00[1] + c]
        c10 = rows[p10[0]][p10[1] + c]
        c01 = rows[p01[0]][p01[1] + c]
        c11 = rows[p11[0]][p11[1] + c]
        c0 = c00 * (1.0 - fx) + c10 * fx
        c1 = c01 * (1.0 - fx) + c11 * fx
        out.append(int(c0 * (1.0 - fy) + c1 * fy + 0.5))
    return out

if args.target_width and args.target_height:
    w = args.target_width
    h = args.target_height
else:
    w = source_w // args.scale_down
    h = source_h // args.scale_down

rows = []
for y in range(h):
    row = bytearray(w * 3)
    for x in range(w):
        if w == source_w and h == source_h:
            p = x * 3
            rgb = source_rows[y][p:p + 3]
        elif source_w % w == 0 and source_h % h == 0:
            scale_x = source_w // w
            scale_y = source_h // h
            sr = sg = sb = 0
            for yy in range(scale_y):
                src_row = source_rows[y * scale_y + yy]
                for xx in range(scale_x):
                    p = (x * scale_x + xx) * 3
                    sr += src_row[p + 0]
                    sg += src_row[p + 1]
                    sb += src_row[p + 2]
            count = scale_x * scale_y
            rgb = (sr // count, sg // count, sb // count)
        else:
            sx = (x + 0.5) * source_w / w - 0.5
            sy = (y + 0.5) * source_h / h - 0.5
            rgb = sample_bilinear(source_rows, sx, sy)
        d = x * 3
        row[d + 0] = quantize(rgb[0], r_bits)
        row[d + 1] = quantize(rgb[1], g_bits)
        row[d + 2] = quantize(rgb[2], b_bits)
    rows.append(bytes(row))

filtered = bytearray(w * h * 3)
out_pos = 0
prev = bytes(w * 3)
for row in rows:
    for i, value in enumerate(row):
        left = row[i - 3] if i >= 3 else 0
        up = prev[i]
        filtered[out_pos] = (value - ((left + up) // 2)) & 255
        out_pos += 1
    prev = row

counts = Counter(filtered)
heap = []
seq = 0
for sym, freq in counts.items():
    heap.append((freq, seq, sym))
    seq += 1
heapq.heapify(heap)
nodes = {}
if len(heap) == 1:
    lengths = {heap[0][2]: 1}
else:
    while len(heap) > 1:
        f1, _, n1 = heapq.heappop(heap)
        f2, _, n2 = heapq.heappop(heap)
        parent = ('n', seq)
        seq += 1
        nodes[parent] = (n1, n2)
        heapq.heappush(heap, (f1 + f2, seq, parent))
        seq += 1
    lengths = {}
    stack = [(heap[0][2], 0)]
    while stack:
        node, depth = stack.pop()
        if isinstance(node, int):
            lengths[node] = max(depth, 1)
        else:
            a, b = nodes[node]
            stack.append((a, depth + 1))
            stack.append((b, depth + 1))

max_len = max(lengths.values())
if max_len > 24:
    raise SystemExit(f'huffman code too long: {max_len}')
code_lengths = bytearray(256)
for sym, length in lengths.items():
    code_lengths[sym] = length

bl_count = [0] * (max_len + 1)
for length in code_lengths:
    if length:
        bl_count[length] += 1
next_code = [0] * (max_len + 1)
code = 0
for bits in range(1, max_len + 1):
    code = (code + bl_count[bits - 1]) << 1
    next_code[bits] = code
codes = {}
for sym in range(256):
    length = code_lengths[sym]
    if length:
        codes[sym] = (next_code[length], length)
        next_code[length] += 1

bitstream = bytearray()
bit_buf = 0
bit_count = 0
for sym in filtered:
    code, length = codes[sym]
    for shift in range(length - 1, -1, -1):
        bit_buf = (bit_buf << 1) | ((code >> shift) & 1)
        bit_count += 1
        if bit_count == 8:
            bitstream.append(bit_buf)
            bit_buf = 0
            bit_count = 0
if bit_count:
    bitstream.append(bit_buf << (8 - bit_count))

header = bytearray()
header.extend(b'RCBG')
header.extend(struct.pack('<IIII', w, h, len(filtered), len(bitstream)))
header.extend(bytes((3, 0, 0, 0)))
header.extend(code_lengths)
Path(args.output).parent.mkdir(parents=True, exist_ok=True)
Path(args.output).write_bytes(header + bitstream)
print(f'wrote {args.output} {len(header) + len(bitstream)} bytes, {w}x{h}, max_code_len={max_len}')
