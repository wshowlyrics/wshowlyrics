# TODO: SonarCloud Code Quality Improvements

## 현재 상태 (2026-01-01)

### ✅ 완료된 Phase (1-8)
- **Bugs**: 0개
- **Vulnerabilities**: 0개
- **BLOCKER**: 0개
- **CRITICAL**: 0개
- **전체 등급**: ⭐ A등급 (Maintainability, Reliability, Security)
- **Quality Gate**: 🟢 GREEN
- **Security Hotspots**: 100% Reviewed

### 📊 남은 작업
- **총 미해결 이슈**: 169개 (C 코드만, Python 제외)
  - **MAJOR**: 43개
  - **MINOR**: 126개

---

## Phase 9: MAJOR Severity 이슈 해결 (43개)

### Priority 1: 중첩 if 병합 (7개) - 35min
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

**주요 대상 파일**:
- rendering_manager.c (3개 함수)
- ruby_render.c (2개 함수)
- word_render.c (3개 함수)
- dbus_control.c (1개 함수)
- wayland_events.c (1개 함수)

---

### Priority 3: 미사용 파라미터 제거 (10개) - 50min
**Rule**: c:S1172 (Remove unused function parameters)
**예상 시간**: 5min/개

**주요 대상 파일**:
- parser_utils.c (4개)
- dbus_control.c (6개)

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
- mpris.c:515
- translator_common.c:261
- lrclib_provider.c:108

---

### Priority 5: 기타 MAJOR 이슈 (13개) - 2h 30min

#### 1. 주석 처리된 코드 제거 (2개) - 10min
**Rule**: c:S125
**예상 시간**: 5min/개

---

#### 2. Obsolete function 교체 (1개) - 10min
**Rule**: c:S1911
**파일**: file_utils.c:605

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
**메시지**: "Refactor structure to have max 20 fields (currently 40)"

**리팩토링**:
- `struct lyrics_state`를 논리적 그룹으로 분리
- 렌더링, 상태, 설정 등을 별도 구조체로 분리

---

#### 4. 중복 브랜치 통합 (1개) - 15min
**Rule**: c:S1871
**예상 시간**: 15min

---

#### 5. 기타 (1개) - 15min
**Rule**: c:S954
**예상 시간**: 15min

---

## Phase 10: MINOR Severity 이슈 해결 (126개)

### Priority 1: Pointer-to-const 파라미터 (54개) - 2h 42min
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

### Priority 2: Pointer-to-const 변수 (44개) - 2h 12min
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

### Priority 3: 다중 선언 분리 (23개) - 1h 9min
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

### Priority 4: 불필요한 cast 제거 (4개) - 12min
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
| 9 | MAJOR 이슈 | 43 | 8h 20min |
| 10 | MINOR 이슈 | 126 | 6h 30min |
| **총합계** | | **169** | **14h 50min** |

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

- **최종 업데이트**: 2026-01-01 (최신 API 데이터 반영)
- **현재 Phase**: Phase 9 시작 준비
- **완료 Phases**: 1-8 (Bugs 0개, Vulnerabilities 0개, BLOCKER 0개, CRITICAL 0개)
- **남은 이슈**: MAJOR 43개, MINOR 126개
- **우선순위**: Medium (코드 품질, 유지보수성)
- **목표**: A등급 유지, Quality Gate GREEN 유지
- **남은 예상 시간**: ~16-17h
