# TODO: SonarCloud Code Quality Improvements

## 현재 상태 (2025-12-30)

### ✅ 완료된 작업 (Phase 1-8)
- **Security Hotspots**: 100% Reviewed (107개 전체 처리)
- **Phase 1-7 완료**: 보안, 복잡도, 파라미터, 중복 제거
- **Phase 8-1 완료**: 복잡도 45-69 (5개 함수)
- **Phase 8-2 완료**: 복잡도 40-57 (7개 함수)
- **Phase 8-3 완료**: 복잡도 26-44 (11개 함수)
- **Phase 8-3.5 완료**: 추가 복잡도 개선 (3개 함수) ✅ **NEW**
- **Phase 8-5 완료**: 보안/품질 이슈 (3개)
- **전체 등급**: ⭐ A등급 달성 (Maintainability, Reliability, Security)
- **Bugs**: 0개
- **Vulnerabilities**: 0개

### 📊 남은 작업 (2025-12-30 기준)
- **총 미해결 이슈**: 208개 (Phase 8-3.5 완료로 3개 추가 감소)
  - **BLOCKER**: 0개
  - **CRITICAL**: 59개 (C 코드만, Python 제외) - 이전 62개에서 3개 추가 감소
  - **MAJOR**: 28개 (C 코드만, Python 제외)
  - **MINOR**: 110개
  - **INFO**: 0개
- **C 코드 우선 이슈**: 87개 (CRITICAL 59 + MAJOR 28)
- **총 예상 시간**: ~22시간 (버퍼 포함 24-26시간)

---

## Phase 8: CRITICAL Severity 이슈 해결 (73개 C 코드)

### Priority 1: 최우선 복잡도 개선 (복잡도 70+, 5개) 🔥
**예상 시간**: 6h 23min

#### 1. main.c:37 - 복잡도 142 → 25 이하 ⚠️ 최우선
**이슈 키**: AZsw3M-lHpp0TbwUWQbn
**Rule**: c:S3776 (Cognitive Complexity)
**예상 시간**: 2h 2min

**현재 상태**:
- 단일 main() 함수에서 모든 로직 처리
- 초기화, 이벤트 루프, 렌더링, 정리 혼재

**리팩토링 방법**:
```c
// 주요 로직 분리
- initialize_application()      // 앱 초기화
- setup_wayland_compositor()    // Wayland 설정
- setup_mpris_connection()      // MPRIS 연결
- main_event_loop()             // 메인 이벤트 루프
- cleanup_resources()           // 리소스 정리
```

---

#### 2. word_render.c:86 - 복잡도 94 → 25 이하
**이슈 키**: AZs1J-D1HL_6RtSCbLsS
**Rule**: c:S3776
**예상 시간**: 1h 14min

**현재 상태**:
- 카라오케 렌더링 로직이 단일 함수에 집중
- 타이밍 계산, 색상 처리, 세그먼트 렌더링 혼재

**리팩토링 방법**:
```c
// 기능별 분리
- calculate_word_timing()       // 단어 타이밍 계산
- determine_fill_color()        // 채우기 색상 결정
- render_karaoke_segment()      // 세그먼트 렌더링
- handle_unfill_effect()        // unfill 효과 처리
```

---

#### 3. lrc_parser.c:11 - 복잡도 84 → 25 이하
**이슈 키**: AZsw3M7UHpp0TbwUWQYD
**Rule**: c:S3776
**예상 시간**: 1h 4min

**리팩토링**:
- LRC 파싱 로직 단계별 분리
- 타임스탬프 파싱, 가사 추출을 별도 함수로

---

#### 4. lyrics_provider.c:556 - 복잡도 77 → 25 이하
**이슈 키**: AZsw3M9EHpp0TbwUWQaM
**Rule**: c:S3776
**예상 시간**: 57min

**현재 상태**:
- 로컬 가사 파일 검색 로직이 복잡
- 11개 경로 순회 + 포맷 체크 혼재

