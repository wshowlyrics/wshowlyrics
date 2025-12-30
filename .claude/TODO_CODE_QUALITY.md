# TODO: SonarCloud Code Quality Improvements

## 현재 상태 (2025-12-30)

### ✅ 완료된 작업
- **Security Hotspots**: 100% Reviewed (107개)
- **Phase 1-8 완료**: BLOCKER 0개, CRITICAL 0개 달성 ✅
- **전체 등급**: ⭐ A등급 (Maintainability, Reliability, Security)
- **Quality Gate**: 🟢 GREEN
- **Bugs**: 0개
- **Vulnerabilities**: 0개

### 📊 남은 작업
- **총 미해결 이슈**: 167개 (C 코드만, Python 제외)
  - **MAJOR**: 43개
  - **MINOR**: 124개

---

## Phase 9: MAJOR Severity 이슈 해결 (43개)

### Priority 1: 중첩 if 병합 (10개) - 50min
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

**이슈 목록**:
1. parser_utils.c:625 - AZts0PZ5S0NST-dYy2ON
2. parser_utils.c:525 - AZtq6019IKCkJKKT-vVX
3. parser_utils.c:560 - AZtq6019IKCkJKKT-vVY
4. parser_utils.c:615 - AZtq6019IKCkJKKT-vVb
5. main.c:257 - AZtp81xHhknWVGFnRajT
6. lrc_parser.c:154 - AZtp81swhknWVGFnRajM
7. lyrics_provider.c:595 - AZtp81vYhknWVGFnRajN
8. lyrics_provider.c:602 - AZtp81vYhknWVGFnRajO
9. system_tray.c:171 - AZs-_pRTEZ4CGuQgOkew
10. shm.c:138 - AZsw3M6KHpp0TbwUWQXN

---

### Priority 2: 파라미터 수 제한 초과 (10개) - 3h 20min
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

**이슈 목록**:
1. rendering_manager.c:48 (8 params) - AZtq606DIKCkJKKT-vVp
2. rendering_manager.c:65 (11 params) - AZtq606DIKCkJKKT-vVq
3. rendering_manager.c:95 (10 params) - AZtq606DIKCkJKKT-vVr
4. ruby_render.c:200 (8 params) - AZtq60x0IKCkJKKT-vVS
5. ruby_render.c:252 (8 params) - AZtq60x0IKCkJKKT-vVS
6. word_render.c:331 (9 params) - AZtq60yfIKCkJKKT-vVT
7. word_render.c:365 (8 params) - AZtq60yfIKCkJKKT-vVV
8. word_render.c:168 (8 params) - AZtp81qdhknWVGFnRajJ
9. dbus_control.c:66 (8 params) - AZtHSiUNBYCiXr9PEey3
10. wayland_events.c:115 (10 params) - AZsw3M-EHpp0TbwUWQbW

---

### Priority 3: 미사용 파라미터 제거 (10개) - 50min
**Rule**: c:S1172 (Remove unused function parameters)
**예상 시간**: 5min/개

**이슈 목록**:
1. parser_utils.c:152 - AZs-_pL4EZ4CGuQgOkel
2. parser_utils.c:492 - AZtscvfMKmH8o-EbaN0b
3. parser_utils.c:522 - AZtscvfMKmH8o-EbaN0c
4. parser_utils.c:544 - AZtscvfMKmH8o-EbaN0d
5. dbus_control.c:67 - AZtHSiUNBYCiXr9PEey4
6. dbus_control.c:68 - AZtHSiUNBYCiXr9PEey5
7. dbus_control.c:69 - AZtHSiUNBYCiXr9PEey6
8. dbus_control.c:70 - AZtHSiUNBYCiXr9PEey7
9. dbus_control.c:157 - AZtHSiUNBYCiXr9PEey8
10. dbus_control.c:193 - AZtHSiUNBYCiXr9PEey9

---

### Priority 4: 중첩 break 감소 (3개) - 1h
**Rule**: c:S924 (Reduce nested break statements)
**예상 시간**: 20min/개

**리팩토링 패턴**:
```c
// Before
while (outer) {
    while (inner) {
        if (condition) {
            break;  // Nested break
        }
    }
    if (condition) break;
}

// After - 함수 추출 또는 flag 변수 사용
bool should_exit = false;
while (outer && !should_exit) {
    while (inner && !should_exit) {
        if (condition) {
            should_exit = true;
        }
    }
}
```

**이슈 목록**:
1. mpris.c:515 - AZtJVZMwABppZyGqgPw4
2. translator_common.c:261 - AZsw3M8OHpp0TbwUWQZF
3. lrclib_provider.c:108 - AZsw3M8vHpp0TbwUWQZn

---

