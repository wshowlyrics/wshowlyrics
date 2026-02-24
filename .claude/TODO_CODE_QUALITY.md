# TODO: Code Quality & Security Improvements

## 현재 상태 (2026-02-24, Phase 14 완료)

### 📊 SonarCloud 현황 (실측)

| 심각도 | 시작 (2/23) | 중간 (2/24) | Phase 14 전 | Phase 14 후 (예상) | 총 변동 |
|--------|:---:|:---:|:---:|:---:|:---:|
| BLOCKER | 1 | 0 | 0 | **0** | -1 |
| CRITICAL | 7 | 2 | 0 | **0** | **-7** |
| MAJOR | 36 | 14 | 3 | **1** | **-35** |
| MINOR | 132 | 137 | 34 | **21** | -111 |
| **합계** | **176** | **153** | **37** | **22** | **-154** |

> MAJOR 3건 중 S107 2건은 Won't Fix 처리 완료 (외부 콜백 시그니처)
> Won't Fix 처리 후 실질 MAJOR: **1건** (S1820 main.h)
> MINOR 21건은 모두 외부 콜백 시그니처 S995 → Won't Fix 대상

---

## 남은 이슈

### MAJOR (1건 — 코드 수정 필요)

| 파일 | Rule | 문제 |
|------|------|------|
| main.h:53 | S1820 | `lyrics_state` 구조체 42개 필드 (최대 20) |

### MINOR — Won't Fix 대상 (21건 — SonarCloud 웹 처리)

외부 라이브러리 콜백 시그니처로 파라미터 수정 불가:

| 파일 | 건수 | 사유 |
|------|:---:|------|
| wayland_events.c | 10 | Wayland `wl_*` 콜백 시그니처 |
| system_tray.c | 6 | GTK `GdkPixbuf`, `GtkMenuItem` 콜백 시그니처 |
| dbus_control.c | 3 | GDBus `GDBusConnection` 콜백 시그니처 |
| shm.c | 1 | Wayland `wl_buffer` 콜백 시그니처 |
| wayland_manager.c | 1 | Wayland `wl_registry` 콜백 시그니처 |

### Security Hotspot (1건 — SonarCloud 웹 처리)

| 파일 | Rule | 문제 | 조치 |
|------|------|------|------|
| parser_utils.c | S5813 | `strlen` 사용 안전성 | Safe 마킹 (NULL-terminated 보장) |

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

### Phase 10: MAJOR/CRITICAL 수정 (완료)

| 문제 | 건수 | 커밋 |
|------|:---:|------|
| 미사용 파라미터 (`format_name`, `head` x3) | 4 | 23ee4fc |
| 중첩 if 병합 (parser_utils, shm) | 3 | 23ee4fc |
| 중첩 break 감소 (lrclib_provider) | 1 | 23ee4fc |
| COPR 빌드 수정 (translator_common NULL 체크) | 1 | 4d014d0 |
| lrclib_provider.c 회귀 (S912, S859, S134) | 3 | 8196b99 |
| mpris.c S134 early-continue | 1 | 5f4470d |
| config.c S134 헬퍼 추출 | 1 | 5f4470d |
| system_tray.c S859 strdup 교체 (x3) | 3 | 5f4470d |
| 중첩 if 병합 (S1066 x8) | 8 | 3a62cf3 |
| 미사용 파라미터 dbus_control (S1172 x8) | 8 | 3a62cf3 |
| 주석 수정 (S125 x2) | 2 | 2de305e |
| utime→utimensat (S1911) | 1 | 2de305e |
| include 위치 이동 (S954) | 1 | 2de305e |
| 중첩 break 감소 (S924 x3) | 3 | 4356a66 |

### Phase 11: CRITICAL 2건 + MINOR 135건 (완료)

