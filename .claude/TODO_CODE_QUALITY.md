# TODO: Code Quality & Security Improvements

## 현재 상태 (2026-02-23)

### ✅ 완료된 Phase (1-8)
- **Bugs**: 0개
- **Vulnerabilities**: 0개
- **BLOCKER**: 0개
- **전체 등급**: ⭐ A등급 (Maintainability, Reliability, Security)
- **Quality Gate**: 🟢 GREEN
- **Security Hotspots**: 100% Reviewed
- **Coverity**: CID 643610 수정 완료 (2026-02-23)

### 📊 남은 작업
- **SonarCloud 미해결 이슈**: 179개 (C 코드만, Python 제외)
  - **CRITICAL**: 2개 (+1, lyrics_provider.c:435 신규)
  - **MAJOR**: 48개 (-9 from 57, 일부 자연 해소)
  - **MINOR**: 129개 (+13 from 116)
- **코드 분석 발견 이슈**: 20개 (SonarCloud 미감지)
  - **CRITICAL**: 3개 (보안취약점/동시성)
  - **HIGH**: 8개
  - **MEDIUM**: 9개

### 📈 변동 사항 (2026-02-11 → 2026-02-23)
- **신규 SonarCloud CRITICAL**: lyrics_provider.c:435 `search_lyrics_in_paths()` Cognitive Complexity 28 > 25
- MAJOR 이슈 9개 자연 해소 (코드 변경으로 제거됨)
- MINOR 이슈 13개 신규 발생 (코드 추가에 따른 pointer-to-const, 다중 선언 등)
- **코드 분석으로 보안취약점 3건 신규 발견** (SonarCloud 미감지)
- Coverity CID 643610 (Out-of-bounds access in expand_path_token) 수정 완료

---

## Phase S: 보안취약점 & 동시성 이슈 (SonarCloud 미감지)

> SonarCloud가 탐지하지 못하는 실질적 보안취약점과 동시성 버그.
> 기능 안정성에 직접 영향을 미치므로 최우선 해결 대상.

### ✅ CRITICAL-S1: Command Injection via `system()` — 완료 (d51ec9e)

**파일**:
- `system_tray.c:346` — `$TERMINAL`, `$EDITOR` 미검증 shell 명령 실행
- `system_tray.c:706` — 트랙 메타데이터 기반 `notify-send` 실행
- `config.c:444` — `sanitize_path()` 결과를 shell 명령에 삽입
- `config.c:1124` — config 키 목록을 shell 명령에 삽입

**문제**:
```c
// system_tray.c:346 — 환경변수를 직접 shell 명령에 삽입
snprintf(cmd, sizeof(cmd), "%s -e %s \"%s\" &", terminal, editor, config_path);
int ret = system(cmd);
// $TERMINAL="xterm; rm -rf ~; echo" → 임의 명령 실행 가능
```

**영향**: 악의적 환경변수 또는 메타데이터로 임의 명령 실행 가능. 로컬 사용자 권한 필요하지만 최소 권한 원칙 위반.

**수정 방향**:
1. 모든 `system()` 호출을 `posix_spawn()` 또는 `fork()`+`execvp()`로 교체
2. 인자를 배열로 분리하여 shell 해석 우회
3. `notify-send`는 `libnotify` API 직접 사용 고려

```c
// Before (취약)
snprintf(cmd, sizeof(cmd), "%s -e %s \"%s\" &", terminal, editor, config_path);
system(cmd);

// After (안전)
char *argv[] = { (char*)terminal, "-e", (char*)editor, config_path, NULL };
pid_t pid;
posix_spawn(&pid, terminal, NULL, NULL, argv, environ);
```

---

### ✅ CRITICAL-S2: Data Race — 번역 스레드 동기화 부재 — 완료 (d51ec9e)

**파일**:
- `lyrics_types.h:58-63` — 공유 필드 선언
- `translator_common.c:543,632,645` — 번역 스레드에서 쓰기
- `rendering_manager.c:251` — 메인 스레드에서 읽기
- `lyrics_manager.c:57-74` — busy-wait 취소 로직

**문제**:
```c
// lyrics_types.h — _Atomic 선언 없이 두 스레드가 동시 접근
bool translation_in_progress;       // 스레드→쓰기, 메인→읽기
bool translation_should_cancel;     // 메인→쓰기, 스레드→읽기
int translation_current;            // 스레드→쓰기, 메인→읽기
int translation_total;              // 스레드→쓰기, 메인→읽기
// line->translation (char*) — 스레드→쓰기, 렌더러→읽기 (포인터 torn read 가능)
```

