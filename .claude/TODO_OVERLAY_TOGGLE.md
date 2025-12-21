# TODO: Overlay Toggle Feature

## 목표

FIFO 명령으로 가사 오버레이를 활성화/비활성화하는 기능 구현

## 요구사항

- **트리거**: `/tmp/wshowlyrics.fifo`로 `show`, `hide`, `toggle` 명령 전송
- **시각적 피드백**: System tray 아이콘 변경 (비활성 시 헤드폰 + 빨간 X)
- **동작**: 비활성 시 투명 렌더링 (일시정지와 동일)
- **Compositor 통합**: 사용자가 단축키 설정 (예: `bindsym Super+Break exec echo toggle > /tmp/wshowlyrics.fifo`)

## 구현 계획

### Phase 1: 상태 관리 (15분)

- [ ] **파일**: `src/main.h`
  - `struct lyrics_state`에 `bool overlay_enabled;` 추가

- [ ] **파일**: `src/main.c`
  - `state.overlay_enabled = true;` 초기화 (기본값: 활성)

### Phase 2: FIFO 명령 확장 (15분)

- [ ] **파일**: `src/main.c` (lines 360-394)
  - `show` 명령: `state.overlay_enabled = true;` + 아이콘 업데이트 + dirty
  - `hide` 명령: `state.overlay_enabled = false;` + 아이콘 업데이트 + dirty
  - `toggle` 명령: 상태 반전 + 아이콘 업데이트 + dirty
  - 기존 숫자 명령 호환성 유지 (else 블록)

### Phase 3: 렌더링 조건 추가 (15분)

- [ ] **파일**: `src/main.c` (lines 425-440 근처)
  - `if (!state.overlay_enabled)` 체크 추가 (최우선)
  - 비활성 시: `state.current_line = NULL;` + 투명 렌더링
  - 활성 시: 기존 `mpris_is_playing()` 로직 유지

### Phase 4: 비활성 아이콘 생성 (45분)

- [ ] **파일**: `src/user_experience/system_tray/system_tray.c`
  - `create_disabled_icon()` 함수 추가:
    - 헤드폰 아이콘 로드 (fallback: audio-headphones → audio-player → multimedia-player)
    - Cairo surface 생성
    - 원본 아이콘 그리기
    - 오른쪽 하단 1/4 크기 (12x12)에 빨간 X 그리기
      - 배경: 투명
      - 전경: `rgb(1.0, 0.0, 0.0)` (빨간색)
      - 두께: 2.5px, rounded cap
      - 2px 여백
    - GdkPixbuf로 변환 후 반환

### Phase 5: 아이콘 전환 함수 (30분)

- [ ] **파일**: `src/user_experience/system_tray/system_tray.h`
  - `void system_tray_set_overlay_state(bool enabled);` 선언

- [ ] **파일**: `src/user_experience/system_tray/system_tray.c`
  - `system_tray_set_overlay_state()` 함수 구현:
    - 정적 변수: `current_state` (중복 업데이트 방지)
    - `enabled == true`: 기본 아이콘 복원 (앨범아트 유지 또는 reset)
    - `enabled == false`: 비활성 아이콘 생성 → PNG 저장 → 업데이트
    - 상수: `DISABLED_ICON_PATH`, `DISABLED_ICON_NAME`

### Phase 6: 테스트 (30분)

- [ ] **빌드 테스트**:
  ```bash
  meson compile -C build
  ```

- [ ] **FIFO 명령 테스트**:
  ```bash
  echo hide > /tmp/wshowlyrics.fifo
  echo show > /tmp/wshowlyrics.fifo
  echo toggle > /tmp/wshowlyrics.fifo
  ```

- [ ] **기존 기능 호환성**:
  ```bash
  echo +100 > /tmp/wshowlyrics.fifo
  echo -50 > /tmp/wshowlyrics.fifo
  echo 0 > /tmp/wshowlyrics.fifo
  ```

- [ ] **System Tray 아이콘 확인**:
  - 비활성: 헤드폰 + 오른쪽 하단 빨간 X
  - 활성: 기본 헤드폰 또는 앨범아트

## 예상 작업 시간

- **코드 구현**: 1-2시간
- **테스트**: 30분
- **총합**: 1.5-2.5시간

## 참고 자료

- **계획 문서**: `/home/hm/.claude/plans/mellow-snuggling-hedgehog.md`
- **FIFO 처리**: `src/main.c:360-394`
- **투명 렌더링**: `src/core/rendering/rendering_manager.c:275-296`
- **System Tray**: `src/user_experience/system_tray/system_tray.c`

## Compositor 설정 예시

### Sway (`~/.config/sway/config`)
```
bindsym Mod4+Pause exec echo toggle > /tmp/wshowlyrics.fifo
```

### Hyprland (`~/.config/hypr/hyprland.conf`)
```
bind = SUPER, BREAK, exec, echo toggle > /tmp/wshowlyrics.fifo
```

## 완료 조건

- [x] TODO 파일 생성
- [ ] 모든 Phase 완료
- [ ] 빌드 성공
- [ ] FIFO 명령 동작 확인
- [ ] System tray 아이콘 변경 확인
- [ ] 기존 timing offset 명령 호환성 확인