| 문제 | 건수 | 커밋 |
|------|:---:|------|
| mpris.c S134 중첩 깊이 2건 | 2 | dd1bbb0 |
| lrclib_provider.c S924 중첩 break | 1 | 896119b |
| MINOR 135건 (S995, S1659, S5350, S1905 등) | 135 | b7e7048 |

### Phase 12: S107 구조체 리팩토링 (완료)

| 문제 | 건수 | 커밋 |
|------|:---:|------|
| lrcx_parser.c S107 — `lrcx_line_builder` 구조체 도입 | 2 | 447b07b |
| rendering_manager.c S107 — `offset_bar_context` 구조체 도입 | 3 | 86f6334 |
| ruby_render.c S107 — `ruby_render_context` 구조체 도입 | 2 | 86f6334 |
| word_render.c S107 — `word_render_context` 구조체 도입 | 3+1 | 86f6334 |

### Phase 13: MEDIUM 코드 리뷰 이슈 (완료)

| # | 파일 | 문제 | 조치 | 커밋 |
|---|------|------|------|------|
| M2 | mpris.c | 메타데이터 추출 60줄 중복 | `parse_metadata_from_dict()` 재사용 | 0a7218e |
| M3 | translator_common.c | cache_path snprintf 잘림 | 반환값 검사 추가 | 0a7218e |
| M4 | parser_utils.c | 멀티바이트 시퀀스 UB | null 체크 추가 | 0a7218e |
| M5 | lyrics_provider.c | 문자열 비교 디스패치 | `is_enabled` 콜백 패턴 도입 | 0a7218e |
| M6 | main.c | thread join 누락 2곳 | `cancel_and_wait_translation()` 추가 | 0a7218e |
| M7 | constants.h | INI 512바이트 줄 제한 | CONFIG_LINE_SIZE 4096으로 증가 | 0a7218e |
| M1 | main.c | %s 치환 보안 | 분석 결과 안전 (strstr 수동 치환) | N/A |
| M8 | system_tray.c | escape_shell_string | Phase S에서 이미 해결 (g_spawn_async) | N/A |
| M9 | translator_common.c | 캐시 파일 크기 미제한 | Phase H에서 이미 해결 (MAX_CACHE_FILE_SIZE) | N/A |

### Phase 14: MINOR 잔여 13건 (완료)

| 분류 | 건수 | 파일 | 변경 |
|------|:---:|------|------|
| S995 const 파라미터 | 6 | lrcx_parser, lyrics_provider, translator_common(x2), parser_utils, word_render | `const` 추가 |
| S5350 const 변수 | 3 | main, word_render, string_utils | `const` 추가 |
| S886 루프 리팩토링 | 4 | lrclib_provider (`for`→`while`), lyrics_provider (`break`), mpris (`break`), lang_detect (`for`→`while`) | 루프 구조 개선 |

---

## 권장 작업 순서

1. **Won't Fix 마킹** — MINOR S995 21건 (SonarCloud 웹) + Security Hotspot Safe 마킹
2. **S1820** — main.h `lyrics_state` 구조체 42필드 → 서브 구조체 분리

---

## 참고 자료

- **SonarCloud Dashboard**: https://sonarcloud.io/project/overview?id=unstable-code_lyrics
- **SonarCloud Issues API**: https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics
- **Coverity Dashboard**: https://scan.coverity.com/projects/unstable-code-lyrics

---

## 상태

- **최종 업데이트**: 2026-02-24 (Phase 14 완료)
- **현재 Phase**: Won't Fix 마킹 21건 → S1820 구조체 분리
- **완료**: Phase 1-14 (BLOCKER 0, CRITICAL 0, MAJOR 대부분, MINOR 대부분)
- **남은 이슈**: 22건 (MAJOR 1 + MINOR 21) → Won't Fix 후 **1건** (S1820)
- **목표**: A등급 유지, Quality Gate GREEN 유지, BLOCKER/CRITICAL 0개
- **참고**: Coverity CID 643610 수정 완료 (b3a410a)
