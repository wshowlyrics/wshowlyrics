# TODO: Code Quality & Security Improvements

## 현재 상태 (2026-05-02, GitLab 마이그레이션 후 재분석)

### ⚠️ 마이그레이션 영향

GitHub→GitLab 메인 전환 후 SonarCloud 프로젝트가 새 ID(`AZzxDhZL...`)로 재분석됨.
**이전에 적용한 Won't Fix / Safe 마킹이 전부 리셋**됨. 코드 변경 없이 이슈 카운트만 증가.

부분 스캔 아님 검증: `ncloc 11,807` / `files 79` / `functions 492` — 전체 재분석 완료.

### 📊 SonarCloud 현황 (실측 2026-05-02)

| 심각도 | Phase 14 후 | 현재 (재분석) | 변동 |
|--------|:---:|:---:|:---:|
| BLOCKER | 0 | 1 | +1 (마킹 리셋) |
| CRITICAL | 0 | 8 | +8 (마킹 리셋) |
| MAJOR | 1 | 3 | +2 (마킹 리셋 2) |
| MINOR | 21 | 21 | 0 |
| **합계** | **22** | **33** | **+11** |

**Quality Gate**: GREEN (`alert_status = OK`)
**Ratings**: Reliability E(5.0), Security D(4.0), Maintainability A(1.0)
※ Reliability/Security 등급은 미마킹 false positive 때문 — 실 코드 품질은 변동 없음.

**Security Hotspots**: 48건 TO_REVIEW (전부 마킹 리셋)
- HIGH 37건: 모두 S5813 `strlen` 안전성 (NULL-terminated 보장 확인 후 Safe)
- MEDIUM 8건: S5849 권한 설정 (`g_spawn_async`, capabilities)
- LOW 3건: S5332 HTTP×1, S4790 weak hash×2 (MD5 — 캐시 식별용 비암호 사용)

---

## 남은 이슈

### 🟢 코드 변경 불필요 — SonarCloud 웹에서 재마킹만 하면 되는 것

#### BLOCKER (1건 — false positive)

| 파일 | Rule | 비고 |
|------|------|------|
| parser_utils.c:335 | S3519 | UTF-8 backwards iter — 이미 `NOSONAR` 주석 + 방어적 bounds check 적용. False positive 재마킹 필요. |

#### CRITICAL (8건 — 전부 false positive, S4423 TLS)

`CURLOPT_SSLVERSION = CURL_SSLVERSION_TLSv1_2` 설정 코드를 SonarCloud가 weak protocol로 오탐.
TLS 1.2는 약하지 않으며 명시적으로 강제하는 보안 코드.

| 파일 | Line |
|------|:---:|
| main.c | 58 |
| translator/common/translator_common.c | 463, 899 |
| translator/deepl/deepl_translator.c | 103 |
| provider/itunes/itunes_artwork.c | 105 |
| provider/lrclib/lrclib_provider.c | 204, 301 |
| user_experience/system_tray/system_tray.c | 62 |

→ 전부 Won't Fix(False Positive) 마킹.

#### MAJOR (2건 — 외부 콜백 시그니처)

| 파일 | Rule | 비고 |
|------|------|------|
| events/wayland_events.c:115 | S107 | Wayland 콜백 시그니처 (수정 불가) |
| utils/dbus_control/dbus_control.c:66 | S107 | GDBus 콜백 시그니처 (수정 불가) |

→ Won't Fix 마킹 (이전 정책과 동일).

#### MINOR (21건 — 외부 콜백 시그니처 S995)

이전 분석과 동일. 파라미터 수정 불가.

| 파일 | 건수 | 사유 |
|------|:---:|------|
| events/wayland_events.c | 10 | Wayland `wl_*` 콜백 |
| user_experience/system_tray/system_tray.c | 6 | GTK 콜백 |
| utils/dbus_control/dbus_control.c | 3 | GDBus 콜백 |
| utils/shm/shm.c | 1 | `wl_buffer` 콜백 |
| utils/wayland/wayland_manager.c | 1 | `wl_registry` 콜백 |

→ 전부 Won't Fix 마킹.

#### Security Hotspots (48건 — 재검토 후 Safe 마킹)

이전 정책과 동일 처리:
- HIGH S5813 (strlen ×37): NULL-terminated 보장된 입력만 사용 → Safe
- MEDIUM S5849 (×8): `g_spawn_async` / 권한 설정 — Phase S에서 검증 완료 → Safe
- LOW S5332 (HTTP ×1): system_tray.c:172 iTunes API URL — 검토 필요 (https로 변경 가능?)
- LOW S4790 (MD5 ×2): file_utils.c — 캐시 식별 용도 비암호 사용 → Safe

---

### 🔴 실제 코드 변경 필요 — MAJOR 1건

| 파일 | Rule | 문제 | 우선순위 |
|------|------|------|:---:|
| main.h:53 | S1820 | `lyrics_state` 구조체 42필드 (최대 20) | LOW |

서브 구조체로 분리 (예: `wayland_objs`, `surface_state`, `playback_state`, `translation_ctx` 등).
대규모 리팩토링이라 단독 PR로 분리 권장.

---

## 권장 작업 순서

1. **SonarCloud 웹 마킹 작업** (코드 변경 0건, 즉시 처리 가능)
   - BLOCKER 1건 → False Positive
   - CRITICAL 8건 → False Positive
   - MAJOR 2건 + MINOR 21건 → Won't Fix
   - Security Hotspot 47건 → Safe (HTTP S5332 1건 제외)
   - 처리 후 예상: 33건 → **1건** (S1820만 남음)
2. **HTTP S5332 검토** — system_tray.c:172 iTunes API를 https로 변경 가능한지 확인
3. **S1820** — `lyrics_state` 구조체 분리 (별도 리팩토링 PR)

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

## 참고 자료

- **SonarCloud Dashboard**: https://sonarcloud.io/project/overview?id=wshowlyrics_wshowlyrics
- **SonarCloud Issues API**: https://sonarcloud.io/api/issues/search?componentKeys=wshowlyrics_wshowlyrics
- **SonarCloud Hotspots API**: https://sonarcloud.io/api/hotspots/search?projectKey=wshowlyrics_wshowlyrics
- **Coverity Dashboard**: https://scan.coverity.com/projects/wshowlyrics

---

## 상태

- **최종 업데이트**: 2026-05-02 (GitLab 마이그레이션 후 재분석 반영)
- **현재 Phase**: SonarCloud 웹 재마킹 → 이후 S1820 구조체 분리
- **완료**: Phase 1-14 (코드 변경 작업 모두 완료)
- **남은 이슈**: 33건 (BLOCKER 1 + CRITICAL 8 + MAJOR 3 + MINOR 21) + Hotspot 48건
  - 마킹 처리 후 실질 잔여: **1건** (S1820 main.h)
- **목표**: A등급 유지, Quality Gate GREEN 유지, BLOCKER/CRITICAL 0개
- **참고**: Coverity CID 643610 수정 완료 (b3a410a)
