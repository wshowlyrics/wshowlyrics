# TODO: SonarCloud Code Quality Improvements

## 현재 상태 (2025-12-19)

### ✅ 완료된 작업 (Phase 1-7)
- **Security Hotspots**: 100% Reviewed (107개 전체 처리)
- **Phase 1-7 완료**: 보안, 복잡도, 파라미터, 중복 제거
- **총 개선**: 75개 이슈 해결 (247개 → 172개)
- **전체 등급**: ⭐ A등급 달성 (Maintainability, Reliability, Security)
- **Bugs**: 0개
- **Vulnerabilities**: 0개

### 📊 남은 작업
- **HIGH Severity**: 57개 (브라우저 확인 기준)
- **총 예상 시간**: ~16시간 (968분)

---

## Phase 8: HIGH Severity 이슈 해결 (57개)

### Priority 1: 최우선 복잡도 개선 (3개) 🔥
**예상 시간**: 3h 36min

#### 1. main.c:21 - 복잡도 106 → 25 이하
**이슈 키**: AZsw3M-lHpp0TbwUWQbn
**Rule**: c:S3776 (Cognitive Complexity)
**예상 시간**: 1h 26min

**현재 상태**:
- 단일 main() 함수에서 모든 로직 처리
- 초기화, 이벤트 루프, 렌더링 혼재

**리팩토링 방법**:
```c
// 로직 분리
initialize_application()
setup_wayland_and_mpris()
main_event_loop()
cleanup_application()
```

---

#### 2. word_render.c:86 - 복잡도 94 → 25 이하
**이슈 키**: AZs1J-D1HL_6RtSCbLsS
**Rule**: c:S3776 (Cognitive Complexity)
**예상 시간**: 1h 14min

**현재 상태**:
- 카라오케 렌더링 로직이 단일 함수에 집중
- 타이밍 계산, 색상 처리, 세그먼트 렌더링 혼재

**리팩토링 방법**:
```c
// 기능별 분리
calculate_segment_timing()
determine_segment_color()
render_segment_with_timing()
handle_unfill_effect()
```

---

#### 3. lyrics_provider.c:559 - 복잡도 76 → 25 이하
**이슈 키**: AZsw3M9EHpp0TbwUWQaM
**Rule**: c:S3776 (Cognitive Complexity)
**예상 시간**: 56min

**현재 상태**:
- 로컬 가사 파일 검색 로직이 복잡
- 11개 경로 순회 + 포맷 체크 혼재

**리팩토링 방법**:
```c
// 경로 검색 전략 패턴
search_in_music_directory()
search_in_current_directory()
search_in_standard_locations()
```

---

### Priority 2: 중간 복잡도 개선 (7개) ⚠️
**예상 시간**: 2h 49min

#### 4. lrc_parser.c:11 - 복잡도 84 → 25 이하
**이슈 키**: AZsw3M7UHpp0TbwUWQYD
**Rule**: c:S3776
**예상 시간**: 1h 4min

**리팩토링**:
- LRC 파싱 로직 단계별 분리
- 타임스탬프 파싱, 가사 추출을 별도 함수로

---

#### 5. rendering_manager.c:43 - 복잡도 56 → 25 이하
**이슈 키**: AZsw3M-THpp0TbwUWQbc
**Rule**: c:S3776
**예상 시간**: 36min

**리팩토링**:
- 포맷별 렌더링 분기를 Strategy 패턴으로
- `render_lrcx()`, `render_lrc()`, `render_srt()` 분리

---

#### 6. lyrics_manager.c:250 - 복잡도 50 → 25 이하
**이슈 키**: AZsw3M-5Hpp0TbwUWQb6
**Rule**: c:S3776
**예상 시간**: 30min

---

#### 7. lrcx_parser.c:13 - 복잡도 44 → 25 이하
**이슈 키**: AZsw3M7KHpp0TbwUWQXx
**Rule**: c:S3776
**예상 시간**: 24min

---

#### 8. file_monitor.c:50 - 복잡도 42 → 25 이하
**이슈 키**: AZsw3M96Hpp0TbwUWQbQ
**Rule**: c:S3776
**예상 시간**: 22min

---

#### 9. srt_parser.c:114 - 복잡도 40 → 25 이하
**이슈 키**: AZsw3M7fHpp0TbwUWQYS
**Rule**: c:S3776
**예상 시간**: 20min

---

#### 10. translator_common.c:855 - 복잡도 38 → 25 이하
**이슈 키**: AZs1aCa59_pDiePoGieh
**Rule**: c:S3776
**예상 시간**: 18min

---

### Priority 3: 소규모 복잡도 개선 (13개) 📝
**예상 시간**: 2h 15min

#### Config.c (4개)
11. **config.c:697** - 복잡도 36 → 25 (16min) | AZsw3M9THpp0TbwUWQas
12. **config.c:827** - 복잡도 33 → 25 (13min) | AZsyKnUoJ8Ufg4bNhRkS
13. **config.c:1059** - 복잡도 28 → 25 (8min) | AZsw3M9THpp0TbwUWQa1

