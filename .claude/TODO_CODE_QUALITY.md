# TODO: SonarCloud Code Quality Improvements

## 현재 상태 (2026-02-11)

### ✅ 완료된 Phase (1-8)
- **Bugs**: 0개
- **Vulnerabilities**: 0개
- **BLOCKER**: 0개
- **CRITICAL**: ⚠️ 1개 (NEW — mpris.c:288 Cognitive Complexity, 2026-02-02 발생)
- **전체 등급**: ⭐ A등급 (Maintainability, Reliability, Security)
- **Quality Gate**: 🟢 GREEN
- **Security Hotspots**: 100% Reviewed

### 📊 남은 작업
- **총 미해결 이슈**: 174개 (C 코드만, Python 제외)
  - **CRITICAL**: 1개 (+1, NEW)
  - **MAJOR**: 57개 (+14 from 43)
  - **MINOR**: 116개 (-10 from 126)

### 📈 변동 사항 (2026-01-01 → 2026-02-11)
- cache_mode, runtime_dir 등 기능 추가로 MAJOR 이슈 증가
- 일부 MINOR 이슈는 코드 변경으로 자연 해소
- **새로운 CRITICAL**: mpris.c `get_current_song()` Cognitive Complexity 34 > 25 (S3776)

---

## Phase 8.5: CRITICAL Severity 이슈 해결 (1개) — NEW

### Cognitive Complexity 초과 (1개) - 30min
**Rule**: c:S3776 (Cognitive Complexity)
**파일**: mpris.c:288 (`get_current_song()`)
**메시지**: "Refactor this function to reduce its Cognitive Complexity from 34 to the 25 allowed."
**생성일**: 2026-02-02

**리팩토링 방향**:
- 중첩 조건문을 early return으로 변환
- helper 함수로 분리 (metadata 파싱, playerctl 호출 등)
- 복잡한 분기를 별도 함수로 추출

---

## Phase 9: MAJOR Severity 이슈 해결 (57개)

### Priority 1: 중첩 if 병합 (11개) - 55min
**Rule**: c:S1066 (Merge nested if statements)
**예상 시간**: 5min/개

**리팩토링 패턴**:
```c
// Before
if (a) {
    if (b) {
        action();
    }
}

// After
if (a && b) {
    action();
}
```

**대상 파일**:
- parser_utils.c:525, 560, 617, 627
- lyrics_provider.c:595, 602
- config.c:1254
- main.c:281
- lrc_parser.c:154
- shm.c:138
- system_tray.c:194

---

### Priority 2: 파라미터 수 제한 초과 (12개) - 4h
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
- lrcx_parser.c:83, 208 (2개 함수, NEW)
- dbus_control.c:66 (1개 함수)
- wayland_events.c:115 (1개 함수)

---

### Priority 3: 미사용 파라미터 제거 (12개) - 1h
**Rule**: c:S1172 (Remove unused function parameters)
**예상 시간**: 5min/개

**주요 대상 파일**:
- parser_utils.c:152, 492, 522, 544 (4개)
- dbus_control.c:67, 68, 69, 70, 157, 193, 193, 198 (8개, +2 NEW)

---

### Priority 4: 중첩 break 감소 (3개) - 1h
**Rule**: c:S924 (Reduce nested break statements)
**예상 시간**: 20min/개

**리팩토링 패턴**:
```c
// Before
while (outer) {
    while (inner) {
        if (condition) break;
    }
    if (condition) break;
}

// After - flag 변수 사용
bool should_exit = false;
while (outer && !should_exit) {
    while (inner && !should_exit) {
        if (condition) should_exit = true;
    }
}
```

**대상 파일**:
- mpris.c:618
- translator_common.c:263
- lrclib_provider.c:108

---

### Priority 5: 기타 MAJOR 이슈 (19개) - 3h 30min

#### 1. 주석 처리된 코드 제거 (2개) - 10min
**Rule**: c:S125
- lyrics_provider.c:428, 443

---

#### 2. Obsolete function 교체 (1개) - 10min
**Rule**: c:S1911
**파일**: file_utils.c:691

**수정**:
```c
// Before
#include <utime.h>
utime(path, NULL);

// After
#include <sys/time.h>
utimes(path, NULL);  // POSIX standard
```

---

#### 3. 구조체 필드 제한 (1개) - 1h
**Rule**: c:S1820
**파일**: main.h:51
**메시지**: "Refactor structure to have max 20 fields (currently 42, was 40)"

