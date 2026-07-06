# Translation Features TODO

## Adaptive Translation Feasibility Check

### Feature Description
번역 완료 예상 시간을 현재 재생 위치를 기반으로 적응형(adaptive)으로 계산하는 기능.

현재 `translator_check_time_feasibility()`는 번역 시작 시점에 한 번만 체크하지만,
실시간으로 현재 재생 위치를 추적하여 더 정확한 완료 예상을 제공할 수 있음.

### 아이콘 의미 (캐시 저장 정책 기준)

| 아이콘 | 의미 | 상태 |
|--------|------|------|
| ⏳ (모래시계) | 번역 진행 중 | 캐시 저장 임계치 미달 |
| 💾 (디스켓) | 저장 가능 | 캐시 저장 임계치 도달 |
| 🗑️ (휴지통) | 폐기 예정 | 시간 부족, 캐시 저장 없이 폐기 |

**캐시 저장 임계치 (cache_policy 설정)**:
| 정책 | 임계치 | 설명 |
|------|--------|------|
| comfort | 50% | 조기 저장, 중단 내성 높음 |
| balanced | 75% | 기본값, 균형 |
| aggressive | 90% | 늦은 저장, 완성도 높음 |

### Current Behavior (Static)
번역 시작 시점에만 feasibility 체크:
- ⏳: 번역 진행 중 → 임계치 도달 시 💾로 변경
- 🗑️: 번역 진행 중이지만 시간 부족으로 폐기 예정

### Desired Behavior (Adaptive)
번역 진행 중 실시간 재계산으로 아이콘 동적 변경:
- ⏳ → 💾: 캐시 저장 임계치 도달 시
- ⏳ → 🗑️: 시간 부족 감지 시
- 🗑️ → ⏳: 일시정지/seek으로 시간 여유 생김

**아이콘 전환 예시**:
1. 번역 시작 → ⏳ 표시 (진행 중)
2. 시간 부족 감지 → 🗑️로 변경 (폐기 예정)
3. 사용자가 일시정지/seek 뒤로 → 시간 여유 생김 → ⏳로 복귀
4. 임계치 도달 (예: 75%) → 💾로 변경 (캐시 저장 가능)
5. 재생 재개 후 다시 시간 부족 → 🗑️로 변경

### Use Cases
1. **일시정지 (Pause)**:
   - 일시정지 중에는 실제 남은 시간이 증가
   - 🗑️ → ⏳ 변경 가능 (시간 여유 생김, 다시 진행)

2. **처음부터 재생 (Replay from beginning)**:
   - 현재 위치가 0으로 리셋
   - 남은 시간이 다시 전체 곡 길이로 늘어남
   - 🗑️ → ⏳ 변경 (충분한 시간 확보)

3. **Seek (위치 이동)**:
   - 사용자가 앞/뒤로 이동
   - 뒤로 이동: 🗑️ → ⏳ (시간 여유 생김)
   - 앞으로 이동: ⏳ → 🗑️ (시간 부족해짐)

### Technical Considerations

#### 1. 재계산 시점
- 번역 진행 중 주기적으로 체크? (매 라인 번역 후?)
- 특정 이벤트 발생 시만? (일시정지, seek, 재생 재개)
- 성능 vs 정확도 트레이드오프

#### 2. MPRIS 상태 추적
```c
// PlaybackStatus 속성 확인 필요
// - "Playing": 재생 중
// - "Paused": 일시정지
// - "Stopped": 정지
```

현재 위치: `mpris_get_position()` 사용 가능

#### 3. 일시정지 시간 처리
- 일시정지 중에는 시간이 흐르지 않음
- 남은 시간 = `track_length_us - current_position_us`
- 일시정지 시간을 계산에서 제외해야 함

#### 4. Seek 감지
- 이전 위치와 현재 위치 차이가 임계값 이상이면 seek로 간주
- 반복 재생(loop)과 수동 seek 구분 필요 여부

#### 5. 동적 플래그 업데이트
```c
// lyrics_data 구조체에 이미 존재
bool translation_will_discard;
```

이 플래그를 번역 진행 중 동적으로 업데이트하여
오버레이의 🗑️/💾 아이콘을 실시간으로 변경

### Related Files
- `src/translator/common/translator_common.c`: `translator_check_time_feasibility()` 함수
- `src/utils/mpris/mpris.c`: MPRIS 메타데이터 및 위치 추적
- `src/core/rendering/rendering_manager.c`: 오버레이 아이콘 렌더링
- `src/lyrics_types.h`: `translation_will_discard` 플래그

### Implementation Steps (Draft)
1. MPRIS에서 `PlaybackStatus` 속성 읽기 기능 추가
2. 번역 스레드에서 주기적으로 현재 위치 및 상태 체크
3. 남은 시간 기반으로 feasibility 재계산
4. `translation_will_discard` 플래그 동적 업데이트
5. 일시정지/seek 이벤트 감지 로직
6. 성능 영향 최소화 (불필요한 재계산 방지)

