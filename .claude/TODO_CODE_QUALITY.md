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
**참고**: 기존 잔여 MAJOR였던 S1820(lyrics_state 42필드)은 R1+R2-A 구조체 분리로 해소됨. 위 "현재 (07-06)" 5건(S1121 ×4, S995 ×1)은 **R13으로 코드상 전부 해소됨 (4446c3d, 2026-07-06)** — 다음 재분석 시 0건 복귀 예상. 표의 수치는 재분석 전 SonarCloud 리포트 기준.

### 처리 이력
- 2026-05-02: GitLab 메인 전환으로 SonarCloud 프로젝트 새 ID(`AZzxDhZL...`)로 재분석.
  Won't Fix / SAFE 마킹 전부 리셋. 코드 변경 없이 카운트만 33+48 증가.
- 2026-05-02: 일괄 마킹 스크립트로 32 issues + 47 hotspots 처리. 정책은 CLAUDE.md "Marking Issues / Hotspots" 표 참조.
- 2026-05-02: S5332 HTTP 1건은 SAFE + acknowledgement 코멘트로 처리 (외부 URL 통제 불가, API가 ACKNOWLEDGED 미지원).
- 2026-07-06: 43a6366 이후 변경분(15커밋) 중심 리팩토링 + 보안 재감사. 보안 MEDIUM 2건(SEC-1, SEC-2) 및 신규 리팩토링 5건(R13–R17) 발견. `wshowlyrics-offset` / dbus_control / translator 캐시 / lock_file / runtime_dir / 파서는 클린 판정.
- 2026-07-06: 감사 발견 항목 전량 처리 (6커밋). SEC-1 (921a6c1) → R13+R17 (4446c3d) → SEC-2 (dde4bd5) → R14~R16 (a9bdf83) → SEC-3/4 (4eeba97) → R7 (f8732f5). 매 단계 `werror=true` 빌드 클린, SEC-1/SEC-2/R7은 실행 검증. **SEC-2 IP 차단은 로컬 동일유저 위협모델 분석 결과 실효 없음 + 자체호스팅(LAN/Tailscale/localhost) 파괴로 의도적 제외**, 대신 매직바이트 포맷 allowlist로 디코더 표면 축소. R7은 GitLab 이슈 #2 vtable 설계.

---

## 남은 작업 — 실제 코드 변경 필요

### 🔒 보안 — 2026-07-06 감사 발견 (신규)

| ID | 심각도 | 위치 | 문제 | 상태 |
|---|---|---|---|---|
| ~~SEC-1~~ | MEDIUM | mpris.c | 악성 MPRIS 메타데이터로 NULL deref DoS (GVariant 타입 미검증) | ✅ 완료 (921a6c1) — `dup_string_variant` 헬퍼 + PlaybackStatus 타입 가드 |
| ~~SEC-2~~ | MEDIUM | system_tray.c | artUrl 경유 SSRF + `file://` 임의 파일 디코드 | ✅ 완료 (dde4bd5) — 매직바이트 포맷 allowlist + realpath/S_ISREG + 크기상한. **IP 차단은 의도적 제외** (로컬 동일유저 위협모델에선 무의미 + 자체호스팅 파괴) |
| ~~SEC-3~~ | LOW | config.c | symlink 폴백이 resolved-path 검증 우회 + prefix 경계 미검사 | ✅ 완료 (4eeba97) — `path_has_dir_prefix` 경계검사 + 폴백을 `is_secret_store_path`(resolved가 /run·$XDG_RUNTIME_DIR)로 제한 |
| ~~SEC-4~~ | LOW | lyrics_provider.c | `%00` 디코딩 시 embedded NUL 허용 (경로 절단) | ✅ 완료 (4eeba97) — `val != 0` 조건으로 `%00` 리터럴 유지 |

### 요약 — 미완료 마이그레이션 / 정리 항목

