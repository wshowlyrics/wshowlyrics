# TODO: Code Quality & Security Improvements

## 현재 상태 (2026-02-24, 3차 업데이트)

### 완료된 Phase
- **Phase 1-8**: Bugs 0개, Vulnerabilities 0개, BLOCKER 0개
- **Phase S**: 보안취약점 3건 + 동시성 버그 수정 (d51ec9e)
- **Phase H**: HIGH 5건 수정 (caa80a4)
- **Phase 9**: Cognitive Complexity CRITICAL 2건 수정 (ba85ad3)
- **Phase 10-pre**: MAJOR 8건 수정 (23ee4fc) + COPR 빌드 수정 (4d014d0)
- **Phase 10-A**: lrclib_provider.c 회귀 BLOCKER+CRITICAL 3건 수정 (8196b99)
- **Phase 10-B**: 신규 CRITICAL 4건 수정 (5f4470d) — S134 x2, S859 x3
- **Phase 10-C 일부**: MAJOR 23건 수정 (3a62cf3, 2de305e, 4356a66)

### 📊 SonarCloud 현황 (푸시 전 예상)

| 심각도 | 이전 (2/23) | 수정 | 예상 잔여 |
|--------|:---:|:---:|:---:|
| BLOCKER | 1 | -1 | **0** |
| CRITICAL | 7 | -7 | **0** |
| MAJOR | 36 | -23 | **13** |
| MINOR | 132 | 0 | **132** |
| **합계** | **176** | **-31** | **~145** |

> 실제 수치는 푸시 후 SonarCloud 스캔 결과로 확인 필요

---

## 미푸시 커밋 (5건)

| 커밋 | 설명 | 수정 건수 |
|------|------|:---:|
| 8196b99 | lrclib_provider.c 회귀 수정 (S912, S859, S134) | BLOCKER 1 + CRITICAL 2 |
| 5f4470d | CRITICAL 4건 (mpris S134, config S134, system_tray S859 x3) | CRITICAL 4 |
| 3a62cf3 | if 병합 8건 (S1066) + 미사용 파라미터 8건 (S1172) | MAJOR 16 |
| 2de305e | 주석 수정 (S125 x2) + utime 교체 (S1911) + include 위치 (S954) | MAJOR 4 |
| 4356a66 | 중첩 break 감소 (S924 x3) | MAJOR 3 |

---

## 남은 MAJOR 이슈 (예상 13건)

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

> S107 12건과 S1820 1건은 모두 구조체 리팩토링이 필요하며 API 변경이 광범위함.
> rendering_manager, word_render, ruby_render는 렌더링 파라미터 구조체로 묶을 수 있음.
> dbus_control.c:66은 GDBus 콜백 시그니처라 변경 불가 (제외 가능).

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

1. **푸시** — 미푸시 커밋 5건 반영 후 SonarCloud 스캔 결과 확인
2. **Phase 10 잔여** — S107 (12건) + S1820 (1건) 구조체 리팩토링
3. **Phase 11** — MINOR 132건
4. **MEDIUM** — 코드 분석 발견 이슈 9건

---

## 참고 자료

- **SonarCloud Dashboard**: https://sonarcloud.io/project/overview?id=unstable-code_lyrics
- **SonarCloud Issues API**: https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics
- **Coverity Dashboard**: https://scan.coverity.com/projects/unstable-code-lyrics

---

## 상태

- **최종 업데이트**: 2026-02-24 (Phase 10-A/B/C 일부 완료)
- **현재 Phase**: 푸시 대기 → Phase 10 잔여 (구조체) → Phase 11
- **완료**: Phase 1-10 (BLOCKER/CRITICAL 전부, MAJOR 대부분)
- **남은 이슈**: 예상 ~145건 (MAJOR 13 + MINOR 132)
- **목표**: A등급 유지, Quality Gate GREEN 유지, BLOCKER/CRITICAL 0개
- **참고**: Coverity CID 643610 수정 완료 (b3a410a)
