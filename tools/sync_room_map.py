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
    match = re.search(r"~~~text\s*\r?\n(.*?)\r?\n~~~", path.read_text(encoding="utf-8"), re.S)
    if not match:
        raise ValueError("~~~text ASCII 블록을 찾을 수 없습니다.")
    rows = match.group(1).splitlines()
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
        raise ValueError(f"{name} 배열을 찾을 수 없습니다.")
    return source[:match.end(1)] + fmt_array(lines) + "};" + source[match.end(2):]


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
        r"(T\([^)]+\)),\s*(T\([^)]+\)),\s*T\([^)]+\),\s*([^,]+),\s*PISTON_[A-Z]+\s*\}"
    )
    result = []
    for x, y, shaft_width, plate_height, phase in re.findall(pattern, source):
        result.append({
            "x": float(x.rstrip("f")),
            "y": float(y.rstrip("f")),
            "shaft_width": shaft_width,
            "plate_height": plate_height,
            "phase": phase,
        })
    return result


def source_enemies(source: str) -> list[dict]:
    pattern = (
        r"\{\s*\{\s*T\(([^)]+)\),\s*T\(([^)]+)\),\s*T\([^)]+\),\s*T\([^)]+\)\s*\},\s*"
        r"([^,]+),\s*([^,]+),\s*WALKER_ENEMY_(M[12])\s*\}"
    )
    return [
        {"x": float(x.rstrip("f")), "y": float(y.rstrip("f")), "speed": speed, "direction": direction, "kind": kind}
        for x, y, speed, direction, kind in re.findall(pattern, source)
    ]


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
    source = replace_array(source, f"g_room{room_id}_type_a_walls", [fmt_rect(rect) for rect in pack_rects(cells(rows, "A"))])

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
        index = min(
            remaining_starts,
            key=lambda candidate: abs(starts[candidate]["rect"][0] - piston["x"]) + abs(starts[candidate]["rect"][1] - piston["y"]),
        )
        remaining_starts.remove(index)
        tuning_by_start[index] = piston
    piston_lines = []
    for index, start in enumerate(starts):
        x, y, w, h = start["rect"]
        same_size = [target for target in targets if target["rect"][2:] == (w, h)]
        if not same_size:
            raise ValueError("I와 같은 크기의 i 목표를 찾을 수 없습니다.")
        target = min(same_size, key=lambda item: abs(item["rect"][0] - x) + abs(item["rect"][1] - y))
        dx, dy = target["rect"][0] - x, target["rect"][1] - y
        if dx and dy:
            raise ValueError("I/i 피스톤은 수평 또는 수직으로만 이동해야 합니다.")
        direction = "PISTON_UP" if dy < 0 else "PISTON_DOWN" if dy > 0 else "PISTON_LEFT" if dx < 0 else "PISTON_RIGHT"
        device_width, device_body_height = (h, w) if dx else (w, h)
        tuning = tuning_by_start.get(index, {"shaft_width": "T(0.90f)", "plate_height": "T(1)", "phase": "0.00f"})
        piston_lines.append(f"{{ {fmt_num(x)}, {fmt_num(y)}, {fmt_num(device_width)}, {fmt_num(device_body_height)}, {tuning['shaft_width']}, {tuning['plate_height']}, {fmt_num(abs(dx + dy))}, {tuning['phase']}, {direction} }}")
    source = replace_array(source, f"g_room{room_id}_pistons", piston_lines)

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
    source = replace_array(source, f"g_room{room_id}_walker_enemies", enemy_lines)

    switch_lines = [
        f"{{ {fmt_rect(device['rect'])}, {'PRESSURE_SWITCH_MOUNT_RIGHT' if device['rect'][2] < device['rect'][3] else 'PRESSURE_SWITCH_MOUNT_DOWN'} }}"
        for device in sorted(marker_rects(rows, "T", True), key=lambda device: device["id"])
    ]
    source = replace_array(source, f"g_room{room_id}_pressure_switches", switch_lines)

    pressure = []
    for upper, lower, horizontal in (("H", "h", True), ("V", "v", False)):
        targets = marker_rects(rows, lower, True)
        for closed in marker_rects(rows, upper, True):
            target = find_open(closed, targets)
            x, y, w, h = closed["rect"]
            dx, dy = target["rect"][0] - x, target["rect"][1] - y
            if (horizontal and dy) or (not horizontal and dx):
                raise ValueError(f"{upper}/{lower} 이동 방향이 맞지 않습니다.")
            pressure.append((closed["rect"], dx, dy, closed["id"], False))
    for closed in marker_rects(rows, "X", True):
        pressure.append((closed["rect"], 0, 0, closed["id"], True))
    pressure.sort(key=lambda item: (item[0][1], item[0][0]))
    pressure_lines = [
        f"{{ {fmt_rect(rect)}, {fmt_num(dx)}, {fmt_num(dy)}, 1u << {identifier - 1}{', 1' if disappears else ''} }}"
        for rect, dx, dy, identifier, disappears in pressure
    ]
    source = replace_array(source, f"g_room{room_id}_pressure_platforms", pressure_lines)

    start, exit_, checkpoint = one_marker(rows, "P"), one_marker(rows, "E"), one_marker(rows, "C")
    room_def = (
        r"\{\s*T\([^)]+\),\s*T\([^)]+\),\s*T\(1\),\s*T\(1\)\s*\},\s*"
        r"T\([^)]+\),\s*T\([^)]+\),\s*\{\s*T\(0\)"
    )
    room_def_value = (
        f"{{ {fmt_num(exit_[0])}, {fmt_num(exit_[1])}, T(1), T(1) }}, "
        f"{fmt_num(start[0])}, {fmt_num(start[1])}, {{ T(0)"
    )
    source, count = re.subn(room_def, room_def_value, source, count=1, flags=re.S)
    if count != 1:
        raise ValueError("RoomDef 시작 좌표를 찾을 수 없습니다.")
    checkpoint_def = rf"(g_room{room_id}_walker_enemies,\s*\(int\)\(sizeof\(g_room{room_id}_walker_enemies\) / sizeof\(g_room{room_id}_walker_enemies\[0\]\)\),\s*\d+,\s*)(\{{\s*T\([^)]+\),\s*T\([^)]+\),\s*T\(1\),\s*T\(1\)\s*\}})(,\s*\}};)"
    source, count = re.subn(
        checkpoint_def, rf"\g<1>{fmt_rect((checkpoint[0], checkpoint[1], 1, 1))}\g<3>", source, count=1, flags=re.S
    )
    if count != 1:
        raise ValueError("RoomDef 체크포인트 좌표를 찾을 수 없습니다.")
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
