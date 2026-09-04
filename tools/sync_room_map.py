#!/usr/bin/env python3
"""Compile a ROOM_XX_MAP.md ASCII map into roomXX.cpp placement arrays.

Default mode is read-only.  Add --write after inspecting the summary.
The compiler keeps movement tuning (enemy speed/direction and piston timing)
from the existing C++ entries; it only replaces spatial placement data.
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CARDINAL = ((1, 0), (-1, 0), (0, 1), (0, -1))
DEFAULT_ENEMY_TUNING = {"M1": ("140.0f", "1"), "M2": ("100.0f", "1")}


def rows_from_map(path: Path) -> list[str]:
    match = re.search(
        r"(?P<fence>~~~|```)text\s*\r?\n(.*?)\r?\n(?P=fence)",
        path.read_text(encoding="utf-8"),
        re.S,
    )
    if not match:
        raise ValueError("text ASCII 코드 블록을 찾을 수 없습니다.")
    rows = match.group(2).splitlines()
    if not rows or any(len(row) != len(rows[0]) for row in rows):
        raise ValueError("ASCII 맵의 행 길이가 일치하지 않습니다.")
    return rows


def cells(rows: list[str], chars: str) -> set[tuple[int, int]]:
    return {(x, y) for y, row in enumerate(rows) for x, value in enumerate(row) if value in chars}


def components(source: set[tuple[int, int]]) -> list[set[tuple[int, int]]]:
    pending = set(source)
    result: list[set[tuple[int, int]]] = []
    while pending:
        component = set()
        queue = deque([pending.pop()])
        while queue:
            cell = queue.popleft()
            component.add(cell)
            for dx, dy in CARDINAL:
                nxt = cell[0] + dx, cell[1] + dy
                if nxt in pending:
                    pending.remove(nxt)
                    queue.append(nxt)
        result.append(component)
    return sorted(result, key=lambda component: min(component, key=lambda cell: (cell[1], cell[0])))


def pack_rects(source: set[tuple[int, int]]) -> list[tuple[int, int, int, int]]:
    remaining = set(source)
    result = []
    while remaining:
        x, y = min(remaining, key=lambda cell: (cell[1], cell[0]))
        width = 0
        while (x + width, y) in remaining:
            width += 1
        height = 1
        while all((tile_x, y + height) in remaining for tile_x in range(x, x + width)):
            height += 1
        for tile_y in range(y, y + height):
            for tile_x in range(x, x + width):
                remaining.remove((tile_x, tile_y))
        result.append((x, y, width, height))
    return result


def horizontal_runs(rows: list[str], marker: str) -> list[tuple[int, int, int]]:
    result = []
    for y, row in enumerate(rows):
        x = 0
        while x < len(row):
            if row[x] != marker:
                x += 1
                continue
            start = x
            while x < len(row) and row[x] == marker:
                x += 1
            result.append((start, y, x - start))
    return result


def vertical_runs(rows: list[str], marker: str) -> list[tuple[int, int, int]]:
    remaining = cells(rows, marker)
    result = []
    while remaining:
        x, y = min(remaining, key=lambda cell: (cell[1], cell[0]))
        height = 0
        while (x, y + height) in remaining:
            remaining.remove((x, y + height))
            height += 1
        result.append((x, y, height))
    return result

def one_marker(rows: list[str], char: str) -> tuple[int, int]:
    result = cells(rows, char)
    if len(result) != 1:
        raise ValueError(f"{char} 마커는 정확히 하나여야 합니다. 현재: {len(result)}")
    return next(iter(result))


def marker_rects(rows: list[str], marker: str, require_id: bool) -> list[dict]:
    if require_id:
        marker_cells = cells(rows, marker)
        if not marker_cells:
            return []
        anchors = []
        for y, row in enumerate(rows):
            for x, value in enumerate(row):
                if not value.isdigit():
                    continue
                if x > 0 and row[x - 1] == "M":
                    continue
                if (x + 1, y) in marker_cells or (x, y + 1) in marker_cells:
                    anchors.append((x, y, int(value)))
        if not anchors:
            raise ValueError(f"{marker} 장치는 좌측 또는 상단의 ID 숫자 하나가 필요합니다.")
        used = set()
        result = []
        for x, y, identifier in anchors:
            width = 1
            while (x + width, y) in marker_cells and (x + width, y) not in used:
                width += 1
            height = 1
            while all((tile_x, y + height) in marker_cells and (tile_x, y + height) not in used for tile_x in range(x, x + width)):
                height += 1
            for tile_y in range(y, y + height):
                for tile_x in range(x, x + width):
                    if (tile_x, tile_y) in marker_cells:
                        used.add((tile_x, tile_y))
            result.append({"rect": (x, y, width, height), "id": identifier})
        return sorted(result, key=lambda item: (item["rect"][1], item["rect"][0]))

    result = []
    for component in components(cells(rows, marker)):
        extended = set(component)
        ids = set()
        first_x, first_y = min(component, key=lambda cell: (cell[1], cell[0]))
        anchors = ((first_x - 1, first_y), (first_x, first_y - 1))
        for nx, ny in anchors:
            if 0 <= ny < len(rows) and 0 <= nx < len(rows[ny]) and rows[ny][nx].isdigit():
                extended.add((nx, ny))
                ids.add(int(rows[ny][nx]))
                break
        if require_id and len(ids) != 1:
            raise ValueError(f"{marker} 장치는 좌측 또는 상단의 ID 숫자 하나가 필요합니다.")
        min_x = min(x for x, _ in extended)
        max_x = max(x for x, _ in extended)
        min_y = min(y for _, y in extended)
        max_y = max(y for _, y in extended)
        result.append({"rect": (min_x, min_y, max_x - min_x + 1, max_y - min_y + 1), "id": next(iter(ids)) if ids else None})
    return sorted(result, key=lambda item: (item["rect"][1], item["rect"][0]))


def pressure_switch_mount(rows: list[str], device: dict) -> str:
    x, y, w, h = device["rect"]
    if w >= h:
        upper_contacts = sum(rows[y - 1][tile_x] == "#" for tile_x in range(x, x + w) if y > 0)
        lower_contacts = sum(rows[y + h][tile_x] == "#" for tile_x in range(x, x + w) if y + h < len(rows))
        if upper_contacts > lower_contacts:
            return "PRESSURE_SWITCH_MOUNT_UP"
        return "PRESSURE_SWITCH_MOUNT_DOWN"
    left_contacts = sum(rows[tile_y][x - 1] == "#" for tile_y in range(y, y + h) if x > 0)
    right_contacts = sum(rows[tile_y][x + w] == "#" for tile_y in range(y, y + h) if x + w < len(rows[tile_y]))
    return "PRESSURE_SWITCH_MOUNT_LEFT" if left_contacts > right_contacts else "PRESSURE_SWITCH_MOUNT_RIGHT"


def fmt_num(value: float | int) -> str:
    if float(value).is_integer():
        return f"T({int(value)})"
    return f"T({value:.2f}".rstrip("0").rstrip(".") + "f)"


def fmt_rect(rect: tuple[int, int, int, int]) -> str:
    return "{ " + ", ".join(fmt_num(value) for value in rect) + " }"


def fmt_array(lines: list[str]) -> str:
    return "\n    " + ",\n    ".join(lines) + ",\n"


def replace_array(source: str, name: str, lines: list[str]) -> str:
    pattern = rf"(static const.*?{re.escape(name)}\[\]\s*=\s*\{{).*?(\s*\}};)"
    match = re.search(pattern, source, re.S)
    if not match:
        if not lines:
            return source
        raise ValueError(f"{name} 배열을 찾을 수 없습니다.")
    return source[:match.end(1)] + fmt_array(lines) + "};" + source[match.end(2):]


def upsert_type_a_walls(source: str, room_id: str, lines: list[str]) -> str:
    name = f"g_room{room_id}_type_a_walls"
    array_pattern = rf"static const RectF {re.escape(name)}\[\]\s*=\s*\{{.*?\s*\}};\n?"
    if re.search(array_pattern, source, re.S):
        if lines:
            return replace_array(source, name, lines)
        source = re.sub(array_pattern, "", source, count=1, flags=re.S)
        fields = rf"{re.escape(name)},\s*\(int\)\(sizeof\({re.escape(name)}\) / sizeof\({re.escape(name)}\[0\]\)\),"
        return re.sub(fields, "0, 0,", source, count=1)
    if not lines:
        return source
    room_declaration = f"extern const RoomDef g_room{room_id}"
    if room_declaration not in source:
        raise ValueError("RoomDef 앞에 TYPE A 벽 배열을 추가할 위치를 찾을 수 없습니다.")
    declaration = f"static const RectF {name}[] = {{" + fmt_array(lines) + "};\n"
    source = source.replace(room_declaration, declaration + "\n" + room_declaration, 1)
    platform_fields = (
        rf"(g_room{room_id}_platforms,\s*\(int\)\(sizeof\(g_room{room_id}_platforms\) / "
        rf"sizeof\(g_room{room_id}_platforms\[0\]\)\),\s*)0\s*,\s*0\s*,"
    )
    replacement = rf"\g<1>{name},\n    (int)(sizeof({name}) / sizeof({name}[0])),"
    source, count = re.subn(platform_fields, replacement, source, count=1)
    if count != 1:
        raise ValueError("RoomDef의 TYPE A 벽 필드를 갱신할 수 없습니다.")
    return source


def upsert_room_array(source: str, ctype: str, name: str, lines: list[str]) -> str:
    if lines:
        return replace_array(source, name, lines)
    array_pattern = rf"static const {re.escape(ctype)} {re.escape(name)}\[\]\s*=\s*\{{.*?\s*\}};\n?"
    source, removed = re.subn(array_pattern, "", source, count=1, flags=re.S)
    if not removed:
        return source
    reference_pattern = rf"{re.escape(name)},\s*\(int\)\(sizeof\({re.escape(name)}\) / sizeof\({re.escape(name)}\[0\]\)\)"
    source, replaced = re.subn(reference_pattern, "0, 0", source, count=1)
    if not replaced:
        raise ValueError(f"{name} 배열의 RoomDef 참조를 찾을 수 없습니다.")
    return source


def find_open(closed: dict, targets: list[dict]) -> dict:
    x, y, w, h = closed["rect"]
    candidates = [
        target for target in targets
        if target["id"] == closed["id"] and target["rect"][2:] == (w, h)
    ]
    if not candidates:
        raise ValueError(f"ID {closed['id']}의 열린 목표를 찾을 수 없습니다.")
    return min(candidates, key=lambda item: abs(item["rect"][0] - x) + abs(item["rect"][1] - y))


def source_pistons(source: str) -> list[dict]:
    pattern = (
        r"\{\s*T\(([^)]+)\),\s*T\(([^)]+)\),\s*T\([^)]+\),\s*T\([^)]+\),\s*"
        r"(T\([^)]+\)),\s*(T\([^)]+\)),\s*T\([^)]+\),\s*([^,}]+)(?:,\s*PISTON_[A-Z]+)?\s*\}"
    )
    result = []
    for x, y, shaft_width, plate_height, start_delay_seconds in re.findall(pattern, source):
        result.append({
            "x": float(x.rstrip("f")),
            "y": float(y.rstrip("f")),
            "shaft_width": shaft_width,
            "plate_height": plate_height,
            "plate_height_value": float(re.search(r"T\(([^)]+)\)", plate_height).group(1).rstrip("f")),
            "start_delay_seconds": start_delay_seconds,
        })
    return result


def upsert_pistons(source: str, room_id: str, lines: list[str]) -> str:
    name = f"g_room{room_id}_pistons"
    array_pattern = rf"static const PistonDevice {re.escape(name)}\[\]\s*=\s*\{{.*?\s*\}};\n?"
    room_declaration = f"extern const RoomDef g_room{room_id}"
    if re.search(array_pattern, source, re.S):
        return replace_array(source, name, lines)
    if not lines:
        return source
    if room_declaration not in source:
        raise ValueError("RoomDef 앞에 피스톤 배열을 추가할 위치를 찾을 수 없습니다.")
    declaration = f"static const PistonDevice {name}[] = {{" + fmt_array(lines) + "};\n"
    source = source.replace(room_declaration, declaration + "\n" + room_declaration, 1)
    legacy_pair = r"(kDefaultDeleteState\s*,\s*)0\s*,\s*0\s*,"
    replacement = rf"\g<1>{name},\n    (int)(sizeof({name}) / sizeof({name}[0])),"
    source, count = re.subn(legacy_pair, replacement, source, count=1)
    if count != 1:
        raise ValueError("RoomDef의 피스톤 필드를 갱신할 수 없습니다.")
    return source


def source_enemies(source: str) -> list[dict]:
    pattern = (
        r"\{\s*\{\s*T\(([^)]+)\),\s*T\(([^)]+)\),\s*T\([^)]+\),\s*T\([^)]+\)\s*\},\s*"
        r"([^,]+),\s*([^,]+),\s*WALKER_ENEMY_(M[12])\s*\}"
    )
    return [
        {"x": float(x.rstrip("f")), "y": float(y.rstrip("f")), "speed": speed, "direction": direction, "kind": kind}
        for x, y, speed, direction, kind in re.findall(pattern, source)
    ]


def upsert_walker_enemies(source: str, room_id: str, lines: list[str]) -> str:
    name = f"g_room{room_id}_walker_enemies"
    array_pattern = rf"static const WalkerEnemyDef {re.escape(name)}\[\]\s*=\s*\{{.*?\s*\}};\n?"
    room_declaration = f"extern const RoomDef g_room{room_id}"
    if re.search(array_pattern, source, re.S):
        if not lines:
            source = re.sub(array_pattern, "", source, flags=re.S)
            enemy_fields = (
                rf"{re.escape(name)},\s*\(int\)\(sizeof\({re.escape(name)}\) / "
                rf"sizeof\({re.escape(name)}\[0\]\)\),\s*"
            )
            source, count = re.subn(enemy_fields, "0, ", source, count=1)
            if count != 1:
                raise ValueError("RoomDef의 워커 적 필드를 비울 수 없습니다.")
            return source
        return replace_array(source, name, lines)
    if not lines:
        return source
    if room_declaration not in source:
        raise ValueError("RoomDef 앞에 워커 적 배열을 추가할 위치를 찾을 수 없습니다.")
    declaration = f"static const WalkerEnemyDef {name}[] = {{" + fmt_array(lines) + "};\n"
    source = source.replace(room_declaration, declaration + "\n" + room_declaration, 1)
    platform_fields = (
        rf"(g_room{room_id}_pressure_platforms,\s*\(int\)\(sizeof\(g_room{room_id}_pressure_platforms\) / "
        rf"sizeof\(g_room{room_id}_pressure_platforms\[0\]\)\),\s*\d+,\s*)0\s*,\s*0\s*,"
    )
    replacement = rf"\g<1>{name},\n    (int)(sizeof({name}) / sizeof({name}[0])),"
    source, count = re.subn(platform_fields, replacement, source, count=1)
    if count != 1:
        raise ValueError("RoomDef의 워커 적 필드를 갱신할 수 없습니다.")
    return source


STATIC_SPIKE_MARKERS = {
    "M": "STATIC_SPIKE_ROTATION_0_DEGREES",
    "N": "STATIC_SPIKE_ROTATION_180_DEGREES",
    "L": "STATIC_SPIKE_ROTATION_90_DEGREES",
    "R": "STATIC_SPIKE_ROTATION_270_DEGREES",
}


def static_spike_markers(rows: list[str]) -> list[tuple[int, int, str]]:
    result = []
    for y, row in enumerate(rows):
        for x, marker in enumerate(row):
            # M1/M2 remain two-character walker-enemy markers, not static spikes.
            if marker == "M" and x + 1 < len(row) and row[x + 1] in "12":
                continue
            if marker in STATIC_SPIKE_MARKERS:
                result.append((x, y, marker))
    return result


def upsert_static_spikes(source: str, room_id: str, lines: list[str]) -> str:
    name = f"g_room{room_id}_static_spikes"
    array_pattern = rf"static const StaticSpikeDef {re.escape(name)}\[\]\s*=\s*\{{.*?\s*\}};\n?"
    tail_pattern = rf"(?P<separator>,\s*){re.escape(name)},\s*\(int\)\(sizeof\({re.escape(name)}\) / sizeof\({re.escape(name)}\[0\]\)\),?"
    if not lines:
        source = re.sub(array_pattern, "", source, flags=re.S)
        return re.sub(tail_pattern, "", source)
    if re.search(array_pattern, source, re.S):
        source = replace_array(source, name, lines)
    else:
        declaration = f"static const StaticSpikeDef {name}[] = {{" + fmt_array(lines) + "};\n"
        room_declaration = f"extern const RoomDef g_room{room_id}"
        if room_declaration not in source:
            raise ValueError("RoomDef 앞에 정적 가시 배열을 추가할 위치를 찾을 수 없습니다.")
        source = source.replace(room_declaration, declaration + "\n" + room_declaration, 1)
    room_start = source.find(f"extern const RoomDef g_room{room_id}")
    room_end = source.find("\n};", room_start)
    if room_start < 0 or room_end < 0:
        raise ValueError("RoomDef 끝을 찾아 정적 가시 배열을 연결할 수 없습니다.")
    tail = f"{name},\n    (int)(sizeof({name}) / sizeof({name}[0]))"
    piston_static_tail = (
        rf"(g_room{room_id}_pistons,\s*\(int\)\(sizeof\(g_room{room_id}_pistons\) / "
        rf"sizeof\(g_room{room_id}_pistons\[0\]\)\),)\s*{name},\s*"
        rf"\(int\)\(sizeof\({name}\) / sizeof\({name}\[0\]\)\)"
    )
    if re.search(piston_static_tail, source):
        expanded = rf"\g<1>\n    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {{}}, 0, 0, {tail}"
        return re.sub(piston_static_tail, expanded, source, count=1)
    if re.search(tail_pattern, source):
        return re.sub(tail_pattern, rf"\g<separator>{tail},", source, count=1)
    room_def = source[room_start:room_end + 3]
    piston_only_tail = (
        rf"(g_room{room_id}_pistons,\s*\(int\)\(sizeof\(g_room{room_id}_pistons\) / "
        rf"sizeof\(g_room{room_id}_pistons\[0\]\)\),)\s*\}};"
    )
    if re.search(piston_only_tail, room_def):
        expanded = rf"\g<1>\n    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {{}}, 0, 0, {tail},\n}};"
        room_def = re.sub(piston_only_tail, expanded, room_def, count=1)
        return source[:room_start] + room_def + source[room_end + 3:]
    short_tail = (
        rf"(g_room{room_id}_pressure_platforms,\s*\(int\)\(sizeof\(g_room{room_id}_pressure_platforms\) / "
        rf"sizeof\(g_room{room_id}_pressure_platforms\[0\]\)\),\s*)0,\s*\}};"
    )
    if re.search(short_tail, room_def):
        expanded = rf"\g<1>0, 0, 0, 0, {{}}, 0, 0, {tail},\n}};"
        room_def = re.sub(short_tail, expanded, room_def, count=1)
        return source[:room_start] + room_def + source[room_end + 3:]
    separator = "" if source[:room_end].rstrip().endswith(",") else ","
    return source[:room_end] + separator + "\n    " + tail + "," + source[room_end:]


def compile_map(rows: list[str], source: str, room_id: str) -> str:
    platform_lines = [fmt_rect(rect) for rect in pack_rects(cells(rows, "#"))]
    platform_lines += [
        f"{{ {fmt_num(x)}, {fmt_num(y)}, {fmt_num(width)}, T(0.25f) }}"
        for x, y, width in horizontal_runs(rows, "=")
    ]
    platform_lines += [
        f"{{ {fmt_num(x)}, {fmt_num(y)}, T(0.25f), {fmt_num(height)} }}"
        for x, y, height in vertical_runs(rows, "|")
    ]
    source = replace_array(source, f"g_room{room_id}_platforms", platform_lines)
    source = upsert_type_a_walls(source, room_id, [fmt_rect(rect) for rect in pack_rects(cells(rows, "A"))])

    speaker_lines = []
    for component in components(cells(rows, "Kk")):
        rects = pack_rects(component)
        if len(rects) != 1:
            raise ValueError("K 스피커는 하나의 직사각형이어야 합니다.")
        x, y, w, h = rects[0]
        if (w, h) == (5, 9):
            speaker_lines.append(f"DefaultSpeakerAt({fmt_num(x)}, {fmt_num(y)})")
        elif (w, h) == (3, 5):
            speaker_lines.append(f"MiniSpeakerAt({fmt_num(x)}, {fmt_num(y)})")
        else:
            raise ValueError(f"지원하지 않는 K 스피커 크기: {w}×{h}")
    source = replace_array(source, f"g_room{room_id}_speakers", speaker_lines)

    box_lines = []
    for component in components(cells(rows, "BQ")):
        min_x = min(x for x, _ in component)
        max_x = max(x for x, _ in component)
        min_y = min(y for _, y in component)
        max_y = max(y for _, y in component)
        anchors = [(x, y) for x, y in component if rows[y][x] == "Q"]
        if len(anchors) > 1:
            raise ValueError("B 예약 영역에는 Q 앵커를 하나만 둘 수 있습니다.")
        if anchors:
            x, y = anchors[0]
            box_lines.append(f"{{ DefaultGravityBoxAt({fmt_num(x - .25)}, {fmt_num(y - .25)}) }}")
        else:
            box_lines.append(f"{{ DefaultGravityBoxAt({fmt_num((min_x + max_x + 1) / 2 - .75)}, {fmt_num((min_y + max_y + 1) / 2 - .75)}) }}")
    source = replace_array(source, f"g_room{room_id}_gravity_boxes", box_lines)

    starts = marker_rects(rows, "I", False)
    targets = marker_rects(rows, "i", False)
    old_pistons = source_pistons(source)
    if len(starts) != len(targets):
        raise ValueError("ASCII I/i 피스톤 수가 일치하지 않습니다.")
    remaining_starts = set(range(len(starts)))
    tuning_by_start = {}
    for piston in old_pistons:
        if not remaining_starts:
            break
        index = min(
            remaining_starts,
            key=lambda candidate: abs(starts[candidate]["rect"][0] - piston["x"]) + abs(starts[candidate]["rect"][1] - piston["y"]),
        )
        remaining_starts.remove(index)
        tuning_by_start[index] = piston
    # An I/i rectangle can have both a horizontal and a vertical counterpart
    # when pistons share the same columns or rows.  Prefer the vertically
    # aligned counterpart: ROOM maps use that layout for opposing ceiling and
    # floor pistons, while a horizontal piston has no vertically aligned goal.
    remaining_targets = set(range(len(targets)))
    paired_targets = {}
    for index, start in enumerate(starts):
        x, y, w, h = start["rect"]
        candidates = [
            candidate for candidate in remaining_targets
            if targets[candidate]["rect"][2:] == (w, h)
            and (targets[candidate]["rect"][0] == x or targets[candidate]["rect"][1] == y)
        ]
        if not candidates:
            raise ValueError("I와 같은 크기의 수평 또는 수직 i 목표를 찾을 수 없습니다.")
        vertical = [candidate for candidate in candidates if targets[candidate]["rect"][0] == x]
        pool = vertical or candidates
        target_index = min(
            pool,
            key=lambda candidate: abs(targets[candidate]["rect"][0] - x)
            + abs(targets[candidate]["rect"][1] - y),
        )
        remaining_targets.remove(target_index)
        paired_targets[index] = targets[target_index]

    piston_lines = []
    for index, start in enumerate(starts):
        x, y, w, h = start["rect"]
        target = paired_targets[index]
        dx, dy = target["rect"][0] - x, target["rect"][1] - y
        if dx and dy:
            raise ValueError("I/i 피스톤은 수평 또는 수직으로만 이동해야 합니다.")
        direction = "PISTON_UP" if dy < 0 else "PISTON_DOWN" if dy > 0 else "PISTON_LEFT" if dx < 0 else "PISTON_RIGHT"
        device_width, device_body_height = (h, w) if dx else (w, h)
        tuning = tuning_by_start.get(index, {"shaft_width": "T(0.90f)", "plate_height": "T(1)", "plate_height_value": 1.0, "start_delay_seconds": "0.000f"})
        shaft_width = tuning["shaft_width"]
        plate_height = tuning["plate_height"]
        body_x, body_y = x, y
        if dx > 0:
            body_x -= device_body_height
        elif dx < 0:
            body_x += tuning["plate_height_value"]
        elif dy < 0:
            body_y += tuning["plate_height_value"]
        else:
            body_y -= device_body_height
        piston_lines.append(f"{{ {fmt_num(body_x)}, {fmt_num(body_y)}, {fmt_num(device_width)}, {fmt_num(device_body_height)}, {shaft_width}, {plate_height}, {fmt_num(abs(dx + dy))}, {tuning["start_delay_seconds"]}, {direction} }}")
    source = upsert_pistons(source, room_id, piston_lines)

    markers = []
    for y, row in enumerate(rows):
        for match in re.finditer(r"M[12]", row):
            markers.append((match.start(), y, match.group()))
    old_enemies = source_enemies(source)
    enemy_lines = []
    for x, y, kind in markers:
        candidates = [enemy for enemy in old_enemies if enemy["kind"] == kind]
        if candidates:
            tuning = min(candidates, key=lambda enemy: abs(enemy["x"] - x) + abs(enemy["y"] - y))
            old_enemies.remove(tuning)
            speed, direction = tuning["speed"], tuning["direction"]
        else:
            speed, direction = DEFAULT_ENEMY_TUNING[kind]
        enemy_lines.append(
            f"{{ {{ {fmt_num(x + .25)}, {fmt_num(y + .10)}, T(1.5f), T(0.9f) }}, {speed}, {direction}, WALKER_ENEMY_{kind} }}"
        )
    source = upsert_walker_enemies(source, room_id, enemy_lines)

    static_markers = static_spike_markers(rows)
    static_spike_lines = [
        f"StaticSpikeAt({fmt_num(x)}, {fmt_num(y)}, {STATIC_SPIKE_MARKERS[marker]})"
        for x, y, marker in static_markers
    ]

    switches = sorted(marker_rects(rows, "T", True), key=lambda device: device["id"])
    switch_masks = {
        identifier: " | ".join(
            f"1u << {index}"
            for index, device in enumerate(switches)
            if device["id"] == identifier
        )
        for identifier in {device["id"] for device in switches}
    }
    switch_lines = [
        f"{{ {fmt_rect(device['rect'])}, {pressure_switch_mount(rows, device)} }}"
        for device in switches
    ]
    source = upsert_room_array(source, "PressureSwitchDevice", f"g_room{room_id}_pressure_switches", switch_lines)

    pressure = []
    for upper, lower, horizontal in (("H", "h", True), ("V", "v", False)):
        targets = marker_rects(rows, lower, True)
        for closed in marker_rects(rows, upper, True):
            x, y, w, h = closed["rect"]
            matching_targets = [target for target in targets if target["id"] == closed["id"] and target["rect"][2:] == (w, h)]
            if matching_targets:
                target = find_open(closed, targets)
                dx, dy = target["rect"][0] - x, target["rect"][1] - y
                if (horizontal and dy) or (not horizontal and dx):
                    raise ValueError(f"{upper}/{lower} movement direction is invalid.")
            else:
                dx, dy = (w, 0) if horizontal else (0, h)
            pressure.append((closed["rect"], dx, dy, closed["id"], False))
    for closed in marker_rects(rows, "X", True):
        pressure.append((closed["rect"], 0, 0, closed["id"], True))
    pressure.sort(key=lambda item: (item[0][1], item[0][0]))
    pressure_lines = [
        f"{{ {fmt_rect(rect)}, {fmt_num(dx)}, {fmt_num(dy)}, {switch_masks[identifier]}{', 1' if disappears else ''} }}"
        for rect, dx, dy, identifier, disappears in pressure
    ]
    source = upsert_room_array(source, "PressurePlatformDevice", f"g_room{room_id}_pressure_platforms", pressure_lines)

    start = one_marker(rows, "P")
    exit_components = components(cells(rows, "E"))
    if len(exit_components) != 1:
        raise ValueError("E marker must be one connected rectangle.")
    exit_rects = pack_rects(exit_components[0])
    if len(exit_rects) != 1:
        raise ValueError("E marker must be rectangular.")
    exit_rect = exit_rects[0]
    checkpoints = sorted(cells(rows, "C"), key=lambda cell: (cell[1], cell[0]))
    checkpoint_name = f"g_room{room_id}_checkpoints"
    checkpoint_lines = [fmt_rect((x, y, 1, 1)) for x, y in checkpoints] or [fmt_rect((0, 0, 0, 0))]
    checkpoint_decl = f"static const RectF {checkpoint_name}[] = {{" + fmt_array(checkpoint_lines) + "};" + "\n"
    checkpoint_pattern = rf"static const RectF {re.escape(checkpoint_name)}\[\]\s*=\s*\{{.*?\s*\}};"
    if checkpoints and re.search(checkpoint_pattern, source, re.S):
        source = replace_array(source, checkpoint_name, checkpoint_lines)
    elif checkpoints:
        source = source.replace("\nextern const RoomDef", "\n" + checkpoint_decl + "\nextern const RoomDef", 1)
    room_def = (
        r"\{\s*T\([^)]+\),\s*T\([^)]+\),\s*T\([^)]+\),\s*T\([^)]+\)\s*\},\s*"
        r"T\([^)]+\),\s*T\([^)]+\)(?:\s*[+-]\s*[^,]+)?,\s*"
        r"\{\s*T\(0\),\s*T\(0\),\s*T\([^)]+\),\s*T\([^)]+\)\s*\}"
    )
    room_def_value = (
        f"{fmt_rect(exit_rect)}, "
        f"{fmt_num(start[0])}, {fmt_num(start[1])}, "
        f"{{ T(0), T(0), {fmt_num(len(rows[0]))}, {fmt_num(len(rows))} }}"
    )
    source, count = re.subn(room_def, room_def_value, source, count=1, flags=re.S)
    if count != 1:
        raise ValueError("RoomDef start position was not found.")
    if checkpoints:
        legacy_checkpoint = fmt_rect((checkpoints[0][0], checkpoints[0][1], 1, 1))
        checkpoint_def = rf"(g_room{room_id}_walker_enemies,\s*\(int\)\(sizeof\(g_room{room_id}_walker_enemies\) / sizeof\(g_room{room_id}_walker_enemies\[0\]\)\),\s*\d+,\s*)(\{{\s*T\([^)]+\),\s*T\([^)]+\),\s*T\(1\),\s*T\(1\)\s*\}})(?:,\s*{checkpoint_name},\s*\(int\)\(sizeof\({checkpoint_name}\) / sizeof\({checkpoint_name}\[0\]\)\))?(?=,\s*(?:g_room{room_id}_static_spikes|\}};))"
        replacement = rf"\g<1>{legacy_checkpoint}, {checkpoint_name}, (int)(sizeof({checkpoint_name}) / sizeof({checkpoint_name}[0]))"
        source, count = re.subn(checkpoint_def, replacement, source, count=1, flags=re.S)
        if count != 1:
            empty_checkpoint_def = rf"(g_room{room_id}_walker_enemies,\s*\(int\)\(sizeof\(g_room{room_id}_walker_enemies\) / sizeof\(g_room{room_id}_walker_enemies\[0\]\)\),\s*)(?:0,\s*)?\{{\}},\s*0,\s*0(?=\s*(?:,\s*g_room{room_id}_static_spikes|\}};))"
            replacement = rf"\g<1>0, {legacy_checkpoint}, {checkpoint_name}, (int)(sizeof({checkpoint_name}) / sizeof({checkpoint_name}[0]))"
            source, count = re.subn(empty_checkpoint_def, replacement, source, count=1, flags=re.S)
        if count != 1:
            raise ValueError("RoomDef checkpoints were not found.")
    source = upsert_static_spikes(source, room_id, static_spike_lines)
    return source


def main() -> int:
    parser = argparse.ArgumentParser(description="Compile ROOM ASCII map placement arrays into C++.")
    parser.add_argument("--room", required=True, type=int)
    parser.add_argument("--write", action="store_true", help="roomXX.cpp에 적용합니다. 기본은 읽기 전용입니다.")
    args = parser.parse_args()
    room_id = f"{args.room:02d}"
    map_path = ROOT / "docs" / "rooms" / f"ROOM_{room_id}_MAP.md"
    code_path = ROOT / "src" / "game" / "rooms" / f"room{room_id}.cpp"
    try:
        old = code_path.read_text(encoding="utf-8")
        compiled = compile_map(rows_from_map(map_path), old, room_id)
    except (OSError, ValueError) as error:
        print(f"ROOM {room_id}: {error}", file=sys.stderr)
        return 1
    changed = old != compiled
    print(f"ROOM {room_id}: {'변경 감지' if changed else '이미 동기화됨'}")
    if args.write and changed:
        code_path.write_text(compiled, encoding="utf-8", newline="\n")
        print(f"적용: {code_path}")
    elif changed:
        print("적용하지 않았습니다. 확인 후 --write를 붙이세요.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
