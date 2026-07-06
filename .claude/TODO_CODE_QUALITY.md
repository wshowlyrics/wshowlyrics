# TODO: Code Quality & Security Improvements

## 현재 상태 (2026-07-06, 전체 코드 재감사 완료)

### 📊 SonarCloud 현황

| 심각도 | 마킹 처리 후 (05-02) | 현재 (07-06) |
|--------|:---:|:---:|
| BLOCKER | 0 | 0 |
| CRITICAL | 0 | 0 |
| MAJOR | 1 (S1820) | **4** (S1121 ×4, 신규 유입) |
| MINOR | 0 | **1** (S995) |
| **합계** | **1** | **5** |

| Hotspots TO_REVIEW | 0 |
|---|:---:|

**Quality Gate**: GREEN (`alert_status = OK`) — bugs 0, vulnerabilities 0
**Ratings**: Reliability **A(1.0)**, Security **A(1.0)**, Maintainability **A(1.0)**
**참고**: 기존 잔여 MAJOR였던 S1820(lyrics_state 42필드)은 R1+R2-A 구조체 분리로 해소됨. 현재 5건은 전부 2026-05-03 이후 커밋에서 유입된 신규 이슈 (→ R13).

### 처리 이력
- 2026-05-02: GitLab 메인 전환으로 SonarCloud 프로젝트 새 ID(`AZzxDhZL...`)로 재분석.
  Won't Fix / SAFE 마킹 전부 리셋. 코드 변경 없이 카운트만 33+48 증가.
- 2026-05-02: 일괄 마킹 스크립트로 32 issues + 47 hotspots 처리. 정책은 CLAUDE.md "Marking Issues / Hotspots" 표 참조.
- 2026-05-02: S5332 HTTP 1건은 SAFE + acknowledgement 코멘트로 처리 (외부 URL 통제 불가, API가 ACKNOWLEDGED 미지원).
- 2026-07-06: 43a6366 이후 변경분(15커밋) 중심 리팩토링 + 보안 재감사. 보안 MEDIUM 2건(SEC-1, SEC-2) 및 신규 리팩토링 5건(R13–R17) 발견. `wshowlyrics-offset` / dbus_control / translator 캐시 / lock_file / runtime_dir / 파서는 클린 판정.

---

## 남은 작업 — 실제 코드 변경 필요

### 🔒 보안 — 2026-07-06 감사 발견 (신규)

| ID | 심각도 | 위치 | 문제 | 상태 |
|---|---|---|---|---|
| **SEC-1** | **MEDIUM** | mpris.c:127,141,148,155,162,819,963 | 악성 MPRIS 메타데이터로 NULL deref DoS (GVariant 타입 미검증) | **진행 예정 — 최우선** |
| **SEC-2** | **MEDIUM** | system_tray.c:54-89,154-215 | artUrl 경유 SSRF + `file://` 임의 파일 디코드 | 진행 예정 |
| **SEC-3** | LOW | config.c:737-793 | symlink 폴백이 resolved-path 검증 우회 + prefix 경계 미검사 | 하드닝 (ea54306 리뷰) |
| **SEC-4** | LOW | lyrics_provider.c:163-189 | `%00` 디코딩 시 embedded NUL 허용 (경로 절단) | 하드닝 |

### 요약 — 미완료 마이그레이션 / 정리 항목