| ID | 위치 | 패턴 | 우선순위 / 상태 |
|---|---|---|---|
| ~~R1~~ | main.h `lyrics_state` Wayland 필드 7개 | `wl_conn` 도입 후 옛 필드 미제거 (e0020f0) | ✅ 완료 (9007eec, 42→35 필드) |
| ~~R2~~ | main.h `lyrics_state` 35필드 | S1820 — 5개 sub-struct 분리 (style/surface/playback/config/runtime) | ✅ 완료 (6b55f10, 35→6 필드). B(시그니처 분해)는 후속 |
| ~~R3~~ | itunes_artwork.c, lrclib_provider.c | thin wrapper 정리 | ✅ 완료 (c15cdb9) |
| ~~R4~~ | config.c, lyrics_provider.c | `config_trim_whitespace` 정리 | ✅ 완료 (c15cdb9) |
| **R5** | config.c `[deepl]` section | Deprecated 섹션 — 제거 데드라인 결정 | ⏸ 보류 (정책) — 07-06 확인: 코드 위치 config.c:211-215, 893-908로 이동만, 변동 없음 |
| ~~R6~~ | mpris.c Seeked signal | 빈 콜백 정리 | ✅ 완료 (c15cdb9) |
| ~~R7~~ | lyrics_provider.c dispatcher | 4-branch if/else → provider 테이블 | ✅ 완료 (f8732f5) — 이슈 #2 vtable 설계. `struct translator_provider` 레지스트리로 dispatch + main.c init/cleanup 3곳 통합. Ollama = 새 모듈 + 배열 한 줄 |
| **R8** | parser_utils.c:374 `find_word_start` | 152줄 함수 (07-06 재측정: 정확히 152줄 유지) | ⏸ 보류 (가독성만 목적, 강제 아님) |
| **R9** | mpris.c:921, wayland_init.c:13, lyrics_manager.c:179 | 100+줄 함수 3개 (07-06 재측정: 108/115/101줄 — `setup_player_subscription`은 126→108줄로 축소) | ⏸ 보류 (강제 아님) |
| ~~R10~~ | config.c, deepl_translator.c | Magic number 버퍼 | ✅ 완료 (c15cdb9) |
| ~~R11~~ | config.c | `XDG_CONFIG_HOME` 빈 문자열 가드 + 잠재 NULL deref | ✅ 완료 (c15cdb9) |
| ~~R12-A~~ | 22 파일 | clang-tidy `misc-include-cleaner` 기준 unused #include 제거 (37 줄 순감) | ✅ 완료 (43a6366) |
| **R12-B** | main.h | main.h 슬림화 (~30 .c가 stdint.h 등 직접 include 필요) | ⏸ 보류 (B 옵션 — 가독성 트레이드오프) |
| ~~R13~~ | wayland_events.c:261, config.c:607/817/954, main.c:276 | SonarCloud 신규 5건 (S1121 ×4 체인 할당, S995 ×1 const 누락) | ✅ 완료 (4446c3d) — 체인 할당 문장 분리 + const 파라미터 |
| ~~R14~~ | system_tray.c | 아이콘 폴백 3-tier에서 `set_indicator_icon_cached(); return true;` 3중 복붙 | ✅ 완료 (a9bdf83) — 단락평가 `||` 하나로 통합 |
| ~~R15~~ | translator_common.c | `max_retries` 조회 + 기본값 `3` 중복 | ✅ 완료 (a9bdf83) — `translator_get_max_retries()` 추출 |
| ~~R16~~ | main.c ×2, file_utils.c/.h | 캐시 경로 prefix 검사 중복 + 빈 cache_base 시 오판 | ✅ 완료 (a9bdf83) — `path_is_in_cache_dir()` 추출. 참고: `get_cache_base_dir()`은 static char[]라 NULL이 아닌 빈 문자열 반환 → `strncmp(...,0)` 항상매치로 로컬 핫리로드 막히던 버그를 가드로 해소 |
| ~~R17~~ | main.c:309 | `char tr_path[768]` — R10에서 정리한 magic number 패턴 회귀 | ✅ 완료 (4446c3d) — `PATH_BUFFER_SIZE` |
| **Ollama** | 신규 `src/translator/ollama/` | R7 vtable 위에 `ollama_translator.c`(HTTP 요청/응답 파싱) 작성 + `ollama_provider` 등록 (translator_common.h extern + 배열 한 줄) | ⏸ **미착수 — 실제 기능 작업**. R7로 통합 표면만 준비됨 (dispatch/init/cleanup 무수정) |

**TINY 묶음 (R3+R4+R6+R10+R11)**: ✅ 완료 (c15cdb9, 2026-05-02)
**TINY 묶음 2 (R13+R17)**: ✅ 완료 (4446c3d, 2026-07-06) — SonarCloud 5건 전부 해소 + magic number

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

### R12. Unused includes 정리

> **A 단계 ✅ 완료 (2026-05-03, 43a6366)**: 22 파일에서 unused #include 37줄 순감.
> - 도구: `clang-tidy --checks="-*,misc-include-cleaner"` (compile_commands.json 활용)
> - False positive 5건 복구 (json-c/json.h ×4, shm.h forward dec 추가, stdio.h ×1)
> - 검증: meson build clean, Sway/Hyprland 런타임 (가사/D-Bus/pause-resume/instrumental/TTY)
>
> **B 단계 ⏸ 보류**: main.h 자체 슬림화. clang-tidy 분석 시 528건 missing include 발견되어 거대 변경 필요. 가독성 트레이드오프 (각 .c 파일이 stdint.h, string.h 등 다 직접 include) — 사용자 결정으로 보류.

---

## 권장 작업 순서

