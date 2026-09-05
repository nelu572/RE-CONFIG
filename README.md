# RE:CONFIG

> **설정 메뉴를 바꾸면, 세계의 규칙도 바뀐다.**

`RE:CONFIG`는 평범한 설정 메뉴의 값을 키보드로 바꾸어 스테이지의 규칙과 상태를 재구성하는 미니멀 플랫폼 퍼즐 프로토타입입니다. 충돌, 캐릭터의 유연성, 장치 속도, 음량처럼 익숙한 옵션이 퍼즐을 푸는 직접적인 도구가 됩니다.

---

## 게임 한눈에 보기

| 항목 | 내용 |
| --- | --- |
| 장르 | 설정 조작형 미니멀 플랫폼 퍼즐 |
| 플레이 방식 | 키보드만으로 이동, 점프, SETTINGS 조작 |
| 목표 | 월드의 변화를 활용해 각 ROOM의 EXIT에 도달 |
| 현재 범위 | ROOM 01–09, 메인 메뉴, 스테이지 선택 |

## 핵심 경험

1. 짧고 제한된 ROOM에서 장애물과 경로를 살핀다.
2. `SETTINGS`를 열어 월드와 연결된 값을 바꾼다.
3. 즉시 달라진 규칙을 관찰하고, 새로운 길로 EXIT를 향한다.

## 설정이 만드는 변화

| 카테고리 | 현재 설정 | 월드에 미치는 영향 |
| --- | --- | --- |
| 게임 | 캐릭터 유연성 | 플레이어의 충돌 크기와 통과 가능한 틈이 바뀝니다. |
| 게임 | 기믹 속도 | 피스톤을 포함한 움직이는 장치의 시뮬레이션 속도가 바뀝니다. |
| 오디오 | BGM 음량 | 배경 음악의 재생 음량이 바뀝니다. |
| 오디오 | 효과음 음량 | 효과음 음량과 스피커 음파의 밀어내기 세기가 함께 바뀝니다. |
| 시스템 | BRICK 충돌 | BRICK WALL을 통과할 수 있는지 바뀝니다. |

## 조작

| 화면 | 조작 |
| --- | --- |
| 메인 메뉴 | `↑` / `↓` 선택 · `Z` 확인 |
| 스테이지 선택 | `←` / `→` 이동 · `Z` ROOM 진입 · `X` 또는 `Esc` 메인 메뉴 |
| 게임 | `←` / `→` 이동 · `↑` 점프 · `R` 재시작 |
| SETTINGS | `X` 열기·상위로·닫기 · `↑` / `↓` 항목 이동 · `←` / `→` 값 변경 · `Z` 항목 선택 |
| 일시정지 | `Esc` 열기 · `↑` / `↓` 선택 · `Z` 확인 · `X` 또는 `Esc` 재개 |
| 전체 화면 | `F11` |

체크포인트가 활성화된 뒤 `R`을 누르거나 사망하면, 해당 지점에서 현재 ROOM을 다시 시작합니다.

## 빌드 및 실행

**요구 사항:** Visual Studio 2022 C++ Build Tools

```bat
:: Build
.\build.bat

:: Run
.\run.bat
```

빌드 결과물은 `dist\reconfig.exe`에 생성됩니다. 배포 파일의 목표 용량은 **1,474,560 bytes**입니다.

## 문서

세부 기믹, ROOM 구성, 그리고 현재 기획의 기준은 [기획 문서 인덱스](docs/reconfig_design.md)에서 확인할 수 있습니다. README는 현재 구현 범위를 안내하며, 기획 문서와 구현이 다를 수 있습니다.

---

# RE:CONFIG — English

> **Change the settings, and change the rules of the world.**

`RE:CONFIG` is a minimalist platform-puzzle prototype where keyboard-controlled settings reconfigure a stage's rules and state. Familiar options—collision, player flexibility, mechanism speed, and volume—become tools for solving puzzles.

---

## At a Glance

| Item | Details |
| --- | --- |
| Genre | Minimalist platform puzzle driven by settings |
| Play style | Move, jump, and operate SETTINGS entirely by keyboard |
| Goal | Use changes in the world to reach each ROOM's EXIT |
| Current scope | ROOM 01–09, main menu, and stage select |

## Core Experience

1. Read the obstacles and routes in a compact ROOM.
2. Open `SETTINGS` and change a value connected to the world.
3. Observe the immediate rule change and find a new path to the EXIT.

## Settings That Change the World

| Category | Current setting | Effect on the world |
| --- | --- | --- |
| Game | Player flexibility | Changes the player's collision size and passable gaps. |
| Game | Mechanism speed | Changes the simulation speed of moving devices, including pistons. |
| Audio | BGM volume | Changes background-music volume. |
| Audio | SFX volume | Changes effect volume and the push strength of speaker waves. |
| System | BRICK collision | Changes whether BRICK WALLs can be passed through. |

## Controls

| Screen | Controls |
| --- | --- |
| Main menu | `↑` / `↓` select · `Z` confirm |
| Stage select | `←` / `→` move · `Z` enter ROOM · `X` or `Esc` return to main menu |
| Game | `←` / `→` move · `↑` jump · `R` restart |
| SETTINGS | `X` open, go back, or close · `↑` / `↓` navigate · `←` / `→` change value · `Z` select |
| Pause | `Esc` open · `↑` / `↓` select · `Z` confirm · `X` or `Esc` resume |
| Fullscreen | `F11` |

After a checkpoint is activated, pressing `R` or dying restarts the current ROOM from that checkpoint.

## Build & Run

**Requirement:** Visual Studio 2022 C++ Build Tools

```bat
:: Build
.\build.bat

:: Run
.\run.bat
```

The build produces `dist\reconfig.exe`. The distribution size target is **1,474,560 bytes**.

## Documentation

For mechanics, ROOM layouts, and the current design reference, see the [design documentation index](docs/reconfig_design.md). This README describes the current implementation scope; it may differ from the design documentation.
