# TODO: Code Quality & Security Improvements

## 현재 상태 (2026-02-24, 스캔 반영 후)

### 완료된 Phase
- **Phase 1-8**: Bugs 0개, Vulnerabilities 0개, BLOCKER 0개
- **Phase S**: 보안취약점 3건 + 동시성 버그 수정 (d51ec9e)
- **Phase H**: HIGH 5건 수정 (caa80a4)
- **Phase 9**: Cognitive Complexity CRITICAL 2건 수정 (ba85ad3)
- **Phase 10-pre**: MAJOR 8건 수정 (23ee4fc) + COPR 빌드 수정 (4d014d0)
- **Phase 10-A**: lrclib_provider.c 회귀 BLOCKER+CRITICAL 3건 수정 (8196b99)
- **Phase 10-B**: CRITICAL 4건 수정 (5f4470d) — S134 x2, S859 x3
- **Phase 10-C**: MAJOR 23건 수정 (3a62cf3, 2de305e, 4356a66)

### 📊 SonarCloud 현황 (실측)

| 심각도 | 이전 (2/23) | 현재 (2/24) | 변동 |
|--------|:---:|:---:|:---:|
| BLOCKER | 1 | **0** | -1 |
| CRITICAL | 7 | **2** | -5 (신규 2건) |
| MAJOR | 36 | **14** | -22 |
| MINOR | 132 | **137** | +5 |
| **합계** | **176** | **153** | **-23** |

### Security Hotspot (1건, TO_REVIEW)

| 파일 | 라인 | Rule | 문제 | 확률 |
|------|------|------|------|------|
| parser_utils.c | 629 | S5813 | `strlen` 사용 안전성 | HIGH |

> `finalize_ruby_segments()`에서 text는 호출자에 의해 NULL-terminated 보장.
> SonarCloud 웹에서 수동으로 "Safe" 마킹 필요.

---

## 신규 CRITICAL 이슈 (2건) — 리팩토링 회귀

우리 리팩토링으로 인해 mpris.c에서 새로 감지된 중첩 깊이 이슈:

| 파일 | 라인 | Rule | 문제 |
|------|------|------|------|
| mpris.c | 231 | S134 | `list_available_players()` 중첩 깊이 > 3 |
| mpris.c | 653 | S134 | preferred player 루프 중첩 깊이 > 3 |

> mpris.c:231은 이전 10-B1에서 수정했으나 라인이 이동하여 다시 감지된 것일 수 있음.
> mpris.c:653은 S924 수정(4356a66)에서 for+done 패턴 도입 시 발생한 신규 이슈.

---

## 남은 MAJOR 이슈 (14건)

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

### Priority 2: 구조체 필드 과다 (1건)
**Rule**: c:S1820

| 파일 | 라인 | 문제 |
|------|------|------|
| main.h | 51 | 구조체 필드 42개 (최대 20) |

### Priority 3: 중첩 break (1건)
**Rule**: c:S924

| 파일 | 라인 | 문제 |
|------|------|------|
| lrclib_provider.c | 113 | 중첩 break 3→1 필요 |

> dbus_control.c:66은 GDBus 콜백 시그니처라 변경 불가 (Won't Fix 가능).
> lrclib_provider.c:113은 이전 수정(8196b99)의 early-continue 패턴에서 잔존.

---

## Phase 11: MINOR 이슈 (137건)

| Rule | 설명 | 예상 건수 |
|------|------|:---:|
| c:S995 | Pointer-to-const 파라미터 | ~47 |
| c:S1659 | 다중 선언 분리 | ~39 |
| c:S5350 | Pointer-to-const 변수 | ~36 |
| c:S1905 | 불필요한 cast 제거 | ~9 |
| c:S886 | 루프 리팩토링 | ~1 |
| 기타 | 신규 감지 | ~5 |
| **합계** | | **137** |

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

1. **CRITICAL 2건** — mpris.c:231, 653 중첩 깊이 수정
2. **MAJOR 잔여** — lrclib_provider.c S924 (1건) + S107 구조체 리팩토링 (12건) + S1820 (1건)
3. **Security Hotspot** — parser_utils.c S5813 수동 리뷰
4. **Phase 11** — MINOR 137건
5. **MEDIUM** — 코드 분석 발견 이슈 9건

---

## 참고 자료

- **SonarCloud Dashboard**: https://sonarcloud.io/project/overview?id=unstable-code_lyrics
- **SonarCloud Issues API**: https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics
- **Coverity Dashboard**: https://scan.coverity.com/projects/unstable-code-lyrics

---

## 상태

- **최종 업데이트**: 2026-02-24 (SonarCloud 스캔 반영 후)
- **현재 Phase**: CRITICAL 2건 → MAJOR 잔여 → Phase 11
- **완료**: Phase 1-10 (BLOCKER 0, MAJOR 대부분)
- **남은 이슈**: 153건 (CRITICAL 2 + MAJOR 14 + MINOR 137)
- **목표**: A등급 유지, Quality Gate GREEN 유지, BLOCKER/CRITICAL 0개
- **참고**: Coverity CID 643610 수정 완료 (b3a410a)
