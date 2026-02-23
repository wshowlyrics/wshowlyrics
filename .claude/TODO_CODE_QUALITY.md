# TODO: Code Quality & Security Improvements

## 현재 상태 (2026-02-23, 2차 업데이트)

### 완료된 Phase
- **Phase 1-8**: Bugs 0개, Vulnerabilities 0개, BLOCKER 0개
- **Phase S**: 보안취약점 3건 + 동시성 버그 수정 (d51ec9e)
- **Phase H**: HIGH 5건 수정 (caa80a4)
- **Phase 9**: Cognitive Complexity CRITICAL 2건 수정 (ba85ad3)
- **Phase 10 일부**: MAJOR 8건 수정 (23ee4fc) — 미사용 파라미터 4건, if 병합 3건, 중첩 break 1건
- **COPR 빌드 수정**: translator_common.c NULL 체크 (4d014d0)

### 📊 SonarCloud 현황

| 심각도 | 이전 (2/23 오전) | 현재 (2/23 오후) | 변동 |
|--------|:---:|:---:|:---:|
| BLOCKER | 0 | **1** | +1 (회귀) |
| CRITICAL | 2 | **7** | +5 (회귀 3 + 신규감지 4) |
| MAJOR | 48 | **36** | -12 |
| MINOR | 129 | **132** | +3 |
| **합계** | **179** | **176** | **-3** |

### 회귀 이슈 (lrclib_provider.c 리팩토링으로 발생)

중첩 break 감소 리팩토링(23ee4fc)에서 while 조건에 대입식을 넣으면서 3건 발생:

| 심각도 | 파일 | 라인 | Rule | 문제 |
|--------|------|------|------|------|
| **BLOCKER** | lrclib_provider.c | 114 | S912 | `&&` 우측에 side effect (대입식) |
| CRITICAL | lrclib_provider.c | 115 | S859 | `(char *)search_pos` const 캐스트 |
| CRITICAL | lrclib_provider.c | 128 | S134 | 중첩 깊이 > 3 |

### 신규 감지 이슈 (기존 코드, SonarCloud가 새로 탐지)

| 심각도 | 파일 | 라인 | Rule | 문제 |
|--------|------|------|------|------|
| CRITICAL | mpris.c | 227 | S134 | 중첩 깊이 > 3 |
| CRITICAL | config.c | 444 | S134 | 중첩 깊이 > 3 |
| CRITICAL | system_tray.c | 346 | S859 | const 캐스트 (x2) |
| CRITICAL | system_tray.c | 697 | S859 | const 캐스트 |

---

## Phase 10-A: 회귀 이슈 수정 (최우선)

### 10-A1. lrclib_provider.c BLOCKER + CRITICAL 3건

**원인**: `find_best_match_in_results()`에서 while 조건에 `obj_start = strchr(...)` 대입을 넣은 것.

**수정 방향**:
- `&&` 우측의 대입식을 루프 본문 첫 줄로 이동
- `strchr`에 const-correct 포인터 전달
- 중첩 깊이 줄이기 위해 early-continue 패턴 적용

---

## Phase 10-B: 신규 CRITICAL 이슈 (4건)

### 10-B1. mpris.c:227 — 중첩 깊이 > 3 (S134)

`list_available_players()` 내 realloc 에러 처리 블록. 중첩 조건문 평탄화 필요.

### 10-B2. config.c:444 — 중첩 깊이 > 3 (S134)

설정 파일 파싱 로직. 헬퍼 함수 추출로 해결.

### 10-B3. system_tray.c:346, 697 — const 캐스트 (S859, 3건)

`posix_spawn()` argv에 const 문자열을 `(char *)` 캐스트하는 패턴.
`posix_spawn`의 `argv`가 `char *const []` 타입이라 캐스트가 필요한 상황이지만, SonarCloud가 위험으로 판단.

**수정 방향**: 비-const 로컬 복사본 사용 또는 `char *` 배열에 strdup 후 free.

---

## Phase 10-C: 남은 MAJOR 이슈 (36건)

### Priority 1: 과다 파라미터 (12건) — 구조체 리팩토링
**Rule**: c:S107 (>7 파라미터)

| 파일 | 라인 | 파라미터 수 |
|------|------|:---------:|
| rendering_manager.c | 65 | 11 |
| rendering_manager.c | 95 | 10 |
| wayland_events.c | 115 | 10 |
| word_render.c | 331 | 9 |
| word_render.c | 168 | 8 |
| word_render.c | 365 | 8 |
| rendering_manager.c | 48 | 8 |
| ruby_render.c | 200 | 8 |
| ruby_render.c | 252 | 8 |
| lrcx_parser.c | 83 | 8 |
| lrcx_parser.c | 208 | 8 |
| dbus_control.c | 66 | 8 |

### Priority 2: 중첩 if 병합 (8건)
**Rule**: c:S1066

| 파일 | 라인 |
|------|------|
| config.c | 1248 |
| parser_utils.c | 620 |
| parser_utils.c | 630 |
| main.c | 281 |
| lrc_parser.c | 154 |
| lyrics_provider.c | 726 |
| lyrics_provider.c | 733 |
| system_tray.c | 193 |

### Priority 3: 미사용 파라미터 (8건)
**Rule**: c:S1172

| 파일 | 라인 | 파라미터 |
|------|------|---------|
| dbus_control.c | 67 | connection |
| dbus_control.c | 68 | sender |
| dbus_control.c | 69 | object_path |
| dbus_control.c | 70 | interface_name |
| dbus_control.c | 157 | name |
| dbus_control.c | 193 | user_data |
| dbus_control.c | 193 | connection |
| dbus_control.c | 198 | user_data |

