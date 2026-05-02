# TODO: Code Quality & Security Improvements

## 현재 상태 (2026-05-02, 마킹 처리 완료)

### 📊 SonarCloud 현황

| 심각도 | Phase 14 후 | 재분석 직후 | 마킹 처리 후 |
|--------|:---:|:---:|:---:|
| BLOCKER | 0 | 1 | 0 |
| CRITICAL | 0 | 8 | 0 |
| MAJOR | 1 | 3 | **1** (S1820) |
| MINOR | 21 | 21 | 0 |
| **합계** | **22** | **33** | **1** |

| Hotspots | 재분석 직후 | 마킹 처리 후 |
|---|:---:|:---:|
| TO_REVIEW | 48 | **0** |

**Quality Gate**: GREEN (`alert_status = OK`)
**Ratings**: Reliability **A(1.0)**, Security **A(1.0)**, Maintainability **A(1.0)**

### 처리 이력
- 2026-05-02: GitLab 메인 전환으로 SonarCloud 프로젝트 새 ID(`AZzxDhZL...`)로 재분석.
  Won't Fix / SAFE 마킹 전부 리셋. 코드 변경 없이 카운트만 33+48 증가.
- 2026-05-02: 일괄 마킹 스크립트로 32 issues + 47 hotspots 처리. 정책은 CLAUDE.md "Marking Issues / Hotspots" 표 참조.
- 2026-05-02: S5332 HTTP 1건은 SAFE + acknowledgement 코멘트로 처리 (외부 URL 통제 불가, API가 ACKNOWLEDGED 미지원).

---

## 남은 작업 — 실제 코드 변경 필요

### 요약 — 미완료 마이그레이션 / 정리 항목

| ID | 위치 | 패턴 | 우선순위 |
|---|---|---|---|
| R1 | main.h `lyrics_state` Wayland 필드 7개 | `wl_conn` 도입 후 옛 필드 미제거 (e0020f0) | MEDIUM |
| R2 | main.h `lyrics_state` 42필드 | S1820 — 서브 구조체 분리 | LOW |
| R3 | itunes_artwork.c, lrclib_provider.c | `json_utils`/`curl_utils` 추출 후 thin wrapper 잔존 | **HIGH (zero-risk)** |
| R4 | config.c, lyrics_provider.c | `string_utils.h` 추출 후 `config_trim_whitespace` thin wrapper 잔존 | **HIGH (zero-risk)** |
| R5 | config.c `[deepl]` section | Deprecated 섹션 (도입 aedbfac 2025-12-19) — 제거 데드라인 결정 | LOW (사용자 영향) |
| R6 | mpris.c:455 Seeked signal | 빈 콜백만 등록 (`f26dd47` 폴링 전환 후 잔존) | LOW |
| R7 | translator/{openai,claude,gemini,deepl}/ | `translate_single_line`/`parse_response_json`/`build_request_json` 4×3 중복 | MEDIUM |
| R8 | parser_utils.c:374 `find_word_start` | 152줄 함수 (UTF-8 word boundary) | LOW |
| R9 | mpris.c:931, wayland_init.c:12, lyrics_manager.c:180 | 100+줄 함수 3개 | LOW |
| R10 | config.c:1116, deepl_translator.c:192 | Magic number 버퍼 (`body[2048]`, `request_body[4096]`) → constants.h | **TINY** |
| R11 | config.c:357 | `XDG_CONFIG_HOME` 빈 문자열 가드 누락 (file_utils.c와 일관성) | **TINY** |

**TINY 묶음 (R3+R4+R6+R10+R11)**: 한 PR로 묶으면 ~40분 + zero-risk. 우선 처리 권장.

---

### R1. 선행: Wayland 필드 → `wl_conn` 통합 (미완료 마이그레이션 완료)

**배경**: 커밋 `e0020f0` (2025-11-25, "feat: Add full Wayland reconnection with surface reinitialization") 에서 `struct wayland_connection`을 도입하고 `state->wl_conn` 필드를 추가했으나, **기존 `lyrics_state`의 Wayland 객체 7개 필드를 제거하지 않음**. 점진적 마이그레이션이 중단된 상태.

**현재 중복** (main.h `struct lyrics_state`):