1. ~~SonarCloud 웹 마킹 작업~~ ✅ 완료 (2026-05-02)
2. ~~HTTP S5332 검토~~ ✅ 완료 (SAFE + acknowledgement)
3. ~~**TINY 묶음 PR** (R3 + R4 + R6 + R10 + R11, zero-risk)~~ ✅ 완료 (c15cdb9, 2026-05-02)
4. ~~**R1**: Wayland 필드 → `wl_conn` 통합~~ ✅ 완료 (9007eec, 2026-05-03)
5. ~~**R2-A**: S1820 `lyrics_state` 분리 (sub-struct)~~ ✅ 완료 (6b55f10, 2026-05-03)
6. ~~**R12-A**: misc-include-cleaner 기반 unused 제거~~ ✅ 완료 (43a6366, 2026-05-03)
7. ~~**SEC-1**: MPRIS GVariant 타입 가드~~ ✅ 완료 (921a6c1, 2026-07-06)
8. ~~**TINY 묶음 2** (R13 + R17): SonarCloud 5건 + magic number~~ ✅ 완료 (4446c3d, 2026-07-06)
9. ~~**SEC-2**: artUrl 포맷검사 + `file://` 하드닝~~ ✅ 완료 (dde4bd5, 2026-07-06) — IP 차단은 의도적 제외
10. ~~**Small 정리** (R14 + R15 + R16): 중복 제거 + 가드~~ ✅ 완료 (a9bdf83, 2026-07-06)
11. ~~**SEC-3 / SEC-4**: 하드닝 (경계 검사, `%00` 거부)~~ ✅ 완료 (4eeba97, 2026-07-06)
12. ~~**R7**: Translator vtable 레지스트리~~ ✅ 완료 (f8732f5, 2026-07-06) — 이슈 #2. **Ollama 통합 인프라만 완료** (실제 `ollama_translator.c` 구현은 별도 작업, 위 표 참조)
13. ~~R2-B / R12-B~~ ⏸ 보류 (둘 다 가치 트레이드오프로 deferred)
14. ~~R5/R8/R9~~ ⏸ 보류

**2026-07-06 세션 완료**: SEC-1~4 + R13/R14/R15/R16/R17 + R7 전부 처리. 로컬 master 커밋됨 (미푸시). 감사/정리(cleanup) 작업은 모두 종료 — 남은 건 기능 작업(Ollama 구현)과 ⏸ 보류(정책/트레이드오프) 항목뿐.

---

## 참고 자료

- **SonarCloud Dashboard**: https://sonarcloud.io/project/overview?id=wshowlyrics_wshowlyrics
- **SonarCloud Issues API**: https://sonarcloud.io/api/issues/search?componentKeys=wshowlyrics_wshowlyrics
- **SonarCloud Hotspots API**: https://sonarcloud.io/api/hotspots/search?projectKey=wshowlyrics_wshowlyrics
- **Coverity Dashboard**: https://scan.coverity.com/projects/wshowlyrics
- **마킹 정책**: `CLAUDE.md` → "Marking Issues / Hotspots (Write API)" 섹션

---

## 상태

- **최종 업데이트**: 2026-07-06 (감사 발견 항목 전량 처리 완료)
- **현재 단계**: 2026-07-06 감사/정리분(SEC-1~4, R13~R17, R7) 전부 완료, 로컬 master 커밋 (미푸시). 남은 기능 작업: **Ollama translator 구현** (R7 인프라 위에서 진행)
- **완료**: Phase 1-14 + 2026-05-02 SonarCloud 마킹 + TINY 묶음 (c15cdb9) + R1 (9007eec) + R2-A (6b55f10) + R12-A (43a6366)
  - **2026-07-06 세션**: SEC-1 (921a6c1) · R13+R17 (4446c3d) · SEC-2 (dde4bd5) · R14~R16 (a9bdf83) · SEC-3/4 (4eeba97) · R7 (f8732f5)
- **남은 이슈**:
  - 기능 작업: **Ollama translator 구현** (R7 vtable 위에 새 모듈 + provider 등록)
  - ⏸ 보류: R2-B (시그니처 분해, ROI 작음), R12-B (main.h 슬림화, 가독성 트레이드오프), R5 (릴리스 정책), R8/R9 (강제 아님)
- **목표**: A등급 유지, Quality Gate GREEN 유지, BLOCKER/CRITICAL 0개, SonarCloud 이슈 0건 복귀 (R13으로 5건 해소 → 재분석 시 0건 복귀 예상)
- **참고**: Coverity CID 643610 수정 완료 (b3a410a). SEC-A/SEC-B 검증 완료 — 모두 안전 (NULL+empty 가드 충분). 2026-07-06 감사에서 `wshowlyrics-offset`/dbus_control/translator 캐시/lock_file/runtime_dir/파서 클린 판정