```c
// lyrics_manager.c:62 — 비원자적 busy-wait (UB)
while (lyrics->translation_in_progress && wait_count < 100) {
    nanosleep(&wait_delay, NULL);  // 컴파일러가 load를 루프 밖으로 hoist 가능
    wait_count++;
}
```

**영향**:
- `-O2` 이상에서 busy-wait 무한 루프 또는 미진입 가능 (컴파일러 최적화)
- `line->translation` 포인터 torn read → use-after-free, segfault
- `cancel_and_wait_translation()`에서 join 누락 케이스 존재 (스레드 핸들 누수)

**수정 방향**:
1. `_Atomic bool`/`_Atomic int`로 공유 필드 선언 (C11)
2. `line->translation` 쓰기를 `pthread_mutex_t`로 보호
3. busy-wait를 `pthread_cond_t` + mutex로 교체
4. `pthread_join()`을 무조건 호출하도록 수정

**관련 이슈**: HIGH-H2 (번역 스레드 join 누락), HIGH-H7 (비원자적 busy-wait)

---

### ✅ CRITICAL-S3: `realloc()` 메모리 누수 패턴 — 완료 (d51ec9e)

**파일**: `mpris.c:224-230`

**문제**:
```c
// realloc 실패 시 원본 포인터 유실 + NULL 역참조
if (*count >= capacity) {
    capacity = capacity == 0 ? 4 : capacity * 2;
    players = realloc(players, capacity * sizeof(char*));  // BUG
}
players[*count] = strdup(player_name);  // realloc 실패 시 NULL 역참조
```

**수정**:
```c
char **new_players = realloc(players, capacity * sizeof(char*));
if (!new_players) {
    for (int i = 0; i < *count; i++) free(players[i]);
    free(players);
    *count = 0;
    return NULL;
}
players = new_players;
```

---

### ✅ HIGH-H1: 번역 스레드 join 실패 시 use-after-free — 완료 (d51ec9e, CRITICAL-S2에서 함께 수정)

---

### ✅ HIGH-H2: 파일 크기 제한 없는 파싱 — 완료

**수정 내용**: `constants.h`에 MAX_LYRICS_FILE_SIZE (10MB), MAX_CACHE_FILE_SIZE (5MB) 상수 추가. `parser_utils.c`와 `translator_common.c`에 크기 검증 추가.

---

### ✅ HIGH-H3: `sanitize_path()` 정적 버퍼 — 스레드 안전성 — 완료

**수정 내용**: `static` → `static _Thread_local` (C11 TLS)로 변경. 각 스레드가 독립 버퍼를 갖게 되어 경합 해소. API 시그니처 불변.

---

### ✅ HIGH-H4: `lrcx_find_segment_at_time()` 인덱스 추적 로직 오류 — 완료

**수정 내용**: tautology 비교 제거. `current_index` 변수로 루프 내 추적, 루프 후 1회만 `segment_index`에 기록.

---

### ✅ HIGH-H5: MPRIS `on_properties_changed()` TOCTOU — 완료

**수정 내용**: mutex 해제 전 `saved_player = strdup(current_player)` 복사본 저장. unlock/relock 후 `saved_player`를 사용하여 TOCTOU 경합 해소.

---

### ✅ HIGH-H6: `build_search_request_url()` snprintf 잘림 미검사 — 완료

**수정 내용**: `offset < 0 || (size_t)offset >= buffer_size` 검사 추가. 잘림 시 artist 파라미터 생략하고 track_name만으로 검색.

---

### 🟡 MEDIUM 이슈 (9개) — 요약

| # | 파일 | 문제 | 영향 |
|---|------|------|------|
| M1 | `main.c:57-105` | `display_detailed_help()` 원격 URL에서 텍스트 가져와 `%s` 치환 — 미신뢰 입력 | Low |
| M2 | `mpris.c:1187-1331` | `mpris_get_metadata()` 내 ~50줄 메타데이터 추출 코드 중복 | Maintainability |
| M3 | `translator_common.c:952-957` | `cache_path` snprintf — `target_lang` 길이 제한 없음 | Truncation |
| M4 | `parser_utils.c:792-823` | `normalize_fullwidth_punctuation()` 불완전 멀티바이트 시퀀스 읽기 | UB |
| M5 | `lyrics_provider.c:705-721` | `should_skip_provider()` 문자열 비교 기반 디스패치 | Maintainability |
| M6 | `lyrics_manager.c` | `cancel_and_wait_translation()` join 누락 케이스 (CRITICAL-S2 관련) | Thread leak |
| M7 | `config.c:484` | INI 파서 512바이트 줄 제한 — 긴 줄 무시 잘림 | Data loss |
| M8 | `system_tray.c:688` | `escape_shell_string()` 감사 필요 | Potential injection |
| M9 | `translator_common.c:166-191` | 캐시 JSON 파싱 시 파일 크기 제한 없음 (HIGH-H2 관련) | OOM |