| state 직접 필드 | wl_conn 안에도 존재 | 참조 빈도 |
|---|---|:---:|
| `display` | `wl_conn->display` | 9 |
| `registry` | `wl_conn->registry` | 4 |
| `compositor` | `wl_conn->compositor` | 5 |
| `shm` | `wl_conn->shm` | 5 |
| `layer_shell` | `wl_conn->layer_shell` | 4 |
| `surface` | `wl_conn->surface` | 22 |
| `layer_surface` | `wl_conn->layer_surface` | 12 |

**작업 범위**:
- 7개 필드를 `state`에서 제거
- 호출부 약 61건을 `state->wl_conn->X`로 일괄 치환
- 8파일 영향 (main.c, lyrics_manager.c, rendering_manager.c, wayland_events.c, wayland_init.c, file_monitor.c, dbus_control.c, system_tray.c)

**효과**: 중복 제거 + S1820 카운트 42 → 35필드 (단독으로는 해소 못 하지만 R2의 선행 정리)

**우선순위**: MEDIUM — 단독으로도 가치 있음 (메모리 일관성, 가독성)

---

### R2. 본 작업: S1820 — `lyrics_state` 구조체 분리

**위치**: `src/main.h:53` `struct lyrics_state` 42필드 (R1 후 35필드) → 권장 20필드

**파급 범위 (실측 2026-05-02)**:
- `state->` 총 참조: **304건**
- 영향 파일: **8개**
- `lyrics_state *` 받는 함수 시그니처: **57개** (헤더 16, 구현 41)
- `&state->X` 포인터 전달: 27건

**필드 사용 빈도 상위**:
```
45  state->lyrics              29  state->current_line
22  state->surface             22  state->current_track
20  state->timing_offset_ms    14  state->overlay_enabled
13  state->width               12  state->layer_surface
12  state->height              10  state->prev_line / next_line
```

**자연스러운 분리안**:
```c
struct lyrics_state {
    struct wayland_connection *wl_conn;   // R1에서 통합 정리됨
    struct surface_state   surf;          // width/height/buffers/anchor/margin/layer  (10)
    struct appearance      look;          // foreground/background/font                (3)
    struct playback_state  pb;            // lyrics/current_track/lines/offset         (11)
    struct config_state    cfg;           // config_file_path/md5                      (2)
    struct runtime_flags   flags;         // run/reconnect/overlay_enabled/quirks      (5)
};
```
→ `lyrics_state` 자체 **6필드** (S1820 해소)

**작업량 추정**:
| 접근 | 시간 | diff |
|---|:---:|---|
| 단순 sed 치환만 (시그니처 `lyrics_state*` 유지) | 1–2h | +400 / −400 |
| 시그니처 분해 (작은 함수는 sub-struct만 받음) | 4–8h | +600 / −500 |

**검증 필수**:
- meson 빌드 (werror=true)
- fuzz 타겟 (lrc/lrcx/srt) 재실행
- 실제 음악 재생 + Wayland 재연결 (TTY 전환) 수동 테스트

**우선순위**: LOW — code smell이고 Quality Gate에 영향 없음. 단일 컨텍스트 구조체 패턴 자체는 유효하므로 Won't Fix도 합리적 선택지.

---

### R3. JSON / URL 헬퍼 thin wrapper 정리

**위치**:
- `src/provider/itunes/itunes_artwork.c:13-20` (`extract_json_string`, `url_encode`)
- `src/provider/lrclib/lrclib_provider.c:15-28` (`url_encode`, `extract_json_string`, `extract_json_int`)

**상태**: 5개 함수 모두 새 공통 함수(`json_extract_string`, `json_extract_int_from`, `curl_url_encode`)를 한 번 호출만 하는 thin wrapper. `// Deprecated:` 주석 명시.

**호출부**:
- itunes_artwork.c: 4건 (line 61, 73, 77, 131)
- lrclib_provider.c: 11건 (line 42, 60, 122, 123, 163, 164, 165, 308, 309, 310, 380, 381)

**작업**:
1. 각 호출부를 직접 새 함수로 일괄 치환 (sed 또는 명시적 Edit)
2. wrapper 함수 5개 삭제
3. meson 빌드 + 실행 확인

**우선순위**: HIGH — zero-risk, ~10분.

---

