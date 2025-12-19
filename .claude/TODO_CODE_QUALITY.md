# TODO: SonarCloud Code Quality Improvements

## 개요

SonarCloud에서 감지된 HIGH+ 코드 스멜 개선 작업
- **총 오픈 이슈**: 247개
- **CRITICAL 이슈**: 14개
- **MAJOR 이슈**: 20개 이상
- **예상 작업 시간**: 17-27시간

## Priority 1: 복잡도 리팩토링 (CRITICAL) ⚠️

### 🔥 최우선: config.c 복잡도 개선
**파일**: `src/user_experience/config/config.c`
**이슈**: 3개 (복잡도 129, 84 + 기타)
**예상 시간**: 6-8시간

- [ ] **라인 219: 복잡도 129 → 25 이하** (c:S3776)
  - 현재: 단일 함수에서 모든 설정 섹션 파싱
  - 목표: 섹션별 함수 분리
  ```c
  // Before: parse_config() - 복잡도 129
  // After:
  parse_display_section()
  parse_lyrics_section()
  parse_translation_section()
  parse_monitor_section()
  ```

- [ ] **라인 824: 복잡도 84 → 25 이하** (c:S3776)
  - 현재: 단일 함수에서 모든 검증 로직
  - 목표: 검증 로직 분리 또는 함수 포인터 테이블 사용
  ```c
  // Switch-case를 함수 포인터 테이블로 리팩토링
  typedef bool (*validator_fn)(const char *value);
  struct {
      const char *key;
      validator_fn validate;
  } validators[] = { ... };
  ```

- [ ] 라인 910, 660: 병합 가능한 if문 통합 (c:S1066)
- [ ] 라인 1005: 사용되지 않는 'offset' 변수 제거 (c:S1854)

### 🔥 Provider 복잡도 개선
**파일**: `src/provider/lrclib/lrclib_provider.c`
**이슈**: 3개 (복잡도 54 + 중첩 깊이 + 중첩 break)
**예상 시간**: 3-4시간

- [ ] **라인 31: 복잡도 54 → 25 이하** (c:S3776)
  - 현재: JSON 파싱 + 에러 핸들링 + 데이터 추출이 하나의 함수
  - 목표: 로직 분리
  ```c
  parse_lrclib_response() {
      json_object *root = parse_json_string(response);
      if (!root) return NULL;

      return extract_lyrics_data(root); // 별도 함수
  }
  ```

- [ ] **라인 139: 중첩 깊이 3단계 초과** (c:S134)
  - Early return 패턴 적용
  - Guard clauses로 중첩 제거

- [ ] **라인 122: 중첩 break문 2개 → 1개** (c:S924)
  - 중첩 루프를 별도 함수로 추출
  - `goto cleanup` 또는 flag 변수 사용

### 🎯 System Tray 복잡도 개선
**파일**: `src/user_experience/system_tray/system_tray.c`
**이슈**: 2개 (복잡도 49 + 병합 가능 if)
**예상 시간**: 2-3시간

- [ ] **라인 443: 복잡도 49 → 25 이하** (c:S3776)
  - 앨범 아트 처리 로직 분리
  - 에러 핸들링을 별도 함수로 추출

- [ ] 라인 408: 병합 가능한 if문 통합 (c:S1066)

### 📝 Parser 복잡도 개선
**파일**: `src/parser/lrc/lrcx_parser.c`
**이슈**: 1개 (복잡도 48)
**예상 시간**: 2-3시간

- [ ] **라인 93: 복잡도 48 → 25 이하** (c:S3776)
  - LRCX 파싱 로직 단계별 함수 분리
  - Word segment 파싱을 별도 함수로 추출

## Priority 2: Translation 파일 중첩 깊이 해소 (CRITICAL) 🔴

**공통 패턴**: 모든 translator 파일에서 동일한 중첩 깊이 문제
**이슈**: 5개 (c:S134)
**예상 시간**: 4-6시간