**리팩토링 방법**:
```c
// 경로 검색 전략 패턴
- search_in_music_directory()
- search_in_current_directory()
- search_in_standard_locations()
```

---

#### 5. mpris.c:421 - 복잡도 70 → 25 이하
**이슈 키**: AZtJVZMwABppZyGqgPwy
**Rule**: c:S3776
**예상 시간**: 50min

**리팩토링**:
- MPRIS 신호 처리 로직 분리
- 플레이어 상태별 핸들러 분리

---

### Priority 2: 중간 복잡도 개선 (복잡도 40-69, 6개) ✅ **완료**
**예상 시간**: 2h 58min
**실제 시간**: ~3h 30min

#### 6. ~~mpris.c:564 - 복잡도 54 → 25 이하~~ ✅ **완료 (커밋: 43ab00f)**
**이슈 키**: AZtFyzUj-JPgH9NPV58t
**예상 시간**: 34min
- `find_best_player()` 156줄 → 39줄 (117줄 절감)
- 헬퍼 추출: `is_player_playing()`, `find_first_playing_player()`, `find_preferred_player()`

#### 7. ~~parser_utils.c:410 - 복잡도 57 → 25 이하~~ ✅ **완료 (커밋: c8b40dc)**
**이슈 키**: AZsw3M7sHpp0TbwUWQYe
**예상 시간**: 37min
- `parse_ruby_segments()` 191줄 → 92줄 (99줄 절감)
- 헬퍼 추출: `create_and_append_segment()`, `handle_translation()`, `handle_newline()`, `handle_ruby_annotation()`

#### 8. ~~lyrics_manager.c:290 - 복잡도 50 → 25 이하~~ ✅ **완료 (커밋: c8b40dc)**
**이슈 키**: AZsw3M-5Hpp0TbwUWQb6
**예상 시간**: 30min
- `lyrics_manager_update_current_line()` 116줄 → 43줄 (73줄 절감)
- 헬퍼 추출: `is_whitespace_only()`, `calculate_line_index()`, `handle_line_changed()`

#### 9. ~~parser_utils.c:8 - 복잡도 50 → 25 이하~~ ✅ **완료 (커밋: c8b40dc)**
**이슈 키**: AZsw3M7sHpp0TbwUWQYY
**예상 시간**: 30min
- `parse_lrc_timestamp_ex()` 73줄 → 32줄 (41줄 절감)
- 헬퍼 추출: `find_end_after_bracket()`, `parse_timestamp_format()`

#### 10. ~~lyrics_manager.c:54 - 복잡도 45 → 25 이하~~ ✅ **완료 (커밋: 43ab00f)**
**이슈 키**: AZsw3M-5Hpp0TbwUWQb1
**예상 시간**: 25min
- `lyrics_manager_update_track_info()` 118줄 → 22줄 (96줄 절감)
- 헬퍼 추출: `cancel_and_wait_translation()`, `handle_no_player_found()`, `detect_track_change()`, `handle_track_changed()`

#### 11. ~~lrcx_parser.c:13 - 복잡도 44 → 25 이하~~ ✅ **완료 (커밋: c8b40dc)**
**이슈 키**: AZsw3M7KHpp0TbwUWQXx
**예상 시간**: 24min
- `parse_first_text_segment()` 77줄 → 33줄 (44줄 절감)
- 헬퍼 추출: `find_first_text_range()`, `build_full_text_from_segments()`

**Phase 8-2 총계**:
- 7개 함수 리팩토링
- 827줄 → 305줄 (522줄 절감, 63% 감소)
- 커밋: 43ab00f (3개), c8b40dc (4개)

---

### Priority 3: 소규모 복잡도 개선 (복잡도 26-44, 11개) ✅ **완료**
**예상 시간**: 2h 59min | **실제 시간**: ~3h