### Priority 4: 중첩 break (3건)
**Rule**: c:S924

| 파일 | 라인 |
|------|------|
| lyrics_provider.c | 630 |
| mpris.c | 645 |
| translator_common.c | 271 |

### Priority 5: 기타 MAJOR (5건)

| Rule | 파일 | 라인 | 문제 |
|------|------|------|------|
| S125 | lyrics_provider.c | 546 | 주석 처리된 코드 제거 |
| S125 | lyrics_provider.c | 561 | 주석 처리된 코드 제거 |
| S1911 | file_utils.c | 691 | `utime()` → `utimensat()` 교체 |
| S1820 | main.h | 51 | 구조체 필드 42개 (최대 20) |
| S954 | lang_detect.c | 12 | #include 위치 이동 |

---

## Phase 11: MINOR 이슈 (132건)

| Rule | 설명 | 건수 |
|------|------|:---:|
| c:S995 | Pointer-to-const 파라미터 | 47 |
| c:S1659 | 다중 선언 분리 | 39 |
| c:S5350 | Pointer-to-const 변수 | 36 |
| c:S1905 | 불필요한 cast 제거 | 9 |
| c:S886 | 루프 리팩토링 | 1 |
| **합계** | | **132** |

---

## 완료된 Phase 상세

### Phase S: 보안취약점 & 동시성 (완료)

| ID | 문제 | 커밋 |
|----|------|------|
| CRITICAL-S1 | Command Injection via `system()` | d51ec9e |
| CRITICAL-S2 | Data Race — 번역 스레드 `_Atomic` 부재 | d51ec9e |
| CRITICAL-S3 | `realloc()` 메모리 누수 패턴 | d51ec9e |

### Phase H: HIGH 이슈 (완료)

| ID | 문제 | 커밋 |
|----|------|------|
| HIGH-H1 | 번역 스레드 join 실패 시 use-after-free | d51ec9e |
| HIGH-H2 | 파일 크기 제한 없는 파싱 (10MB/5MB) | caa80a4 |
| HIGH-H3 | `sanitize_path()` 정적 버퍼 스레드 안전성 | caa80a4 |
| HIGH-H4 | LRCX 인덱스 추적 로직 오류 | caa80a4 |
| HIGH-H5 | MPRIS TOCTOU 경합 | caa80a4 |
| HIGH-H6 | snprintf 잘림 미검사 | caa80a4 |

### Phase 9: Cognitive Complexity (완료)

| ID | 문제 | 커밋 |
|----|------|------|
| 9-1 | `on_properties_changed()` CC 34→분리 | ba85ad3 |
| 9-2 | `build_search_directories()` CC 28→분리 | ba85ad3 |

### Phase 10 일부 (완료)

| 문제 | 건수 | 커밋 |
|------|:---:|------|
| 미사용 파라미터 (`format_name`, `head` x3) | 4 | 23ee4fc |
| 중첩 if 병합 (parser_utils, shm) | 3 | 23ee4fc |
| 중첩 break 감소 (lrclib_provider) | 1 | 23ee4fc |
| COPR 빌드 수정 (translator_common NULL 체크) | 1 | 4d014d0 |

### MEDIUM 이슈 (미착수)

| # | 파일 | 문제 | 영향 |
|---|------|------|------|
| M1 | main.c:57-105 | 원격 URL 텍스트 `%s` 치환 | Low |
| M2 | mpris.c:1187-1331 | 메타데이터 추출 코드 중복 | Maintainability |
| M3 | translator_common.c:952-957 | cache_path snprintf 잘림 | Truncation |
| M4 | parser_utils.c:792-823 | 멀티바이트 시퀀스 읽기 | UB |
| M5 | lyrics_provider.c:705-721 | 문자열 비교 디스패치 | Maintainability |
| M6 | lyrics_manager.c | join 누락 케이스 | Thread leak |
| M7 | config.c:484 | INI 512바이트 줄 제한 | Data loss |
| M8 | system_tray.c:688 | escape_shell_string 감사 | Potential injection |
| M9 | translator_common.c:166-191 | 캐시 파일 크기 미제한 | OOM |

---

## 권장 작업 순서

1. **Phase 10-A** — lrclib_provider.c 회귀 3건 수정 (BLOCKER 1 + CRITICAL 2)
2. **Phase 10-B** — 신규 CRITICAL 4건 (중첩 깊이 2 + const 캐스트 3)
3. **Phase 10-C** — 남은 MAJOR 36건
4. **Phase 11** — MINOR 132건
5. **MEDIUM** — 코드 분석 발견 이슈 9건

---

## 참고 자료

- **SonarCloud Dashboard**: https://sonarcloud.io/project/overview?id=unstable-code_lyrics
- **SonarCloud Issues API**: https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics
- **Coverity Dashboard**: https://scan.coverity.com/projects/unstable-code-lyrics

---

## 상태

- **최종 업데이트**: 2026-02-23 오후 (Phase 9 + Phase 10 일부 완료 후)
- **현재 Phase**: Phase 10-A (회귀 수정) → 10-B → 10-C → 11
- **완료**: Phase 1-9, Phase S, Phase H, Phase 10 일부
- **남은 이슈**: SonarCloud 176건 (BLOCKER 1 + CRITICAL 7 + MAJOR 36 + MINOR 132)
- **목표**: A등급 유지, Quality Gate GREEN 유지, BLOCKER/CRITICAL 0개
- **참고**: Coverity CID 643610 수정 완료 (b3a410a)