| ID | 위치 | 패턴 | 우선순위 / 상태 |
|---|---|---|---|
| ~~R1~~ | main.h `lyrics_state` Wayland 필드 7개 | `wl_conn` 도입 후 옛 필드 미제거 (e0020f0) | ✅ 완료 (9007eec, 42→35 필드) |
| ~~R2~~ | main.h `lyrics_state` 35필드 | S1820 — 5개 sub-struct 분리 (style/surface/playback/config/runtime) | ✅ 완료 (6b55f10, 35→6 필드). B(시그니처 분해)는 후속 |
| ~~R3~~ | itunes_artwork.c, lrclib_provider.c | thin wrapper 정리 | ✅ 완료 (c15cdb9) |
| ~~R4~~ | config.c, lyrics_provider.c | `config_trim_whitespace` 정리 | ✅ 완료 (c15cdb9) |
| **R5** | config.c `[deepl]` section | Deprecated 섹션 — 제거 데드라인 결정 | ⏸ 보류 (정책) — 07-06 확인: 코드 위치 config.c:211-215, 893-908로 이동만, 변동 없음 |
| ~~R6~~ | mpris.c Seeked signal | 빈 콜백 정리 | ✅ 완료 (c15cdb9) |
| **R7** | lyrics_provider.c:29-68 dispatcher | 4-branch if/else → provider 테이블 | **MEDIUM — 진행 예정** (07-06 재확인: 여전히 4-branch, 변동 없음) |
| **R8** | parser_utils.c:374 `find_word_start` | 152줄 함수 (07-06 재측정: 정확히 152줄 유지) | ⏸ 보류 (가독성만 목적, 강제 아님) |
| **R9** | mpris.c:921, wayland_init.c:13, lyrics_manager.c:179 | 100+줄 함수 3개 (07-06 재측정: 108/115/101줄 — `setup_player_subscription`은 126→108줄로 축소) | ⏸ 보류 (강제 아님) |
| ~~R10~~ | config.c, deepl_translator.c | Magic number 버퍼 | ✅ 완료 (c15cdb9) |
| ~~R11~~ | config.c | `XDG_CONFIG_HOME` 빈 문자열 가드 + 잠재 NULL deref | ✅ 완료 (c15cdb9) |
| ~~R12-A~~ | 22 파일 | clang-tidy `misc-include-cleaner` 기준 unused #include 제거 (37 줄 순감) | ✅ 완료 (43a6366) |
| **R12-B** | main.h | main.h 슬림화 (~30 .c가 stdint.h 등 직접 include 필요) | ⏸ 보류 (B 옵션 — 가독성 트레이드오프) |
| **R13** | wayland_events.c:261, config.c:607/817/954, main.c:276 | SonarCloud 신규 5건 (S1121 ×4 체인 할당, S995 ×1 const 누락) | **HIGH — TINY 묶음, zero-risk** |
| **R14** | system_tray.c:896-909 | 아이콘 폴백 3-tier에서 `set_indicator_icon_cached(); return true;` 3중 복붙 | MEDIUM |
| **R15** | translator_common.c:105-106, 554-556 | `max_retries` 조회 + 기본값 `3` 중복 | LOW |
| **R16** | main.c:296-299 vs 332-333 | 캐시 경로 prefix 검사 중복 + `get_cache_base_dir()` NULL 시 `strlen(NULL)` 잠재 크래시 | LOW (NULL 가드 포함) |
| **R17** | main.c:309 | `char tr_path[768]` — R10에서 정리한 magic number 패턴 회귀 | TINY (R13 묶음에 포함) |

**TINY 묶음 (R3+R4+R6+R10+R11)**: ✅ 완료 (c15cdb9, 2026-05-02)
**TINY 묶음 2 (R13+R17)**: 미착수 — SonarCloud 5건 전부 해소 + magic number, ~15분, zero-risk

---

### SEC-1. MPRIS 메타데이터 GVariant 타입 미검증 → NULL deref DoS (MEDIUM)

**위치**: `src/utils/mpris/mpris.c`
- `parse_metadata_from_dict`: 127 (`xesam:title`), 141 (`xesam:album`), 148 (`xesam:url`), 155 (`mpris:trackid`), 162 (`mpris:artUrl`)
- `is_player_playing`: 819, `setup_player_subscription`: 963 (`PlaybackStatus`)

**문제**: `get_dict_value()`는 타입 무관하게 값을 돌려주는데, string이 아닌 GVariant에 `g_variant_get_string()`을 호출하면 NULL 반환 → `strdup(NULL)` → segfault. 세션 버스의 아무 프로세스나 `org.mpris.MediaPlayer2.evil`을 등록하고 `xesam:title`을 int32로 발행하면 오버레이 전체가 크래시 (CWE-476/843, 신뢰성 있는 로컬 DoS).

**대비**: 숫자 필드 `mpris:length`(169-174)와 artist 배열(`extract_string_array`)은 이미 타입 체크함. `PlaybackStatus` 핸들러 중 311/562는 `g_variant_lookup_value(..., G_VARIANT_TYPE_STRING)`이라 안전 — 819/963만 미검증.

**수정안**: `mpris:length` 패턴을 따라 `g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)` 가드 추가. 헬퍼 `dup_string_variant(GVariant*)` (타입 불일치 시 NULL) 하나로 5곳 중복 제거 가능.

**우선순위**: **HIGH-급 처리 권장** — 수정 작고 명확, 세션 버스 접근만으로 재현 가능한 DoS.