#### 12. ~~file_monitor.c:50 - 복잡도 42 → 25~~ ✅ **완료 (커밋: 8da3a3b)**
**이슈 키**: AZsw3M96Hpp0TbwUWQbQ
- `file_monitor_reload_config()` 113줄 → 37줄 (67% 감소)
- 헬퍼 추출: `has_string_changed()`, `update_state_colors()`, `check_translation_config_changed()`, `check_font_changed()`, `check_layout_changed()`

#### 13. ~~srt_parser.c:114 - 복잡도 40 → 25~~ ✅ **완료 (커밋: 8da3a3b)**
**이슈 키**: AZsw3M7fHpp0TbwUWQYS
- `srt_parse_string()` 105줄 → 88줄 (16% 감소)
- 헬퍼 추출: `should_skip_line()`, `try_parse_timestamp()`, `create_and_add_subtitle()`

#### 14. ~~config.c:762 - 복잡도 36 → 25~~ ✅ **완료 (커밋: 8da3a3b)**
**이슈 키**: AZsw3M-3THpp0TbwUWQbn
- `validate_config_path()` 68줄 → 12줄 (82% 감소)
- 헬퍼 추출: `resolve_path_or_directory()`, `is_path_in_safe_location()`

#### 15. ~~ruby_render.c:192 - 복잡도 34 → 25~~ ✅ **완료 (커밋: 8da3a3b)**
**이슈 키**: AZs1J-BMHL_6RtSCbLsR
- `render_ruby_segments_with_translation()` 118줄 → 67줄 (43% 감소)
- 헬퍼 추출: `render_original_text()`, `render_translation_text()`, `calculate_total_height()`

#### 16. ~~config.c:1124 - 복잡도 32 → 25~~ ✅ **완료 (커밋: 439604f)**
**이슈 키**: AZsw3M9THpp0TbwUWQa1
- `config_load_with_fallback()` 81줄 → 24줄 (70% 감소)
- 헬퍼 추출: `create_config_directory()`, `copy_system_config_to_user()`, `try_load_config_from_path()`

#### 17. ~~ruby_render.c:44 - 복잡도 30 → 25~~ ✅ **완료 (커밋: 439604f)**
**이슈 키**: AZs1J-BMHL_6RtSCbLsQ
- `render_ruby_segments()` 130줄 → 91줄 (30% 감소)
- 헬퍼 추출: `is_translation_segment()`, `update_total_width()`, `render_translation_segment()`, `handle_newline_segment()`

#### 18. ~~rendering_manager.c:40 - 복잡도 30 → 25~~ ✅ **완료 (커밋: 439604f)**
**이슈 키**: AZtgjSRWhSZXhd8a4uzp
- `render_offset_bar()` 92줄 → 49줄 (47% 감소)
- 헬퍼 추출: `render_single_bar()`, `render_same_sign_bars()`, `render_opposite_sign_bar()`

#### 19. ~~parser_utils.c:291 - 복잡도 28 → 25~~ ✅ **완료 (커밋: 439604f)**
**이슈 키**: AZsxJYoWqE2ErzW5x-dz
- `find_word_start()` 46줄 → 11줄 (76% 감소)
- 헬퍼 추출: `move_back_one_utf8_char()`, `find_space_boundary()`, `find_kanji_boundary()`

#### 20. ~~lyrics_provider.c:200 - 복잡도 28 → 25~~ ✅ **완료 (커밋: 439604f)**
**이슈 키**: AZsw3M9EHpp0TbwUWQaB
- `get_extension_priority()` 70줄 → 7줄 (90% 감소)
- 헬퍼 추출: `create_default_extensions()`, `parse_custom_extensions()`

#### 21. ~~mpris.c:323 - 복잡도 28 → 25~~ ✅ **완료 (커밋: 43ab00f)**
**이슈 키**: AZthAcXSCKz1u_SNSw_j