### 전략: Early Return 패턴 + 공통 로직 추출

- [ ] **deepl_translator.c**
  - 라인 340: 복잡도 36 → 25 이하 (c:S3776)
  - 라인 432: 중첩 깊이 3단계 초과 (c:S134)

- [ ] **gemini_translator.c**
  - 라인 371: 중첩 깊이 3단계 초과 (c:S134)

- [ ] **openai_translator.c**
  - 라인 372: 중첩 깊이 3단계 초과 (c:S134)

- [ ] **claude_translator.c**
  - 라인 361: 중첩 깊이 3단계 초과 (c:S134)

### 리팩토링 방법
```c
// Before: 중첩된 if문
if (response) {
    if (json) {
        if (content) {
            // 실제 로직
        }
    }
}

// After: Early return + Guard clauses
if (!response) return false;
if (!json) {
    cleanup_response(response);
    return false;
}
if (!content) {
    cleanup_json(json);
    cleanup_response(response);
    return false;
}

// 실제 로직
```

### 공통 로직 추출
- [ ] `src/translator/common/translator_common.c`에 공통 함수 추가
  - `validate_translation_response()`
  - `parse_api_error()`
  - `cleanup_translation_resources()`

## Priority 3: 보안 수정 (CRITICAL) 🔒

### 🚨 즉시 수정 필요
**예상 시간**: 2-3시간 → **실제**: 1.5시간 ✅

- [x] **system() 호출 제거 - AUR 빌드 땜빵 근본 해결**
  - **위치**: 3개 translator 파일
  - **예상 시간**: 30분
  - **배경**: 커밋 d7b1fa2에서 system() 반환값 체크로 땜빵 처리 (CI 빌드 경고)
  - **근본 해결**: file_utils.c의 ensure_cache_directories() 사용

  **수정 대상**:
  - openai_translator.c:495
  - gemini_translator.c:494
  - claude_translator.c:484

  ```c
  // Before: system() 호출 (AUR 빌드 땜빵)
  if (system("mkdir -p /tmp/wshowlyrics/translated") != 0) {
      log_warn("translator: Failed to create cache directory");
  }

  // After: 기존 mkdir_p() 활용
  // file_utils.h에 ensure_cache_directories() 이미 있음
  if (!ensure_cache_directories()) {
      log_error("translator: Failed to create cache directories");
      return false;
  }
  ```

- [x] **pango_utils.c:36 - 포맷 스트링 보안** (c:S5281)
  ```c
  // Before: get_text_size(..., const char *fmt, ...)
  // After: get_text_size(..., const char *text)
  // 가변 인자 제거로 포맷 스트링 보안 문제 근본 해결
  ```

- [x] **pango_utils.c:31 - 가변 인자 제거** (c:S923)
  - 현재: `void create_pango_layout(cairo_t *cr, ...)`
  - 목표: 고정 파라미터 또는 구조체 사용
  ```c
  struct pango_layout_params {
      const char *text;
      const char *font_desc;
      int alignment;
  };
  void create_pango_layout(cairo_t *cr, struct pango_layout_params *params);
  ```

- [x] **parser_utils.c:151 - 0바이트 malloc** (c:S5488)
  ```c
  // Before: malloc(size + 1) without validation
  // After: 파일 크기 검증 추가
  if (size < 0) return false;  // ftell 실패
  if (size == 0) return false; // 빈 파일은 유효하지 않음
  char *content = malloc(size + 1);
  ```

## Priority 4: 함수 파라미터 감소 (MAJOR) 📦

**공통 문제**: Rendering 함수들의 과다 파라미터 (7개 초과)
**이슈**: 4개 (c:S107)
**예상 시간**: 2-3시간

### 전략: 구조체로 파라미터 묶기