### R4. `config_trim_whitespace` thin wrapper 정리

**위치**: `src/user_experience/config/config.c:272-276`, 헤더에 노출 (`config.h:122`)

**상태**: `string_utils.h`의 `trim_whitespace()`를 한 번 호출만 하는 wrapper. `// Deprecated:` 주석 명시.

**호출부**: 15건
- `config.c`: 13건 (line 478, 500, 501, 507, 548, 584, 606, 656, 678, 831, 851)
- `provider/lyrics/lyrics_provider.c`: 3건 (line 252, 402, 446)

**작업**:
1. 15건 호출부를 `trim_whitespace`로 일괄 치환
2. `config.c`의 wrapper 함수 + `config.h`의 선언 삭제
3. `lyrics_provider.c`에 `string_utils.h` include 추가 확인
4. meson 빌드

**우선순위**: HIGH — zero-risk, ~10분.

---

### R5. `[deepl]` deprecated config section 제거 데드라인 결정

**위치**: `src/user_experience/config/config.c:217-269` (`parse_deprecated_deepl_section`), 882-908 (warning suppression)

**상태**: 도입 `aedbfac` (2025-12-19, ~5개월 경과). 사용자에게 `⚠️ [deepl] section is deprecated` 경고 출력 후 호환 처리.

**작업**:
1. 다음 릴리스 노트에 "Removal scheduled in v0.X.0" 명시
2. 메이저 릴리스에서 코드 + warning suppression 제거
3. `[translation]` section만 인식하도록 단순화

**우선순위**: LOW — 사용자 영향. 한 메이저 릴리스 사이클 grace period 권장.

---

### R6. MPRIS Seeked signal 빈 콜백 정리

**위치**: `src/utils/mpris/mpris.c:445-456`

**상태**: 콜백 함수가 `G_GNUC_UNUSED` 파라미터만 받고 본문이 비어있음. 코멘트:
> "Position is queried directly in mpris_get_position(), so we don't need to cache it here.
> Seeked signal subscription is kept for potential future optimizations"

도입 `f26dd47` (perf: hybrid signal-polling)에서 폴링으로 충분하다 판단되어 콜백 비웠으나 구독 자체는 유지.

**작업** (둘 중 하나 결정):
- (a) 구독 자체 제거 (signal handler 등록 + 콜백 함수 삭제)
- (b) 향후 최적화 의도가 명확하면 유지하되 코멘트에 사용 시나리오 명시

**우선순위**: LOW — ~5분.

---

### R7. Translator provider별 함수 중복 → vtable 추상화

**위치**: 4개 translator 모듈에 같은 시그니처가 반복

| 함수 | openai | claude | gemini | deepl |
|---|:---:|:---:|:---:|:---:|
| `translate_single_line` | ✅ L89 | ✅ L100 | ✅ L84 | ✅ L174 |
| `parse_response_json` | ✅ L57 | ✅ L60 | ✅ L56 | — (form 응답) |
| `build_request_json` | ✅ L27 | ✅ L29 | ✅ L26 | — (query string) |

**상태**: 함수 이름과 시그니처는 동일하지만 내부 구현이 provider별로 다름 (다른 API 엔드포인트, 다른 JSON 스키마). DeepL은 form-encoded라 일부 함수만 공유.

**작업안**:
1. `translator_common.h`에 vtable 정의:
   ```c
   struct translator_ops {
       const char *name;
       char* (*build_request)(const char *text, const char *target_lang, const char *model);
       char* (*parse_response)(const char *json_str);
       char* (*translate_line)(const char *text, const char *target_lang, ...);
   };
   ```
2. 각 provider가 `static const struct translator_ops openai_ops = { ... };` 등록
3. dispatcher에서 `provider->translate_line()` 호출

**효과**:
- 새 provider 추가 시 한 파일에 ops 구조체만 정의
- common 코드(retry, rate limit, language detect)는 vtable 위에서 일원화
- 단, deepl 같은 예외 케이스(form-encoded) 처리 여지 필요

**우선순위**: MEDIUM — 새 provider 추가 빈도와 중복 비용에 따라. 단순 중복이 아니라 진짜 다른 로직이라 가치 vs 비용 측정 필요.

---

### R8. `find_word_start` 152줄 분리