#### 22. ~~system_tray.c:411 - 복잡도 44 → 25~~ ✅ **완료 (커밋: 439604f)**
**이슈 키**: AZtaZrqUhSZXhd8aT109
- `system_tray_send_notification()` 93줄 → 43줄 (54% 감소)
- 헬퍼 추출: `build_notification_body()`, `create_badged_icon()`, `prepare_notification_icon()`
- **최고 복잡도 함수 (44)** 성공적으로 리팩토링

#### 23. ~~word_render.c:281 - 복잡도 26 → 25~~ ✅ **완료 (커밋: 439604f)**
**이슈 키**: AZs1J-D1HL_6RtSCbLsT
- `render_karaoke_multiline()` 112줄 → 50줄 (55% 감소)
- 헬퍼 추출: `render_context_line()`, `render_main_karaoke_line()`

**Phase 8-3 총계**:
- 11개 함수 리팩토링 (복잡도 26-44)
- 30개 헬퍼 함수 추가
- 1028줄 → 479줄 (549줄 절감, 53% 감소)
- 커밋: 8da3a3b (4개), 439604f (7개)
- 모든 함수 복잡도 25 이하로 감소 달성

---

### Priority 3.5: 추가 복잡도 개선 (3개) ✅ **완료**
**예상 시간**: 1h 30min | **실제 시간**: ~1h 30min

이전 리팩토링 이후 발견된 추가 복잡도 이슈 처리:

#### 24. ~~translator_common.c:1014 - 복잡도 38 → 25~~ ✅ **완료 (커밋: 05f93a9)**
**함수**: `json_extract_text_by_path()`
- 100줄 → 30줄 (while 루프 단순화, 70% 감소)
- 헬퍼 추출: `handle_array_access()`, `handle_object_field()`
- JSON 경로 파싱 로직을 array/object 처리로 분리

#### 25. ~~translator_common.c:815 - 복잡도 26 → 25~~ ✅ **완료 (커밋: 05f93a9)**
**함수**: `translator_perform_http_request()`
- 110줄 → 40줄 (retry 루프 단순화, 64% 감소)
- 헬퍼 추출: `setup_translator_curl_request()`, `handle_translator_http_response()`
- CURL 설정 및 HTTP 응답 처리 분리

#### 26. ~~parser_utils.c:624 - 복잡도 36 → 25~~ ✅ **완료 (커밋: 05f93a9)**
**함수**: `parse_ruby_segments()`
- 97줄 → 30줄 (파싱 루프 단순화, 69% 감소)
- 헬퍼 추출: `handle_parsing_failure_and_continue()`, `finalize_ruby_segments()`
- 에러 처리 패턴 통합 및 마무리 로직 분리

**Phase 8-3.5 총계**:
- 3개 함수 리팩토링 (복잡도 26-38)
- 6개 헬퍼 함수 추가
- 307줄 → 100줄 (207줄 절감, 67% 감소)
- 커밋: 05f93a9
- 중복 에러 처리 패턴 제거

---

### Priority 4: 중첩 깊이 해소 (46개) 🔧
**예상 시간**: 7h 40min

**Rule**: c:S134 (Nesting Depth > 3)
**공통 해결 방법**: Early return, Guard clauses

#### MPRIS (8개) - 1h 20min
27. Line 397 - AZthAcXSCKz1u_SNSw_k (10min)
28. Line 604 - AZthAcXSCKz1u_SNSw_l (10min)
29. Line 694 - AZthAcXSCKz1u_SNSw_m (10min)
30. Line 478 - AZtJVZMwABppZyGqgPwz (10min)
31. Line 540 - AZtJVZMwABppZyGqgPw0 (10min)
32. Line 544 - AZtJVZMwABppZyGqgPw1 (10min)
33. Line 667 - AZtFyzUj-JPgH9NPV58u (10min)
34. Line 689 - AZtFyzUj-JPgH9NPV58v (10min)