---

### SEC-2. 앨범아트 URL 경유 SSRF + `file://` 임의 파일 디코드 (MEDIUM)

**위치**: `src/user_experience/system_tray/system_tray.c:54-89` (`download_image`), `:154-215` (`load_image_from_url`)

**문제**: `art_url`은 rogue MPRIS 플레이어 / iTunes API 응답에서 오는데 필터 없이 흘러감:
1. `http(s)://` → `CURLOPT_FOLLOWLOCATION=1` + 호스트 allow-list 없음 → `http://169.254.169.254/...`, `http://localhost:PORT/...` 등 내부망 blind SSRF/포트 프로브 (CWE-918)
2. `file://` → `gdk_pixbuf_new_from_file(url + 7)` 직접 호출 → 유저가 읽을 수 있는 임의 파일을 디코더에 투입 (CWE-73). 이미지 디코드 실패 + ~정사각형 체크로 유출 채널은 아니고 약한 file oracle 수준
3. 두 경로 모두 공격자 바이트를 gdk-pixbuf 로더(CVE 다발 표면)에 노출

**수정안**:
- http(s): loopback/link-local/사설 대역 거부 또는 `CURLOPT_REDIR_PROTOCOLS` 제한, 다운로드 크기 상한
- `file://`: `realpath` 정규화 후 예상 디렉토리($XDG_CACHE_HOME, 음악 디렉토리)로 제한

**우선순위**: MEDIUM — 세션 버스 상의 악성 프로세스 전제, 출력 표면(48×48 트레이 아이콘)이 영향 제한.

---

### SEC-3. config symlink 폴백의 검증 우회 + prefix 경계 미검사 (LOW, 하드닝)

**위치**: `src/user_experience/config/config.c:737-793` (커밋 ea54306 "Accept symlinked config from secret-manager stores")

**문제**:
1. 새 폴백(787-788)이 `realpath()` 결과가 safe 목록 밖이어도, **unresolved 리터럴 경로**가 `/`로 시작하고 `..` 없고 문자열상 safe 디렉토리 안이면 통과시킴. 원래 이 함수가 막으려던 "심링크가 임의 위치를 가리키는" 케이스를 되열어줌.
2. `strncmp(resolved_path, home, strlen(home))` (740행)에 trailing-slash 경계가 없어 `HOME=/home/hm`이 `/home/hmEVIL/...`과도 매치.

**평가**: config 경로는 항상 내부에서 구성되고(`config_get_user_path()`), `~/.config`에 심링크를 심을 수 있는 공격자는 이미 config 자체를 덮어쓸 수 있으므로 새 권한 경계 침해는 아님. defense-in-depth 약화.

**수정안**: prefix 검사를 경계 인식으로 (다음 문자가 `/` 또는 문자열 끝). 심링크 케이스는 resolved 대상의 알려진 secret-store prefix(`/run/secrets`, `$XDG_RUNTIME_DIR/secrets.d`) allow-list 방식 권장. 최소한 `is_path_in_safe_location()` 근처에 이 폴백의 안전 근거 주석 추가.

---

### SEC-4. URL 디코딩 시 `%00` embedded NUL 허용 (LOW/INFO)

**위치**: `src/provider/lyrics/lyrics_provider.c:163-189` (`url_decode_string`)

**문제**: 버퍼 경계는 안전하나 `%00`이 NUL로 디코드되어 경로/파일명이 절단됨 (CWE-158). rogue 플레이어의 `file:///…/song%00.mp3` → 잘못된 디렉토리에서 가사 탐색. 메모리 안전 문제 아님, 기능적 영향만.

**수정안**: 디코드 결과가 `0x00`이면 리터럴 유지 또는 디코드 중단.

---

### 클린 판정 (2026-07-06 감사)