**리팩토링**:
- `struct lyrics_state`를 논리적 그룹으로 분리
- 렌더링, 상태, 설정 등을 별도 구조체로 분리

---

#### 4. #include 위치 (1개) - 15min
**Rule**: c:S954
- lang_detect.c:12

---

#### 5. 기타 신규 MAJOR (14개 추정)
- 코드 변경(cache_mode, runtime_dir 등)으로 신규 발생
- API에서 확인된 57개 중 위 43개 이외의 나머지
- 세부 파일/라인은 다음 스캔 후 확인 필요

---

## Phase 10: MINOR Severity 이슈 해결 (116개)

### Priority 1: Pointer-to-const 파라미터 (47개) - 2h 21min
**Rule**: c:S995
**메시지**: "Make the type of this parameter a pointer-to-const"
**예상 시간**: 3min/개

**리팩토링 패턴**:
```c
// Before
void process(struct lyrics_data *data);

// After
void process(const struct lyrics_data *data);
```

**효과**:
- API 계약 명확화 (읽기 전용)
- 함수 순수성 표현
- 멀티스레드 안전성 향상

---

### Priority 2: 다중 선언 분리 (38개) - 1h 54min
**Rule**: c:S1659
**메시지**: "Define each identifier in a dedicated statement"
**예상 시간**: 3min/개

**리팩토링 패턴**:
```c
// Before
int x = 1, y = 2, z = 3;
char *a, *b, *c;

// After
int x = 1;
int y = 2;
int z = 3;
char *a;
char *b;
char *c;
```

**효과**:
- 가독성 향상
- 디버깅 용이성
- 포인터 선언 실수 방지

---

### Priority 3: Pointer-to-const 변수 (35개) - 1h 45min
**Rule**: c:S5350
**메시지**: "Make the type of this variable a pointer-to-const"
**예상 시간**: 3min/개

**리팩토링 패턴**:
```c
// Before
char *text = line->text;

// After
const char *text = line->text;
```

**효과**:
- 불변성 보장
- 의도치 않은 수정 방지
- 컴파일러 최적화 향상

---

### Priority 4: 불필요한 cast 제거 (9개) - 27min
**Rule**: c:S1905
**메시지**: "Remove redundant casts"
**예상 시간**: 3min/개

**효과**:
- 코드 간결성 향상
- 컴파일러 타입 추론 활용

---

### Priority 5: 에러 발생 루프 리팩토링 (1개) - 15min
**Rule**: c:S886
**예상 시간**: 15min

---

## 총 예상 시간

| Phase | 작업 | 이슈 수 | 예상 시간 |
|-------|------|---------|-----------|
| 8.5 | CRITICAL 이슈 | 1 | 30min |
| 9 | MAJOR 이슈 | 57 | 10h 25min |
| 10 | MINOR 이슈 | 116 | 6h 42min |
| **총합계** | | **174** | **17h 37min** |

**버퍼 포함**: ~20시간 (테스트 및 디버깅)

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

3. **SonarCloud 검증**:
   - 이슈 해결 확인
   - 새로운 이슈 발생 여부

---

## 커밋 메시지 형식

```
refactor: [Phase N] [요약 제목]

[상세 설명]

Changes:
- [변경 사항 1]
- [변경 사항 2]

Benefits:
- [개선 효과 1]
- [개선 효과 2]

Fixes: SonarCloud issues [issue-key-1], [issue-key-2]

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

---

## 참고 자료

- **SonarCloud Dashboard**: https://sonarcloud.io/project/overview?id=unstable-code_lyrics
- **SonarCloud Issues API**: https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics

---

## 상태

- **최종 업데이트**: 2026-02-11 (SonarCloud API 데이터 반영)
- **현재 Phase**: Phase 8.5 (CRITICAL 1개 신규 발생) → Phase 9
- **완료 Phases**: 1-8 (Bugs 0개, Vulnerabilities 0개, BLOCKER 0개)
- **남은 이슈**: CRITICAL 1개, MAJOR 57개, MINOR 116개 (총 174개)
- **우선순위**: Medium-High (CRITICAL 이슈 신규 발생)
- **목표**: A등급 유지, Quality Gate GREEN 유지, CRITICAL 0개 복원
- **남은 예상 시간**: ~20h
- **참고**: S1871 (중복 브랜치) 이슈는 코드 변경으로 자연 해소됨