#### Config.c (8개) - 1h 20min
35. Line 432 - AZsw3M9THpp0TbwUWQaU (10min)
36. Line 714 - AZsw3M9THpp0TbwUWQan (10min)
37. Line 794 - AZsw3M9THpp0TbwUWQat (10min)
38. Line 802 - AZsw3M9THpp0TbwUWQau (10min)
39. Line 906 - AZsw3M9THpp0TbwUWQaw (10min)
40. Line 930 - AZs1J-IlHL_6RtSCbLsZ (10min)
41. Line 966 - AZsw3M9THpp0TbwUWQay (10min)
42. Line 1166 - AZsxJYtZqE2ErzW5x-d4 (10min)

#### LRC Parser (5개) - 50min
43. Line 43 - AZsw3M7UHpp0TbwUWQYE (10min)
44. Line 52 - AZsw3M7UHpp0TbwUWQYF (10min)
45. Line 59 - AZsw3M7UHpp0TbwUWQYG (10min)
46. Line 73 - AZsw3M7UHpp0TbwUWQYH (10min)
47. Line 130 - AZsw3M7UHpp0TbwUWQYI (10min)

#### LRCX Parser (4개) - 40min
48. Line 49 - AZsw3M7KHpp0TbwUWQXy (10min)
49. Line 59 - AZsw3M7KHpp0TbwUWQXz (10min)
50. Line 68 - AZsw3M7KHpp0TbwUWQX0 (10min)
51. Line 73 - AZsw3M7KHpp0TbwUWQX1 (10min)

#### Parser Utils (4개) - 40min
52. Line 31 - AZsw3M7sHpp0TbwUWQYZ (10min)
53. Line 62 - AZsw3M7sHpp0TbwUWQYa (10min)
54. Line 489 - AZsw3M7sHpp0TbwUWQYf (10min)
55. Line 536 - AZsw3M7sHpp0TbwUWQYg (10min)

#### Main.c (3개) - 30min
56. Line 76 - AZs-_pShEZ4CGuQgOkex (10min)
57. Line 88 - AZs-_pShEZ4CGuQgOkey (10min)
58. Line 437 - AZsw3M-lHpp0TbwUWQbp (10min)

#### Rendering Manager (2개) - 20min
59. Line 242 - AZsw3M-THpp0TbwUWQbd (10min)
60. Line 290 - AZtgsuFs5tzUgqrsL2T2 (10min)

#### Lyrics Provider (2개) - 20min
61. Line 593 - AZtgYEJEzbfTxQQNCPWG (10min)
62. Line 641 - AZsw3M9EHpp0TbwUWQaN (10min)

#### SRT Parser (2개) - 20min
63. Line 156 - AZsw3M7fHpp0TbwUWQYT (10min)
64. Line 181 - AZsw3M7fHpp0TbwUWQYV (10min)

#### File Utils (2개) - 20min
65. Line 344 - AZtLpiaozZ4cBjTLQUYE (10min)
66. Line 480 - AZtLpiaozZ4cBjTLQUYG (10min)

#### 기타 (6개) - 1h
67. word_render.c:183 - AZsw3M6BHpp0TbwUWQW8 (10min)
68. ruby_render.c:243 - AZsw3M5rHpp0TbwUWQWs (10min)
69. system_tray.c:460 - AZtaZrqUhSZXhd8aT10- (10min)
70. translator_common.c:968 - AZs1aCa59_pDiePoGiei (10min)
71. lock_file.c:47 - AZs6Yry3l5_7WeqXAcGN (10min)
72. lrclib_provider.c:125 - AZs08litjzW8D2rZBAU7 (10min)

**리팩토링 패턴**:
```c
// Before: 중첩 if
if (a) {
    if (b) {
        if (c) {
            if (d) {
                // logic
            }
        }
    }
}

// After: Early return
if (!a) return;
if (!b) return;
if (!c) return;
if (!d) return;
// logic
```

---

### Priority 5: 보안/품질 이슈 (3개) 🔒 ✅ **완료**
**예상 시간**: 45min