- **`wshowlyrics-offset`** (gdbus→busctl/dbus-send 폴백 +100줄): 인젝션 없음. 정수 정규식 검증, exact-match case, `busctl --`, 인용 처리 모두 적절.
- **`dbus_control.c`**: introspection XML로 타입 강제 → SEC-1 류 타입 혼동 불가. offset ±5000 클램프.
- **`translator_common.c` 캐시** (b891690 변경 포함): MAX_CACHE_FILE_SIZE 5MB 로드 전 검증, snprintf 절단 검사, umask 0077, json-c 인덱스 바운드 체크. 이상 없음.
- **`lock_file.c` / `runtime_dir.c`**: 0600/0700 권한, stale-lock fd 유지로 TOCTOU 회피. 클린.
- **`lrclib_provider.c`**: TLS 1.2 강제, json-c 파싱. `find_best_match_in_results`의 `strchr('{','}')` 오브젝트 분할은 가사 내 `}` 시 매칭 오류 가능 — 보안 아닌 정확성 이슈로 기록만.
- **`rendering_manager.c` HiDPI 스케일링** (d76a4c2): scale은 컴포지터 제공(1-3), 오버플로 비현실적. 클린.
- **파서들**: 이번 구간 diff 없음 + fuzz 커버리지 유지로 심층 재분석 생략.

---

### R1. 선행: Wayland 필드 → `wl_conn` 통합 (미완료 마이그레이션 완료)

> ✅ **완료 (2026-05-03, 9007eec)**. lyrics_state 필드 42 → 35.
> 검증: meson build clean, Sway/Hyprland 런타임 (가사/D-Bus/pause-resume/TTY 전환).
> handle_reconnection의 동기화 8줄 삭제 — listener 등록만 wl_conn 직접 참조.

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

> ✅ **완료 A 단계 (2026-05-03, 6b55f10)**. lyrics_state 35→6 필드.
> 5개 sub-struct: style / surface_state / playback_state / config_state / runtime.
> 배치: playback → lyrics_types.h, config → config.h, 나머지는 main.h.
> ⏸ **B 단계 (시그니처 분해)는 후속 PR**. 자연스러운 함수만 점진적으로 sub-struct만 받도록 변경.
> 검증: meson build clean, Sway/Hyprland 런타임 (가사/D-Bus/pause-resume/instrumental break/TTY 전환).

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

> **결정 (2026-05-03)**: 보류. 별도 릴리스 정책 결정 사이클에서 다룸.

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

### R7. Translator provider 등록 테이블 (dispatcher 정리)

> **결정 (2026-05-03)**: 진행 예정. Ollama 등 새로운 LLM provider 추가 대비.
> GitLab 이슈: https://gitlab.com/wshowlyrics/wshowlyrics/-/work_items/2
>
> **정정 (2026-05-03)**: 초기 추정은 "translate_single_line / parse / build 3개 함수가 4× 중복 → vtable 추상화"였으나 실제 코드 확인 결과 부정확. 현재 구조가 이미 함수 포인터 기반 (각 translator가 `translate_single_line`을 `translator_translate_lyrics_generic`에 전달). `parse/build`는 각 translator의 private implementation detail. 진짜 중복은 dispatcher의 4-branch string-prefix 매칭뿐.

**진짜 작업 위치**: `src/provider/lyrics/lyrics_provider.c::translate_lyrics_with_provider()`의 4-branch if/else.

**작업안**:
1. `translator_common.h`에 provider 테이블 정의:
   ```c
   struct translator_provider {
       const char *name;
       bool (*matches)(const char *provider_string);
       bool (*init)(void);
       void (*cleanup)(void);
       bool (*translate_lyrics)(struct lyrics_data *data, int64_t length_us);
   };
   ```
2. 각 translator가 `static const struct translator_provider X_provider = { ... };` 등록 (~10줄)
3. `translator_common.c`에 providers 배열 노출
4. dispatcher가 테이블 순회로 변경

**작업량 (정확)**:
- struct 정의: ~15줄
- 4 translator × ~10줄 = ~40줄
- dispatcher: −25 / +10
- **총 ~50–100줄 변경, 6 파일, 1–2시간**

**Ollama 추가 비용 (R7 후)**: ollama_translator.c 작성 + provider struct (~10줄) + 배열에 한 줄 추가. 끝.

**우선순위**: MEDIUM — 작은 변경이라 부담 적고 Ollama PR 비용 절감 효과 큼.

---

### R8. `find_word_start` 152줄 분리

> **결정 (2026-05-03)**: 보류. 강제 아님 (정적 분석 통과), 가독성 개선만 목적.
> 다음에 해당 함수를 손볼 일 생기면 그때 분리 검토.

**위치**: `src/parser/utils/parser_utils.c:374`

**상태**: UTF-8 word boundary 탐색 로직. SonarCloud cognitive complexity 통과 (이미 Phase 9에서 정리된 패턴이라 추정). 기능은 정확하나 사람이 읽기 부담.