**위치**: `src/parser/utils/parser_utils.c:374`

**상태**: UTF-8 word boundary 탐색 로직. SonarCloud cognitive complexity 통과 (이미 Phase 9에서 정리된 패턴이라 추정). 기능은 정확하나 사람이 읽기 부담.

**작업안**:
- UTF-8 backwards iter 부분 분리 (이미 `move_back_one_utf8_char` 헬퍼 존재)
- space-boundary 분기 / kanji-boundary 분기를 별도 정적 함수로
- bounds check 부분은 그대로 유지 (NOSONAR 주석 보존)

**우선순위**: LOW — 동작 정확성 OK, 가독성 개선.

---

### R9. 100+줄 함수 분리

**위치 / 길이**:
- `src/utils/mpris/mpris.c:931` `setup_player_subscription` — 126줄
- `src/utils/wayland/wayland_init.c:12` `wayland_init_surface` — 115줄
- `src/lyrics/lyrics_manager.c:180` `lyrics_manager_load_lyrics` — 101줄

**작업안**: 각 함수의 단계별 블록을 헬퍼로 추출. 외부 시그니처 유지.

**우선순위**: LOW — 리팩토링 ROI 작음. 현재 SonarCloud 룰 통과 중.

---

### R10. Magic number 버퍼 → constants.h

**위치**:
- `src/user_experience/config/config.c:1116` `char body[2048];`
- `src/translator/deepl/deepl_translator.c:192` `char request_body[4096];`

**작업안**: `constants.h`에 `HTTP_REQUEST_BODY_SIZE` 등 추가, 두 곳 치환.

**우선순위**: TINY — 5분, R3/R4 묶음 PR에 끼워 넣기 좋음.

---

### R11. `XDG_CONFIG_HOME` 빈 문자열 가드

**위치**: `src/user_experience/config/config.c:357`

**현재**:
```c
if (config_home) {                       // NULL만 가드
    result = build_config_path(path, CONFIG_PATH_SIZE, config_home);
}
```

**문제**: `XDG_CONFIG_HOME=""` (빈 문자열)이 들어오면 `build_config_path`가 빈 prefix로 부적절한 경로 생성 가능. 비표준 설정이라 실 위험은 낮지만 동일 모듈(`file_utils.c:65`)은 `xdg_cache && xdg_cache[0] != '\0'` 패턴이라 일관성 결여.

**작업**:
```c
if (config_home && config_home[0] != '\0') {
    ...
}
```

**우선순위**: TINY — 1줄 수정, R3/R4/R10 묶음 PR에 끼워 넣기.

---

## 권장 작업 순서

1. ~~SonarCloud 웹 마킹 작업~~ ✅ 완료 (2026-05-02)
2. ~~HTTP S5332 검토~~ ✅ 완료 (SAFE + acknowledgement)
3. **TINY 묶음 PR** (R3 + R4 + R6 + R10 + R11, zero-risk, ~40분)
4. **R1**: Wayland 필드 → `wl_conn` 통합 (단독 PR)
5. **R7**: Translator vtable 추상화 (별도 PR, 가치 평가 후)
6. **R8 + R9**: 긴 함수 분리 (선택, ROI 평가 후)
7. **R2**: S1820 `lyrics_state` 분리 (단독 PR, 또는 Won't Fix 처리)
8. **R5**: 다음 메이저 릴리스 사이클에 정책 결정

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
- **마킹 정책**: `CLAUDE.md` → "Marking Issues / Hotspots (Write API)" 섹션

---

## 상태

- **최종 업데이트**: 2026-05-02 (재분석 + 마킹 처리 + R1~R11 항목 추가)
- **현재 단계**: TINY 묶음 PR (R3+R4+R6+R10+R11) 대기 중
- **완료**: Phase 1-14 (코드 변경) + 2026-05-02 마킹 처리
- **남은 이슈**: SonarCloud 1건 (S1820 — R2) + 코드베이스 정리 11건 (R1~R11)
- **목표**: A등급 유지, Quality Gate GREEN 유지, BLOCKER/CRITICAL 0개
- **참고**: Coverity CID 643610 수정 완료 (b3a410a). SEC-A/SEC-B 검증 완료 — 모두 안전 (NULL+empty 가드 충분)