#### 73. ~~parser_utils.c:152 - 0바이트 malloc~~ ✅ **완료**
**이슈 키**: AZs1J-GZHL_6RtSCbLsX
**Rule**: c:S5488
**Severity**: CRITICAL
**예상 시간**: 5min

**수정**: 이미 파일 크기 검증 존재 확인
```c
if (size <= 0) {
    log_error("Invalid file size: %ld", size);
    fclose(fp);
    return NULL;
}
```

---

#### 74. ~~file_utils.c:88 - Remove ellipsis notation~~ ✅ **완료**
**이슈 키**: AZsw3M62Hpp0TbwUWQXn
**Rule**: c:S923
**Severity**: CRITICAL
**예상 시간**: 30min

**수정**: 6개 타입 안전 함수로 대체 (커밋: 73adfae)

---

#### 75. ~~file_utils.c:95 - format string not literal~~ ✅ **완료**
**이슈 키**: AZsw3M62Hpp0TbwUWQXk
**Rule**: c:S5281
**Severity**: CRITICAL
**예상 시간**: 10min

**수정**: 리터럴 포맷 스트링 사용 (커밋: 73adfae)

---

## Phase 9: MAJOR Severity 이슈 해결 (28개, C 코드만)

### Priority 1: 중첩 if 병합 (5개) - 25min
**Rule**: c:S1066 (Merge nested if statements)

76. shm.c:138 - AZsw3M6KHpp0TbwUWQXN (5min)
77. file_utils.c:344 - AZtLpiaozZ4cBjTLQUYF (5min)
78. main.c:356 - AZtGYIOteewIkXJctHjF (5min)
79. system_tray.c:171 - AZs-_pRTEZ4CGuQgOkew (5min)
80. lyrics_provider.c:584 - AZtgYEJEzbfTxQQNCPWH (5min)

**리팩토링**:
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

### Priority 2: 파라미터 제한 초과 (4개) - 1h 20min
**Rule**: c:S107 (Function has too many parameters)

81. lrcx_parser.c:13 - AZsw3M7KHpp0TbwUWQX2 (20min)
82. lrcx_parser.c:176 - AZsw3M7KHpp0TbwUWQX8 (20min)
83. wayland_events.c:115 - AZsw3M-EHpp0TbwUWQbW (20min)
84. dbus_control.c:66 - AZtHSiUNBYCiXr9PEey3 (20min)

**리팩토링**:
```c
// 파라미터 구조체로 그룹화
struct parser_config {
    const char *filename;
    struct lyrics_data *data;
    // ... 기타 파라미터
};
```

---

### Priority 3: 미사용 파라미터 제거 (7개) - 35min
**Rule**: c:S1172 (Remove unused function parameters)

85. parser_utils.c:137 - AZs-_pL4EZ4CGuQgOkel (5min)
86. dbus_control.c:67 - AZtHSiUNBYCiXr9PEey4 (5min)
87. dbus_control.c:68 - AZtHSiUNBYCiXr9PEey5 (5min)
88. dbus_control.c:69 - AZtHSiUNBYCiXr9PEey6 (5min)
89. dbus_control.c:70 - AZtHSiUNBYCiXr9PEey7 (5min)
90. dbus_control.c:157 - AZtHSiUNBYCiXr9PEey8 (5min)
91. dbus_control.c:193 - AZtHSiUNBYCiXr9PEey- (5min)

---

### Priority 4: 중첩 break 감소 (3개) - 1h
**Rule**: c:S924 (Reduce the number of nested 'break' statements)

92. translator_common.c:261 - AZsw3M8OHpp0TbwUWQZF (20min)
93. lrclib_provider.c:108 - AZsw3M8vHpp0TbwUWQZn (20min)
94. mpris.c:478 - AZtJVZMwABppZyGqgPw4 (20min)

---

### Priority 5: 주석 처리된 코드 제거 (3개) - 15min
**Rule**: c:S125 (Remove this commented out code)

