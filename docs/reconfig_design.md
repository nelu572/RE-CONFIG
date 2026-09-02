# RE:CONFIG 기획서 인덱스

이 문서는 현재 프로젝트의 최신 게임 기획 기준 문서 묶음에 대한 인덱스다.

공식 게임명은 `RE:CONFIG`다. 플레이어가 설정 메뉴의 값을 변경해 월드의 규칙과 상태를 재구성하고 퍼즐을 해결하는 현재 콘셉트를 기준으로 한다.

## 기준 문서

- [문서 책임 가이드](DOCUMENTATION_GUIDE.md)
- [핵심 콘셉트](design/core_concept.md)
- [설정 기믹](design/settings_gimmicks.md)
- [레벨과 튜토리얼](design/level_tutorial.md)
- [아트와 UI](design/art_ui.md)
- [기본 이동형 적](design/walker_enemy.md)
- [압력 스위치와 이동 문](design/pressure_switch.md)
- [중력 박스](design/gravity_box.md)
- [피스톤](design/piston.md)
- [스피커와 음파](design/speaker.md)
- [체크포인트](design/checkpoint.md)
- [확정 사항과 미확정 사항](design/decisions_open_items.md)
- [구현 변경 규칙](IMPLEMENTATION_RULES.md)

## 문서 상태

- 최신 방향 반영일: 2026-08-17
- 문서 성격: 목표 기획 기준 문서
- 주의: 현재 구현 상태와 최종 기획은 다를 수 있다. 구현 현황은 별도 README와 소스 코드를 확인한다.

## 이전 방향에서 변경된 핵심

- 단순 기능 제거 중심의 게임 설명을 더 이상 핵심 기획으로 사용하지 않는다.
- 설정 UI를 월드 규칙 조작 장치로 사용한다.
- 설정 조작은 마우스 없이 키보드만 사용한다.
- 설정값은 연속 슬라이더보다 ON/OFF 또는 명확한 단계/선택지로 설계한다.
- 확정된 기믹과 후보/검토 중인 기믹을 분리해 관리한다.
- 현재 아트/UI 문서에 Red Platform 컬러 테스트 팔레트와 색상별 역할을 반영했다.