- [ ] **word_render.c:318 - 파라미터 10개 → 구조체**
- [ ] **ruby_render.c:146 - 파라미터 9개 → 구조체**
- [ ] **word_render.c:78 - 파라미터 8개 → 구조체**
- [ ] **render_common.c:42 - 파라미터 8개 → 구조체**

### 리팩토링 방법
```c
// Before: 10개 파라미터
void render_word(
    cairo_t *cr,
    struct word_segment *seg,
    int x, int y,
    int width, int height,
    double opacity,
    PangoFontDescription *font_desc,
    const char *color,
    bool show_ruby
);

// After: 구조체 사용
struct render_params {
    cairo_t *cr;
    struct word_segment *seg;
    struct {
        int x, y, width, height;
    } rect;
    double opacity;
    PangoFontDescription *font_desc;
    const char *color;
    bool show_ruby;
};

void render_word(struct render_params *params);
```

### 수정 파일
- [ ] `src/utils/render/render_params.h` (신규)
  - `struct render_params` 정의
  - `struct ruby_render_params` 정의
  - `struct word_render_params` 정의

- [ ] `src/utils/render/word_render.c` - 구조체 적용
- [ ] `src/utils/render/ruby_render.c` - 구조체 적용
- [ ] `src/utils/render/render_common.c` - 구조체 적용

## Priority 5: 코드 정리 (MAJOR) 🧹

**예상 시간**: 2-4시간

### 사용되지 않는 파라미터 제거 (c:S1172)

- [ ] **render_common.c:55, 43 - max_ruby_height 파라미터**
  ```c
  // Option 1: 제거
  void render_plain_text(cairo_t *cr, const char *text, int x, int y);

  // Option 2: [[maybe_unused]] 표시 (나중에 사용 예정인 경우)
  void render_plain_text(
      cairo_t *cr,
      const char *text,
      int x, int y,
      [[maybe_unused]] int max_ruby_height
  );
  ```

- [ ] **shm.c:61 - wl_buffer 파라미터**
  - Wayland callback 시그니처이므로 [[maybe_unused]] 표시

### 사용되지 않는 변수 제거 (c:S1854)

- [ ] **config.c:1005 - offset 변수**
- [ ] **srt_parser.c:214 - next_line 변수**

### 병합 가능한 if문 통합 (c:S1066)

총 9개의 중첩 if문 병합

- [ ] **lyrics_manager.c**: 라인 268, 270, 35
  ```c
  // Before:
  if (data) {
      if (data->lines) {
          // 로직
      }
  }

  // After:
  if (data && data->lines) {
      // 로직
  }
  ```

- [ ] **config.c**: 라인 910, 660
- [ ] **system_tray.c**: 라인 408
- [ ] **shm.c**: 라인 137
- [ ] **srt_parser.c**: 라인 171

### 기타

- [ ] **word_render.c:31 - 중첩 삼항 연산자** (c:S3358)
  ```c
  // Before: 중첩 삼항 연산자
  int value = (a ? (b ? c : d) : e);

  // After: 명시적 if문
  int value;
  if (a) {
      value = b ? c : d;
  } else {
      value = e;
  }
  ```

- [ ] **lang_detect.c:12 - #include 위치** (c:S954)
  - 조건부 #include를 파일 상단으로 이동

## 구현 단계

### Phase 1: 보안 수정 (즉시) 🚨
**예상**: 2-3시간 → **완료** ✅
- [x] system() 호출 제거 (3개 translator 파일)
- [x] pango_utils.c 포맷 스트링 + 가변 인자
- [x] parser_utils.c 0바이트 malloc

### Phase 2: 복잡도 최우선 (config.c) ✅
**예상**: 6-8시간 → **실제**: 2시간 ✅
- [x] config.c:219 함수 분리 (복잡도 129 → 25 이하)
  - parse_rate_limit_value() 추출
  - parse_display_section() 추출
  - parse_lyrics_section() 추출
  - parse_translation_section() 추출
  - parse_deprecated_deepl_section() 추출