95. lyrics_provider.c:423 - AZsw3M9EHpp0TbwUWQaR (5min)
96. lyrics_provider.c:438 - AZsw3M9EHpp0TbwUWQaS (5min)
97. mpris.c:627 - AZtFyzUj-JPgH9NPV580 (5min)

---

### Priority 6: 기타 MAJOR 이슈 (6개) - 2h

98. lang_detect.c:12 - Include directive placement (10min) | c:S954
99. state_helpers.c:24 - Duplicate code blocks (10min) | c:S1871
100. main.h:51 - Structure field limit (1h) | c:S1820
101. file_utils.c:526 - Obsolete function usage (30min) | c:S1911
102. (기타 MAJOR 이슈 2개 추가 가능)

---

## 구현 계획

### Phase 8-1: 최우선 복잡도 (1-5번) 🔥 ✅ **완료**
**예상 시간**: 6h 23min
**실제 시간**: ~8h (테스트 포함)
**커밋**: 8d665ab

- [x] main.c:37 리팩토링 (2h 2min) - 최우선
- [x] word_render.c:86 리팩토링 (1h 14min)
- [x] lrc_parser.c:11 리팩토링 (1h 4min)
- [x] lyrics_provider.c:556 리팩토링 (57min)
- [x] mpris.c:421 리팩토링 (50min)
- [x] 빌드 검증
- [x] 기능 회귀 테스트

---

### Phase 8-2: 중간 복잡도 (6-11번) ⚠️ **진행 중**
**예상 시간**: 2h 58min
**실제 시간**: ~1h (3개 완료)
**커밋**: 43ab00f

- [x] mpris.c:564 (34min) - find_best_player (복잡도 54 → 감소)
- [ ] parser_utils.c:410 (37min) - parse_ruby_segments (복잡도 57)
- [ ] lyrics_manager.c:290 (30min) - sync_lyrics_content (복잡도 50)
- [ ] parser_utils.c:8 (30min) - parse_lrc_timestamp_ex (복잡도 50)
- [x] lyrics_manager.c:54 (25min) - lyrics_manager_update_track_info (복잡도 45 → 감소)
- [ ] lrcx_parser.c:13 (24min) - parse_lrcx_file (복잡도 44)
- [x] 빌드 검증

---

### Phase 8-3: 소규모 복잡도 (12-23번) ✅ **완료**
**예상 시간**: 3h 7min | **실제 시간**: ~3h
**커밋**: 8da3a3b, 439604f

- [x] 11개 함수 복잡도 개선
- [x] 빌드 검증

---

### Phase 8-3.5: 추가 복잡도 개선 (24-26번) ✅ **완료**
**예상 시간**: 1h 30min | **실제 시간**: ~1h 30min
**커밋**: 05f93a9

- [x] translator_common.c 2개 함수 (38→25, 26→25)
- [x] parser_utils.c 1개 함수 (36→25)
- [x] 빌드 검증

---

### Phase 8-4: 중첩 깊이 일괄 해소 (27-72번) 🔧
**예상 시간**: 7h 40min

- [ ] MPRIS 8개 (1h 20min)
- [ ] Config.c 8개 (1h 20min)
- [ ] LRC Parser 5개 (50min)
- [ ] LRCX Parser 4개 (40min)
- [ ] Parser Utils 4개 (40min)
- [ ] Main.c 3개 (30min)
- [ ] Rendering Manager 2개 (20min)
- [ ] Lyrics Provider 2개 (20min)
- [ ] SRT Parser 2개 (20min)
- [ ] File Utils 2개 (20min)
- [ ] 기타 6개 (1h)
- [ ] 빌드 검증

---

### Phase 8-5: 보안/품질 이슈 (73-75번) 🔒 ✅ **완료**
**예상 시간**: 45min
**실제 시간**: ~1h (컴파일 에러 수정 포함)
**커밋**: 73adfae

- [x] parser_utils.c:152 0바이트 malloc 수정 (5min) - 이미 검증 존재
- [x] file_utils.c:88 ellipsis 제거 (30min) - 6개 타입 안전 함수로 대체
- [x] file_utils.c:95 format string 수정 (10min) - 리터럴 포맷 스트링 사용
- [x] 빌드 검증