---

## Phase 9: SonarCloud CRITICAL 이슈 (2개)

### 9-1. Cognitive Complexity 초과: mpris.c — 30min
**Rule**: c:S3776
**파일**: mpris.c:288 (`get_current_song()`)
**메시지**: "Cognitive Complexity 34 > 25"
**생성일**: 2026-02-02

**리팩토링 방향**:
- 중첩 조건문을 early return으로 변환
- helper 함수로 분리 (metadata 파싱, playerctl 호출 등)
- 복잡한 분기를 별도 함수로 추출

### 9-2. Cognitive Complexity 초과: lyrics_provider.c — 30min (NEW)
**Rule**: c:S3776
**파일**: lyrics_provider.c:435 (`search_lyrics_in_paths()`)
**메시지**: "Cognitive Complexity 28 > 25"
**생성일**: 2026-02-12

**리팩토링 방향**:
- 검색 경로별 로직을 helper 함수로 분리
- early return 패턴 적용
- 중첩 조건 평탄화

---

## Phase 10: SonarCloud MAJOR 이슈 (48개)

### Priority 1: 파라미터 수 제한 초과 (15개) - 5h
**Rule**: c:S107 (Function has too many parameters > 7)
**예상 시간**: 20min/개

**리팩토링 패턴**:
```c
// Before
void func(int a, int b, int c, int d, int e, int f, int g, int h);

// After
struct func_params {
    int a, b, c, d, e, f, g, h;
};
void func(struct func_params *params);
```

**주요 대상 파일**:
- rendering_manager.c:48, 65, 95 (3개 함수)
- ruby_render.c:200, 252 (2개 함수)
- word_render.c:168, 331, 365 (3개 함수)
- lrcx_parser.c:83, 208 (2개 함수)
- dbus_control.c:66 (1개 함수)
- wayland_events.c:115 (1개 함수)
- 기타 신규 3개

---

### Priority 2: 미사용 파라미터 제거 (10개) - 50min
**Rule**: c:S1172 (Remove unused function parameters)
**예상 시간**: 5min/개

**주요 대상 파일**:
- parser_utils.c:152, 492, 522, 544 (4개)
- dbus_control.c:67, 68, 69, 70, 157, 193 (6개)

---

### Priority 3: 중첩 if 병합 (7개) - 35min
**Rule**: c:S1066 (Merge nested if statements)
**예상 시간**: 5min/개

**대상 파일**:
- parser_utils.c:525, 560, 617, 627
- lyrics_provider.c:595, 602
- config.c:1254

---

### Priority 4: 중첩 break 감소 (5개) - 1h 40min
**Rule**: c:S924 (Reduce nested break statements)
**예상 시간**: 20min/개

**대상 파일**:
- mpris.c:618
- translator_common.c:263
- lrclib_provider.c:108
- 기타 신규 2개

---

### Priority 5: 기타 MAJOR 이슈 (11개) - 2h 30min

#### 1. 주석 처리된 코드 제거 (2개) - 10min
**Rule**: c:S125
- lyrics_provider.c:428, 443

#### 2. Obsolete function 교체 (1개) - 10min
**Rule**: c:S1911
**파일**: file_utils.c:691

```c
// Before
#include <utime.h>
utime(path, NULL);

// After
#include <sys/time.h>
utimes(path, NULL);  // POSIX standard
```

#### 3. 구조체 필드 제한 (1개) - 1h
**Rule**: c:S1820
**파일**: main.h:51
**메시지**: "Refactor structure to have max 20 fields (currently 42)"

**리팩토링**:
- `struct lyrics_state`를 논리적 그룹으로 분리
- 렌더링, 상태, 설정 등을 별도 구조체로 분리

#### 4. #include 위치 (1개) - 15min
**Rule**: c:S954
- lang_detect.c:12

#### 5. Include guard 이슈 (2개) - 20min
**Rule**: c:S3805
- 헤더 파일 내 #include 앞 다중 선언

#### 6. 기타 신규 MAJOR (4개)
- 코드 변경에 따른 추가 이슈
- 세부 파일/라인은 API에서 확인 필요

---

## Phase 11: SonarCloud MINOR 이슈 (129개)