**작업안**:
- UTF-8 backwards iter 부분 분리 (이미 `move_back_one_utf8_char` 헬퍼 존재)
- space-boundary 분기 / kanji-boundary 분기를 별도 정적 함수로
- bounds check 부분은 그대로 유지 (NOSONAR 주석 보존)

**우선순위**: LOW — 동작 정확성 OK, 가독성 개선.

---

### R9. 100+줄 함수 분리

> **결정 (2026-05-03)**: 보류. R8과 같은 사유 (강제 아님, ROI 작음).

**위치 / 길이** (2026-07-06 재측정):
- `src/utils/mpris/mpris.c:921` `setup_player_subscription` — 108줄 (126→108, `player_matches_preferred()` 추출로 축소)
- `src/utils/wayland/wayland_init.c:13` `wayland_init_surface` — 115줄
- `src/lyrics/lyrics_manager.c:179` `lyrics_manager_load_lyrics` — 101줄

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

### R12. Unused includes 정리

> **A 단계 ✅ 완료 (2026-05-03, 43a6366)**: 22 파일에서 unused #include 37줄 순감.
> - 도구: `clang-tidy --checks="-*,misc-include-cleaner"` (compile_commands.json 활용)
> - False positive 5건 복구 (json-c/json.h ×4, shm.h forward dec 추가, stdio.h ×1)
> - 검증: meson build clean, Sway/Hyprland 런타임 (가사/D-Bus/pause-resume/instrumental/TTY)
>
> **B 단계 ⏸ 보류**: main.h 자체 슬림화. clang-tidy 분석 시 528건 missing include 발견되어 거대 변경 필요. 가독성 트레이드오프 (각 .c 파일이 stdint.h, string.h 등 다 직접 include) — 사용자 결정으로 보류.

---

### R13. SonarCloud 신규 5건 — S1121 ×4 + S995 ×1 (2026-07-06 유입)

**전부 2026-05-03 이후 커밋에서 유입. 코드 검증 완료.**

| 위치 | 룰 | 현재 코드 | 수정 |
|---|---|---|---|
| `wayland_events.c:261` | S1121 | `state->surface.width = state->surface.height = 0;` | 두 문장으로 분리 |
| `config.c:607` | S1121 | `head = tail = node;` (`parse_config_keys_from_file`) | 분리 또는 아래 macro |
| `config.c:817` | S1121 | `*head = *tail = node;` (`add_section_node`) | 〃 |
| `config.c:954` | S1121 | `*head = *tail = node;` (`add_config_key_node`) | 〃 |
| `main.c:276` | S995 | `touch_active_track_cache(struct lyrics_state *state)` — 본문 44줄 전부 읽기 전용 | `const struct lyrics_state *` |

**config.c 3건 추가 참고**: 동일한 head/tail-append 6줄 블록의 3중 복붙 (`grep "= tail = "` 기준 src/ 전체에서 이 3곳뿐). 단순 분리로 Sonar만 끄거나, `APPEND_NODE(head, tail, node)` 매크로로 중복까지 제거하는 선택지.

**우선순위**: HIGH (zero-risk, ~15분, R17과 묶음) — 완료 시 SonarCloud 이슈 0건 복귀.

---

### R14. system_tray 아이콘 폴백 3중 중복

**위치**: `src/user_experience/system_tray/system_tray.c:896-909` (`system_tray_update_icon_with_fallback`, 커밋 0bb966e에서 유입)

**상태**: cached → MPRIS → iTunes 3개 폴백 분기가 각각 `set_indicator_icon_cached(metadata_hash); return true;`를 반복.

**작업안**: 3개 조건을 `||`로 묶어 성공 시 한 곳에서만 호출.

**우선순위**: MEDIUM — 향후 폴백 tier 추가 시 누락 위험. ~10분 + 트레이 수동 테스트.

---

### R15. `max_retries` 조회 중복

**위치**: `src/translator/common/translator_common.c:105-106`, `:554-556`

**상태**: `config_get()` + `cfg ? cfg->translation.max_retries : 3` 이 2곳 중복. 기본값 `3`도 `config_init_defaults`와 소스 이원화.

**작업안**: `static int translator_get_max_retries(void)` 추출, 554행의 ad-hoc 블록 스코프 제거.

**우선순위**: LOW — ~10분.

---

### R16. 캐시 경로 prefix 검사 중복 + NULL 가드