- [x] config.c:824 검증 로직 리팩토링 (복잡도 84 → 25 이하)
  - find_example_config_path() 추출
  - find_unknown_config_entries() 추출
  - find_missing_config_entries() 추출
  - display_unknown_keys_warning() 추출
  - display_missing_keys_warning() 추출
- [x] 빌드 검증 완료

### Phase 3: Provider 및 Parser 복잡도 ✅
**예상**: 5-7시간 → **실제**: 1시간 ✅
- [x] lrclib_provider.c 복잡도 개선 (54 → <25)
  - build_search_request_url() 추출
  - perform_lrclib_request() 추출
  - find_best_match_in_results() 추출
  - extract_metadata_from_result() 추출
- [x] system_tray.c 복잡도 개선 (49 → <25)
  - cache_current_artwork() 추출
  - load_cached_album_art() 추출
  - try_mpris_artwork() 추출
  - try_itunes_artwork() 추출
- [x] lrcx_parser.c 복잡도 개선 (48 → <25)
  - ensure_and_append_to_full_text() 추출
  - add_parsed_word_segments() 추출
  - add_raw_text_segment() 추출
- [x] 빌드 검증 완료

### Phase 4: Translation 파일 일괄 수정
**예상**: 4-6시간
- [ ] 4개 translator 파일 중첩 깊이 해소
- [ ] 공통 로직 translator_common.c로 추출
- [ ] deepl_translator.c 복잡도 개선 (36)

### Phase 5: Rendering 파라미터 리팩토링
**예상**: 2-3시간
- [ ] render_params.h 구조체 정의
- [ ] word_render.c, ruby_render.c, render_common.c 적용

### Phase 6: 코드 정리
**예상**: 2-4시간
- [ ] 사용되지 않는 파라미터/변수 제거
- [ ] 병합 가능한 if문 통합 (9개)
- [ ] 기타 MAJOR 이슈 해결

## 테스트 전략

### 각 Phase별 테스트
1. 빌드 성공 확인: `meson compile -C build`
2. 기능 회귀 테스트: 설정 로드, 가사 표시, 번역 동작
3. SonarCloud 재검사: 해결된 이슈 확인

### 통합 테스트
- [ ] 전체 워크플로우 테스트
  - Config 파일 로드
  - MPRIS 연동
  - 로컬/온라인 가사 검색
  - 번역 (4개 provider)
  - Wayland 렌더링
  - System tray 표시

## 예상 개선 효과

| Phase | 해결 이슈 | SonarCloud 점수 개선 |
|-------|----------|-------------------|
| Phase 1 | 3개 CRITICAL | 보안 취약점 제거 |
| Phase 2 | 5개 CRITICAL + 3개 MAJOR | A등급 진입 가능 |
| Phase 3 | 6개 CRITICAL | 복잡도 50% 감소 |
| Phase 4 | 5개 CRITICAL | 중첩 문제 완전 해소 |
| Phase 5 | 4개 MAJOR | API 개선 |
| Phase 6 | 13개 MAJOR | 코드 품질 향상 |

**총 예상**: 34+ 이슈 해결, SonarCloud A등급 달성

## 참고 자료

- **SonarCloud Dashboard**: https://sonarcloud.io/project/overview?id=unstable-code_lyrics
- **SonarCloud Issues API**: https://sonarcloud.io/api/issues/search?componentKeys=unstable-code_lyrics
- **C++ Core Guidelines**: https://isocpp.github.io/CppCoreGuidelines/
- **Cognitive Complexity**: https://www.sonarsource.com/docs/CognitiveComplexity.pdf

## 커밋 메시지 형식

각 Phase별 커밋 시 다음 형식 사용:

```
refactor: [Phase 번호] [요약 제목]

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

## 상태

- **생성일**: 2025-12-19
- **현재 Phase**: 미시작
- **우선순위**: High (코드 품질 및 보안)
- **SonarCloud 현재 등급**: (확인 필요)
- **목표 등급**: A