#### Ruby Render (2개)
14. **ruby_render.c:192** - 복잡도 34 → 25 (14min) | AZs1J-BMHL_6RtSCbLsR
15. **ruby_render.c:44** - 복잡도 30 → 25 (10min) | AZs1J-BMHL_6RtSCbLsQ

#### Lyrics Manager (1개)
16. **lyrics_manager.c:54** - 복잡도 33 → 25 (13min) | AZsw3M-5Hpp0TbwUWQb1

#### Parser Utils (3개)
17. **parser_utils.c:410** - 복잡도 57 → 25 (37min) | AZsw3M7sHpp0TbwUWQYe
18. **parser_utils.c:291** - 복잡도 28 → 25 (8min) | AZsxJYoWqE2ErzW5x-dz

#### Provider (1개)
19. **lyrics_provider.c:203** - 복잡도 28 → 25 (8min) | AZsw3M9EHpp0TbwUWQaB

#### Word Render (1개)
20. **word_render.c:281** - 복잡도 26 → 25 (6min) | AZs1J-D1HL_6RtSCbLsT

#### File Utils (1개)
21. **file_utils.c:88** - Remove ellipsis notation (30min) | AZsw3M62Hpp0TbwUWQXn

---

### Priority 4: 중첩 깊이 해소 (32개) 🔧
**예상 시간**: 5h 20min

**Rule**: c:S134 (Nesting Depth > 3)
**공통 해결 방법**: Early return, Guard clauses

#### Config.c (8개)
22. Line 374 - AZsw3M9THpp0TbwUWQaU (10min)
23. Line 649 - AZsw3M9THpp0TbwUWQan (10min)
24. Line 729 - AZsw3M9THpp0TbwUWQat (10min)
25. Line 737 - AZsw3M9THpp0TbwUWQau (10min)
26. Line 841 - AZsw3M9THpp0TbwUWQaw (10min)
27. Line 865 - AZs1J-IlHL_6RtSCbLsZ (10min)
28. Line 901 - AZsw3M9THpp0TbwUWQay (10min)
29. Line 1098 - AZsxJYtZqE2ErzW5x-d4 (10min)

#### LRC Parser (5개)
30. Line 43 - AZsw3M7UHpp0TbwUWQYE (10min)
31. Line 52 - AZsw3M7UHpp0TbwUWQYF (10min)
32. Line 59 - AZsw3M7UHpp0TbwUWQYG (10min)
33. Line 73 - AZsw3M7UHpp0TbwUWQYH (10min)
34. Line 130 - AZsw3M7UHpp0TbwUWQYI (10min)

#### LRCX Parser (4개)
35. Line 49 - AZsw3M7KHpp0TbwUWQXy (10min)
36. Line 59 - AZsw3M7KHpp0TbwUWQXz (10min)
37. Line 68 - AZsw3M7KHpp0TbwUWQX0 (10min)
38. Line 73 - AZsw3M7KHpp0TbwUWQX1 (10min)

#### Parser Utils (4개)
39. Line 31 - AZsw3M7sHpp0TbwUWQYZ (10min)
40. Line 62 - AZsw3M7sHpp0TbwUWQYa (10min)
41. Line 489 - AZsw3M7sHpp0TbwUWQYf (10min)
42. Line 536 - AZsw3M7sHpp0TbwUWQYg (10min)

#### Main.c (2개)
43. Line 58 - AZsw3M-lHpp0TbwUWQbo (10min)
44. Line 355 - AZsw3M-lHpp0TbwUWQbp (10min)

#### Rendering Manager (2개)
45. Line 136 - AZsw3M-THpp0TbwUWQbd (10min)
46. Line 180 - AZsw3M-THpp0TbwUWQbe (10min)

#### Lyrics Provider (2개)
47. Line 597 - AZsw3M9EHpp0TbwUWQaN (10min)
48. Line 643 - AZsw3M9EHpp0TbwUWQaO (10min)

#### SRT Parser (2개)
49. Line 156 - AZsw3M7fHpp0TbwUWQYT (10min)
50. Line 181 - AZsw3M7fHpp0TbwUWQYV (10min)

#### Translator Common (1개)
51. Line 886 - AZs1aCa59_pDiePoGiei (10min)

#### LRCLib Provider (1개)
52. Line 120 - AZs08litjzW8D2rZBAU7 (10min)

#### Ruby Render (1개)
53. Line 243 - AZsw3M5rHpp0TbwUWQWs (10min)

#### Word Render (1개)
54. Line 183 - AZsw3M6BHpp0TbwUWQW8 (10min)

**리팩토링 패턴**:
```c
// Before: 중첩 if
if (a) {
    if (b) {
        if (c) {
            // logic
        }
    }
}

// After: Early return
if (!a) return;
if (!b) return;
if (!c) return;
// logic
```

---

### Priority 5: 보안/품질 이슈 (3개) 🔒
**예상 시간**: 45min

#### 55. parser_utils.c:152 - 0바이트 malloc
**이슈 키**: AZs1J-GZHL_6RtSCbLsX
**Rule**: c:S5488
**예상 시간**: 5min