**위치**: `src/main.c:296-299` (신규 `touch_active_track_cache`) vs `:332-333` (기존 `monitor_track_and_files`)

**상태**: "경로가 캐시 디렉토리 안인가" 검사(`get_cache_base_dir()` + `strncmp`)가 2곳 중복. 추가로 `get_cache_base_dir()`이 NULL 반환 가능($HOME 미설정 등)한데 `strlen(NULL)` → 크래시. 잠재 위험은 기존부터 있었으나 이번 diff로 2곳으로 확산.

**작업안**: `static bool path_is_in_cache_dir(const char *path)` 를 file_utils에 추출 (cache_base NULL 가드 포함), 두 곳에서 사용.

**우선순위**: LOW — ~15분. NULL 가드는 실질 가치 있음.

---

### R17. `char tr_path[768]` magic number 회귀

**위치**: `src/main.c:309`

**상태**: `constants.h`에 `PATH_BUFFER_SIZE 1024`가 이미 존재하는데 새 코드가 unnamed literal `768` 사용 — R10에서 정리한 패턴의 회귀.

**작업**: `char tr_path[PATH_BUFFER_SIZE];`

**우선순위**: TINY — R13 묶음에 포함.

---

## 권장 작업 순서

1. ~~SonarCloud 웹 마킹 작업~~ ✅ 완료 (2026-05-02)
2. ~~HTTP S5332 검토~~ ✅ 완료 (SAFE + acknowledgement)
3. ~~**TINY 묶음 PR** (R3 + R4 + R6 + R10 + R11, zero-risk)~~ ✅ 완료 (c15cdb9, 2026-05-02)
4. ~~**R1**: Wayland 필드 → `wl_conn` 통합~~ ✅ 완료 (9007eec, 2026-05-03)
5. ~~**R2-A**: S1820 `lyrics_state` 분리 (sub-struct)~~ ✅ 완료 (6b55f10, 2026-05-03)
6. ~~**R12-A**: misc-include-cleaner 기반 unused 제거~~ ✅ 완료 (43a6366, 2026-05-03)
7. **SEC-1**: MPRIS GVariant 타입 가드 — **최우선** (작은 수정, 확실한 DoS 차단)
8. **TINY 묶음 2** (R13 + R17): SonarCloud 5건 + magic number — zero-risk, ~15분
9. **SEC-2**: artUrl SSRF/`file://` 제한 — SEC-1과 같은 threat model이라 연달아 처리 권장
10. **Small 정리 PR** (R14 + R15 + R16): 중복 제거 + NULL 가드 — ~40분
11. **SEC-3 / SEC-4**: 하드닝 (경계 검사, `%00` 거부) — 여유 있을 때
12. **R7**: Translator vtable 추상화 — Ollama 추가 전 마치는 게 합리적
13. ~~R2-B / R12-B~~ ⏸ 보류 (둘 다 가치 트레이드오프로 deferred)
14. ~~R5/R8/R9~~ ⏸ 보류

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

- **최종 업데이트**: 2026-07-06 (전체 코드 재감사 — 리팩토링 + 보안)
- **현재 단계**: SEC-1 (MPRIS 타입 가드) → TINY 묶음 2 (R13+R17) → SEC-2 순 처리 대기
- **완료**: Phase 1-14 + 2026-05-02 SonarCloud 마킹 + TINY 묶음 (c15cdb9) + R1 (9007eec) + R2-A (6b55f10) + R12-A (43a6366)
- **남은 이슈**:
  - 보안: SEC-1, SEC-2 (MEDIUM), SEC-3, SEC-4 (LOW 하드닝)
  - 진행 예정: R13+R17 (SonarCloud 5건), R14, R15, R16, R7
  - 보류: R2-B (시그니처 분해, ROI 작음), R12-B (main.h 슬림화, 가독성 트레이드오프), R5 (릴리스 정책), R8/R9 (강제 아님)
- **목표**: A등급 유지, Quality Gate GREEN 유지, BLOCKER/CRITICAL 0개, SonarCloud 이슈 0건 복귀
- **참고**: Coverity CID 643610 수정 완료 (b3a410a). SEC-A/SEC-B 검증 완료 — 모두 안전 (NULL+empty 가드 충분). 2026-07-06 감사에서 `wshowlyrics-offset`/dbus_control/translator 캐시/lock_file/runtime_dir/파서 클린 판정