---

### Phase 9-1: MAJOR 이슈 해결 (76-102번)
**예상 시간**: 5h 15min

- [ ] 중첩 if 병합 5개 (25min)
- [ ] 파라미터 제한 4개 (1h 20min)
- [ ] 미사용 파라미터 7개 (35min)
- [ ] 중첩 break 3개 (1h)
- [ ] 주석 코드 제거 3개 (15min)
- [ ] 기타 MAJOR 6개 (2h)
- [ ] 빌드 검증

---

## 총 예상 시간

| Phase | 작업 | 이슈 수 | 예상 시간 |
|-------|------|---------|----------|
| 8-1 | 최우선 복잡도 | 5 | 6h 23min |
| 8-2 | 중간 복잡도 | 6 | 2h 58min |
| 8-3 | 소규모 복잡도 | 12 | 3h 7min |
| 8-4 | 중첩 깊이 | 46 | 7h 40min |
| 8-5 | 보안/품질 | 3 | 45min |
| **Phase 8 합계** | **CRITICAL** | **72** | **20h 53min** |
| 9-1 | MAJOR 이슈 | 26 | 5h 15min |
| **Phase 9 합계** | **MAJOR** | **26** | **5h 15min** |
| **총합계** | | **98** | **26h 8min** |

**버퍼 포함**: ~28-30시간 (테스트 및 디버깅)

**참고**: Python 파일 이슈는 모두 제외 (내부 테스트용 스크립트)

---

## 예상 개선 효과

### Before (현재 - 2025-12-29)
- **BLOCKER**: 0개
- **CRITICAL**: 73개 (C 코드만)
- **MAJOR**: 28개 (C 코드만)
- **MINOR**: 110개
- **전체 등급**: A (유지)
- **복잡도**: 일부 함수 142까지 초과

### After (Phase 8-9 완료 시)
- **BLOCKER**: 0개
- **CRITICAL**: 0개 (C 코드 전체 해결)
- **MAJOR**: 0개 (C 코드 전체 해결)
- **MINOR**: 110개 (유지)
- **전체 등급**: A (유지)
- **복잡도**: 모든 C 함수 25 이하
- **유지보수성**: 대폭 향상
- **가독성**: 향상
- **테스트 용이성**: 향상

**참고**: Python 스크립트 이슈(내부 테스트용)는 프로젝트 범위 외

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
   - 복잡도 목표 달성 확인

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
- **최종 업데이트**: 2025-12-30 (Phase 8-3.5 완료)
- **현재 Phase**: Phase 8 진행 중
- **완료 Phases**:
  - 1-7 (보안, 일부 복잡도, 파라미터, 중복 제거)
  - 8-1 (최우선 복잡도 5개, 커밋: 8d665ab)
  - 8-2 (중간 복잡도 7개, 커밋: 43ab00f, c8b40dc)
  - 8-3 (소규모 복잡도 11개, 커밋: 8da3a3b, 439604f)
  - 8-3.5 (추가 복잡도 3개, 커밋: 05f93a9) ✅ **NEW**
  - 8-5 (보안/품질 3개, 커밋: 73adfae)
- **남은 Phase 8 작업**:
  - 8-4: 중첩 깊이 (46개, ~8h)
- **남은 이슈 (C 코드만)**:
  - CRITICAL: ~55개 (18개 해결됨: Phase 8-1 5개 + 8-2 7개 + 8-3 3개 + 8-3.5 3개)
  - MAJOR: 28개 (Python 내부 테스트 스크립트 제외)
  - 총: ~83개
- **우선순위**: High (코드 품질, 유지보수성)
- **목표**: 모든 C 코드 CRITICAL/MAJOR 이슈 해결, A등급 유지
- **남은 예상 시간**: ~13h (Phase 8-4 + Phase 9)