**수정**:
```c
// 파일 크기 검증 추가
if (size <= 0) {
    log_error("Invalid file size: %ld", size);
    return NULL;
}
char *content = malloc(size + 1);
```

---

#### 56. file_utils.c:88 - Remove ellipsis notation
**이슈 키**: AZsw3M62Hpp0TbwUWQXn
**Rule**: c:S923
**예상 시간**: 30min

**수정**:
- 가변 인자 제거
- 고정 파라미터 또는 구조체 사용

---

#### 57. file_utils.c:95 - format string not literal
**이슈 키**: AZsw3M62Hpp0TbwUWQXk
**Rule**: c:S5281
**예상 시간**: 10min

**수정**:
- 포맷 스트링을 리터럴로 변경
- 또는 적절한 검증 추가

---

## 구현 계획

### Phase 8-1: 최우선 복잡도 (1-3번) 🔥
**예상 시간**: 3h 36min

- [ ] main.c:21 리팩토링 (1h 26min)
- [ ] word_render.c:86 리팩토링 (1h 14min)
- [ ] lyrics_provider.c:559 리팩토링 (56min)
- [ ] 빌드 검증
- [ ] 기능 회귀 테스트

---

### Phase 8-2: 중간 복잡도 (4-10번) ⚠️
**예상 시간**: 2h 49min

- [ ] lrc_parser.c:11 (1h 4min)
- [ ] rendering_manager.c:43 (36min)
- [ ] lyrics_manager.c:250 (30min)
- [ ] lrcx_parser.c:13 (24min)
- [ ] file_monitor.c:50 (22min)
- [ ] srt_parser.c:114 (20min)
- [ ] translator_common.c:855 (18min)
- [ ] 빌드 검증

---

### Phase 8-3: 소규모 복잡도 (11-21번) 📝
**예상 시간**: 2h 15min

- [ ] Config.c 복잡도 3개 (37min)
- [ ] Ruby Render 2개 (24min)
- [ ] Lyrics Manager 1개 (13min)
- [ ] Parser Utils 2개 (45min)
- [ ] Provider 1개 (8min)
- [ ] Word Render 1개 (6min)
- [ ] File Utils 1개 (30min)
- [ ] 빌드 검증

---

### Phase 8-4: 중첩 깊이 일괄 해소 (22-54번) 🔧
**예상 시간**: 5h 20min

- [ ] Config.c 8개 (1h 20min)
- [ ] LRC Parser 5개 (50min)
- [ ] LRCX Parser 4개 (40min)
- [ ] Parser Utils 4개 (40min)
- [ ] Main.c 2개 (20min)
- [ ] Rendering Manager 2개 (20min)
- [ ] Lyrics Provider 2개 (20min)
- [ ] SRT Parser 2개 (20min)
- [ ] 나머지 4개 (40min)
- [ ] 빌드 검증

---

### Phase 8-5: 보안/품질 이슈 (55-57번) 🔒
**예상 시간**: 45min

- [ ] parser_utils.c:152 0바이트 malloc 수정 (5min)
- [ ] file_utils.c:88 ellipsis 제거 (30min)
- [ ] file_utils.c:95 format string 수정 (10min)
- [ ] 빌드 검증

---

## 총 예상 시간

| Phase | 작업 | 이슈 수 | 예상 시간 |
|-------|------|---------|----------|
| 8-1 | 최우선 복잡도 | 3 | 3h 36min |
| 8-2 | 중간 복잡도 | 7 | 2h 49min |
| 8-3 | 소규모 복잡도 | 11 | 2h 15min |
| 8-4 | 중첩 깊이 | 32 | 5h 20min |
| 8-5 | 보안/품질 | 3 | 45min |
| **합계** | | **57** | **14h 45min** |

**버퍼 포함**: ~16-18시간 (테스트 및 디버깅)

---

## 예상 개선 효과

### Before (현재)
- **HIGH Severity**: 57개
- **전체 등급**: A (유지)
- **복잡도**: 일부 함수 100+ 초과

### After (Phase 8 완료 시)
- **HIGH Severity**: 0개 예상
- **전체 등급**: A (유지)
- **복잡도**: 모든 함수 25 이하
- **유지보수성**: 대폭 향상
- **가독성**: 향상
- **테스트 용이성**: 향상

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

3. **SonarCloud 검증**:
   - 이슈 해결 확인
   - 새로운 이슈 발생 여부

---

## 커밋 메시지 형식

```
refactor: [Phase 8-N] [요약 제목]

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
- **Cognitive Complexity**: https://www.sonarsource.com/docs/CognitiveComplexity.pdf
- **C Refactoring**: https://refactoring.guru/refactoring/catalog

---

## 상태

- **생성일**: 2025-12-19
- **최종 업데이트**: 2025-12-19 21:15
- **현재 Phase**: Phase 8 시작 전
- **완료 Phases**: 1-7 (보안, 복잡도, 파라미터, 중복 제거)
- **남은 이슈**: 57개 HIGH Severity
- **우선순위**: High (코드 품질, 유지보수성)
- **목표**: 모든 HIGH Severity 이슈 해결, A등급 유지
- **예상 소요 시간**: 14h 45min (버퍼 포함 16-18시간)