### Priority 1: Pointer-to-const 파라미터 (~60개) - 3h
**Rule**: c:S995
**메시지**: "Make the type of this parameter a pointer-to-const"
**예상 시간**: 3min/개

```c
// Before
void process(struct lyrics_data *data);

// After
void process(const struct lyrics_data *data);
```

---

### Priority 2: 다중 선언 분리 (~25개) - 1h 15min
**Rule**: c:S1659
**메시지**: "Define each identifier in a dedicated statement"
**예상 시간**: 3min/개

```c
// Before
int x = 1, y = 2, z = 3;

// After
int x = 1;
int y = 2;
int z = 3;
```

---

### Priority 3: Pointer-to-const 변수 (~20개) - 1h
**Rule**: c:S5350
**메시지**: "Make the type of this variable a pointer-to-const"
**예상 시간**: 3min/개

---

### Priority 4: 기타 MINOR (~24개) - 1h 15min
- **불필요한 cast 제거** (c:S1905): ~9개
- **에러 발생 루프 리팩토링** (c:S886): 1개
- **기타 신규**: ~14개

---

## 총 예상 시간

| Phase | 작업 | 이슈 수 | 예상 시간 |
|-------|------|---------|-----------|
| S | 보안취약점 & 동시성 | 20 | 13h |
| 9 | SonarCloud CRITICAL | 2 | 1h |
| 10 | SonarCloud MAJOR | 48 | 10h 35min |
| 11 | SonarCloud MINOR | 129 | 6h 30min |
| **총합계** | | **199** | **31h 5min** |

**버퍼 포함**: ~37시간 (테스트 및 디버깅)

---

## 권장 작업 순서

1. **CRITICAL-S3** (30min) — `realloc()` 누수, 자체 완결적 수정
2. **CRITICAL-S1** (2~3h) — `system()` command injection, `posix_spawn()` 교체
3. **CRITICAL-S2 + HIGH-H1** (4~6h) — 번역 스레드 동기화, 함께 수정 필요
4. **HIGH-H2** (30min) — 파일 크기 제한
5. **HIGH-H3** (1h) — `sanitize_path()` 스레드 안전성
6. **HIGH-H4** (30min) — LRCX 인덱스 로직 수정
7. **Phase 9** (1h) — SonarCloud CRITICAL (Cognitive Complexity)
8. **Phase 10** → **Phase 11** — 나머지 SonarCloud 이슈

---

## 테스트 전략

### 각 Phase별 테스트
1. **빌드 검증**: `meson compile -C build`
2. **기능 회귀 테스트**:
   - Config 로드
   - 가사 표시 (LRCX, LRC, SRT)
   - 번역 (4개 provider)
   - Wayland 렌더링
   - System tray
   - MPRIS 플레이어 감지
   - D-Bus control interface

3. **보안 테스트** (Phase S 전용):
   - `$TERMINAL`/`$EDITOR`에 shell 메타문자 설정 후 Edit Settings 실행
   - 번역 중 트랙 변경 반복 (race condition 재현)
   - 대용량 가사 파일 (>10MB) 로드 시도
   - 동시 로깅 시 `sanitize_path()` 출력 검증

4. **정적 분석 검증**:
   - SonarCloud 이슈 해결 확인
   - Coverity 주간 스캔 결과 확인
   - 새로운 이슈 발생 여부

---

## 커밋 메시지 형식

```
fix: [Brief description]

[Detailed explanation]

Changes:
- [Change 1]
- [Change 2]

Benefits:
- [Benefit 1]
- [Benefit 2]

Fixes: [Tool] issues [issue-key-1], [issue-key-2]

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
```

---

## 참고 자료

- **SonarCloud Dashboard**: https://sonarcloud.io/project/overview?id=unstable-code_lyrics
- **SonarCloud Issues API**: https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics
- **Coverity Dashboard**: https://scan.coverity.com/projects/unstable-code-lyrics

---

## 상태

- **최종 업데이트**: 2026-02-23 (SonarCloud API + 코드 분석 반영)
- **현재 Phase**: Phase S (보안취약점 최우선) → Phase 9 → Phase 10 → Phase 11
- **완료 Phases**: 1-8 (Bugs 0개, Vulnerabilities 0개, BLOCKER 0개)
- **남은 이슈**: SonarCloud 179개 + 코드 분석 20개 = 총 199개
- **우선순위**: HIGH (보안취약점 3건, 동시성 버그 발견)
- **목표**: A등급 유지, Quality Gate GREEN 유지, 보안취약점 0개
- **남은 예상 시간**: ~37h (버퍼 포함)
- **참고**: Coverity CID 643610 수정 완료 (b3a410a), S1871 자연 해소