### Priority 5: 기타 MAJOR 이슈 (10개) - 2h 30min

**Note**: API에서 누락된 8개 이슈 추가 확인 필요

#### 확인된 이슈 (2개) - 1h 10min

#### 1. main.h:51 - Structure field limit (1h)
**이슈 키**: AZsw3M-vHpp0TbwUWQbx
**Rule**: c:S1820
**메시지**: "Refactor structure to have max 20 fields (currently 40)"

**리팩토링**:
- `struct lyrics_state`를 논리적 그룹으로 분리
- 렌더링, 상태, 설정 등을 별도 구조체로

---

#### 2. file_utils.c:605 - Obsolete function (10min)
**이슈 키**: AZtLbsuZPLnv084pGrSv
**Rule**: c:S1911
**메시지**: "Remove use of obsolete 'utime' function"

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

## Phase 10: MINOR Severity 이슈 해결 (124개)

### 개요
- **총 124개 이슈** (C 코드만)
- **3가지 주요 Rule**:
  - c:S5350: Pointer-to-const 변수 (50개)
  - c:S995: Pointer-to-const 파라미터 (62개)
  - c:S1659: 다중 선언 분리 (12개)

---

### Priority 1: Pointer-to-const 변수 (50개) - 2h 30min
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

**주요 파일**:
- translator_common.c (다수)
- lyrics_manager.c (다수)
- parser_utils.c (다수)
- rendering_manager.c
- config.c

**효과**:
- 불변성 보장
- 의도치 않은 수정 방지
- 컴파일러 최적화 향상

---

### Priority 2: Pointer-to-const 파라미터 (62개) - 3h
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

**주요 파일**:
- lyrics_manager.c (다수)
- rendering_manager.c (다수)
- translator_common.c (다수)
- ruby_render.c
- word_render.c

**효과**:
- API 계약 명확화 (읽기 전용)
- 함수 순수성 표현
- 멀티스레드 안전성 향상

---

### Priority 3: 다중 선언 분리 (12개) - 36min
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

**주요 파일**:
- parser_utils.c (다수)
- 기타 파서/렌더러 파일

**효과**:
- 가독성 향상
- 디버깅 용이성
- 포인터 선언 실수 방지

---

## 구현 계획

### Phase 9: MAJOR 이슈 (43개)
**예상 시간**: 8h 30min (8개 추가 이슈 확인 필요)

**단계별 실행**:
1. **중첩 if 병합 (10개)** - 50min
   - 간단한 조건 결합
   - 빌드 검증

2. **파라미터 수 제한 (10개)** - 3h 20min
   - 파라미터 구조체 설계
   - 함수 시그니처 변경
   - 호출 코드 업데이트
   - 빌드 검증

3. **미사용 파라미터 (10개)** - 50min
   - 파라미터 제거 또는 `__attribute__((unused))` 추가
   - 빌드 검증

4. **중첩 break (3개)** - 1h
   - flag 변수 또는 함수 추출
   - 빌드 검증

5. **기타 MAJOR (10개)** - 2h 30min
   - 구조체 리팩토링 (1h)
   - obsolete 함수 교체 (10min)
   - 추가 8개 이슈 확인 및 수정 (1h 20min)
   - 빌드 검증

---

### Phase 10: MINOR 이슈 (124개, 선택적)
**예상 시간**: 6h 6min

**단계별 실행**:
1. **Pointer-to-const 변수 (50개)** - 2h 30min
   - 변수 선언에 const 추가
   - 빌드 검증

2. **Pointer-to-const 파라미터 (62개)** - 3h
   - 함수 파라미터에 const 추가
   - 호출 코드 영향 확인
   - 빌드 검증

3. **다중 선언 분리 (12개)** - 36min
   - 한 줄에 하나의 선언으로 분리
   - 빌드 검증

---

## 총 예상 시간

| Phase | 작업 | 이슈 수 | 예상 시간 |
|-------|------|---------|-----------|
| 9 | MAJOR 이슈 | 43 | 8h 30min |
| 10 | MINOR 이슈 | 124 | 6h 6min |
| **총합계** | | **167** | **14h 36min** |

**버퍼 포함**: ~16-17시간 (테스트 및 디버깅)

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

- **생성일**: 2025-12-19
- **최종 업데이트**: 2025-12-30 (Phase 8 완료, Phase 9-10 계획 수립)
- **현재 Phase**: Phase 9 시작 준비
- **완료 Phases**: 1-8 (BLOCKER 0개, CRITICAL 0개)
- **남은 이슈**: MAJOR 43개, MINOR 124개
- **우선순위**: Medium (코드 품질, 유지보수성)
- **목표**: A등급 유지, Quality Gate GREEN 유지
- **남은 예상 시간**: ~15-17h
