# RE:CONFIG 문서 책임 가이드

## 목적

이 문서는 각 문서가 무엇을 결정하고 어디에 기록해야 하는지 정한다. 같은 규칙을 여러 문서에 복사해 서로 다른 사양이 생기는 일을 막고, ROOM 문서가 맵 구조와 해법에 집중하게 한다.

문서 변경 전에는 이 가이드, [구현 변경 규칙](IMPLEMENTATION_RULES.md), 그리고 변경 대상의 소유 문서를 함께 확인한다.

## 문서별 책임

| 문서 | 책임 | 기록하지 않는 것 |
| --- | --- | --- |
| [reconfig_design.md](reconfig_design.md) | 기획 문서의 진입점과 최신 기준 문서 목록 | 개별 기믹·ROOM의 상세 사양 |
| [design/core_concept.md](design/core_concept.md) | 게임의 한 줄 소개, 핵심 루프, 제품 방향 | 개별 ROOM 해법과 구현 수치 |
| [design/settings_gimmicks.md](design/settings_gimmicks.md) | SETTINGS가 바꾸는 월드 규칙과 기믹의 확정 상태 | 특정 ROOM의 배치 좌표·풀이 순서 |
| [design/level_tutorial.md](design/level_tutorial.md) | 전체 진행, 학습 순서, 레벨 설계 원칙 | 특정 ROOM의 ASCII 맵 |
| [design/art_ui.md](design/art_ui.md) | 공통 아트·팔레트·UI 표현 규칙 | ROOM별 플랫폼 배치 |
| `design/<기믹>.md` | 적, 박스, 피스톤처럼 여러 ROOM에서 공유하는 오브젝트의 공통 동작·충돌·시각·기본 수치 | 특정 ROOM에서 그 오브젝트가 맡는 퍼즐 역할 |
| [design/decisions_open_items.md](design/decisions_open_items.md) | 확정·후보·미확정의 상태 목록 | 사양의 상세 원문 |
| [IMPLEMENTATION_RULES.md](IMPLEMENTATION_RULES.md) | 규칙 변경 시 조사·승인·검증 절차 | 개별 기믹 또는 맵의 사양 |
| [rooms/ROOM_MAP_GUIDE.md](rooms/ROOM_MAP_GUIDE.md) | ROOM ASCII 맵의 단위, 범례, 표기와 코드 반영 규칙 | 적·피스톤 등 공통 동작의 상세 |
| `rooms/ROOM_XX_MAP.md` | ROOM의 공간 구조, 오브젝트 배치, 구간별 의도, 풀이 흐름, 방별 예외 | 공통 기믹의 감지 거리·상태 시간·충돌 계산·렌더 세부 |
| [README.md](../README.md) | 현재 빌드의 빠른 실행·조작 안내 | 최종 기획의 기준 사양 |

## 공통 기믹과 ROOM의 경계

공통 기믹 문서는 “오브젝트가 항상 어떻게 동작하는가”를 소유한다. 예를 들어 M1의 감지 거리, 가시 전개와 수납, 충돌, BRICK 상호작용, 외형은 [M1](design/walker_enemy.md)에만 기록한다.

ROOM 문서는 “그 동작을 이 방에서 왜 사용하는가”를 소유한다. ROOM에는 다음을 기록한다.

- 오브젝트가 놓이는 위치·왕복 구간·다른 구조물과의 관계
- 관찰, 우회, 되돌아감, 상태 전환이 만드는 경로 판단
- 스위치·문·EXIT와 연결된 방별 해법
- 공통 기본값과 다른 시작 방향·속도·크기처럼 실제로 해당 방에만 적용되는 예외

ROOM은 공통 문서를 링크할 수 있지만, 공통 동작을 다시 서술하거나 수치를 복사하지 않는다.

## 새 규칙 또는 기믹을 추가할 때

1. 공통 동작이 생기면 `docs/design/`에 해당 기믹 문서를 만들거나 기존 문서를 갱신한다.
2. [reconfig_design.md](reconfig_design.md) 인덱스에서 새 기준 문서로 연결한다.
3. 사용하는 ROOM에는 배치와 해법만 기록하고, 공통 문서 링크만 남긴다.
4. 기존 기본 상호작용·충돌·시각·퍼즐 경로를 바꾸면 [구현 변경 규칙](IMPLEMENTATION_RULES.md)의 승인 절차를 따른다.

## 별도 관리가 필요한 기존 초안

- `docs/design/room08_map_48x27.md`는 현재 `docs/rooms/ROOM_08_MAP.md`와 별개로 남아 있는 과거 ROOM 08 초안이다. 현재 ROOM 08의 기준 맵이나 공용 규칙으로 사용하지 않는다.
- 새 ROOM 맵과 현재 ROOM 수정은 `docs/rooms/ROOM_XX_MAP.md`와 [ROOM 맵 가이드](rooms/ROOM_MAP_GUIDE.md)에만 기록한다.
- 이 기존 초안의 삭제·이관·통합은 두 문서와 현재 코드의 차이를 별도로 검토한 뒤 결정한다.

## 문서 충돌 처리

- 문서와 코드가 다르면 임의로 한쪽을 정답으로 고치지 않는다. 현재 구현, 문서의 소유 범위, 플레이 영향부터 확인한다.
- 공통 규칙은 해당 공통 문서가, ROOM의 배치와 해법은 해당 ROOM 문서가 소유한다.
- 확정·후보·현 구현 참고를 구분해 적고, 후보를 승인된 규칙처럼 구현하지 않는다.
- 공통 사양을 바꾸면 그 문서 하나를 갱신하고, 영향받는 ROOM의 해법·배치 의도만 다시 검토한다.