### Open Questions
- 얼마나 자주 재계산해야 하는가? (성능 영향)
- 일시정지 중 아이콘 변경이 필요한가?
- Seek 동작 시 번역을 리셋해야 하는가?
- 반복 재생과 수동 seek를 구분할 필요가 있는가?

### Priority
**Medium** - 유용한 기능이지만 현재 기본 feasibility check가 작동하므로 급하지 않음

### Related Commits
- `f565d91`: feat: Add visual indicator for translation discard warning
- `30e3a05`: fix: Support UINT64 type for mpris:length in MPRIS metadata
- `347e1a5`: feat: Add translation time feasibility check before starting translation

---

## Ollama Translator Provider (Self-Hosted LLM)

> `.claude/TODO_CODE_QUALITY.md`의 R7(코드 정리)에서 이관됨. R7은 dispatcher를
> vtable 레지스트리로 바꿔 **통합 표면만** 준비했고, 실제 Ollama 번역 구현은
> 번역 기능 작업이라 여기서 추적한다.

### Feature Description
로컬/자체호스팅 Ollama 서버(기본 `http://localhost:11434`)를 번역 provider로
추가. 클라우드 API 키 없이 온프레미스 LLM으로 가사를 번역하려는 사용자를 위한 기능.

### 선행 인프라 (완료)
R7 (커밋 `1d3800a`)로 translator provider 레지스트리가 이미 존재:
- `struct translator_provider { name, matches, init, cleanup, translate_lyrics }`
  (`src/translator/common/translator_common.h`)
- 각 모듈이 자기 `X_provider`를 self-register, `translator_providers[]` 배열 순회로
  dispatch + init + cleanup 처리.
- → **새 provider 추가 시 dispatcher / main.c 수정 불필요.**

### 남은 실제 작업 (미착수)
1. **새 모듈** `src/translator/ollama/ollama_translator.{c,h}`:
   - `ollama_translate_lyrics(struct lyrics_data*, int64_t)` — 기존
     `translator_translate_lyrics_generic()`에 `translate_single_line` 함수
     포인터를 넘기는 패턴 재사용 (gemini/claude/openai와 동일 구조).
   - `ollama_translator_init()` / `ollama_translator_cleanup()`.
   - HTTP 요청/응답 파싱: Ollama `POST /api/chat` (또는 `/api/generate`),
     `{"model": ..., "messages": [...], "stream": false}` → 응답 JSON에서
     번역 텍스트 추출 (json-c). TLS 불필요(로컬 평문 http)하나 원격 호스트
     허용 시 옵션 고려.
2. **등록** (2줄):
   - `translator_common.h`에 `extern const struct translator_provider ollama_provider;`
   - `translator_common.c`의 `translator_providers[]`에 `&ollama_provider,` 한 줄
3. **provider struct + matches** (모듈 내 ~10줄):
   - `ollama_matches(p)` = `strncmp(p, "ollama", 6) == 0` (예: `ollama-llama3.1`)
   - `const struct translator_provider ollama_provider = { ... };`
4. **설정**: `[translation] provider = ollama-<model>` + Ollama 서버 URL/모델
   설정 키 (`config.h` / `settings.ini.example`). 기본 `http://localhost:11434`.

### Technical Considerations
- **모델명 파싱**: provider 문자열 `ollama-<model>`에서 `<model>` 추출해 API `model`
  필드로 전달 (gemini의 `gemini-<model>` 관례와 동일).
- **엔드포인트/URL 설정**: 로컬 기본값 + 사용자 지정 호스트 허용. 원격 http 허용
  시 보안 고려(SEC-2의 로컬 동일유저 위협모델 논의 참고 — 사용자 자신의 서버).
- **응답 지연**: 로컬 LLM은 모델/하드웨어에 따라 느릴 수 있음 → adaptive
  feasibility check(위 기능)와 잘 맞물림. `max_retries` / rate-limit 로직 재사용.
- **LRC only**: 기존 API translator와 동일하게 LRC만 (LRCX/SRT/VTT 제외).

### Related Files
- `src/translator/common/translator_common.{c,h}`: provider 레지스트리 + `translate_single_line` 제네릭 러너
- `src/translator/gemini/gemini_translator.c`: 가장 가까운 참고 구현 (prefix 매칭 + 제네릭 러너)
- `src/provider/lyrics/lyrics_provider.c`: dispatcher (수정 불필요)
- `src/user_experience/config/config.{c,h}`, `settings.ini.example`: provider/URL 설정

### Priority
**Medium** — R7로 통합 비용이 크게 낮아졌고 자체호스팅 수요 대응. 실제 구현
(HTTP/JSON 파싱)은 새로 작성 필요.

### Related Issues
- GitLab wshowlyrics#2 (R7 vtable) — 인프라 선행 완료
