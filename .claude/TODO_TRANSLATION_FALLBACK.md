# TODO: Multi-Provider Translation Support

## 배경

현재 DeepL API 단독 사용 중이며, 약 89% 성공률을 보임.
사용자에게 다른 번역 API 선택권을 제공하여:
- 비용 최적화 (무료 Gemini API 사용)
- 번역 품질 향상 (Claude API 사용)
- Provider 장애 시 대체 수단 제공

**DeepL 실패 케이스 (11%)**:
- 의성어/의태어가 많은 가사 (いぇいいぇーい, ふっふー 등)
- 구어체/비문법적 표현
- 일본어 특유의 표현

## 구현 방식

**선택: 단일 Provider 선택 (Fallback은 나중에)**
- 이유: Fallback 실패 판정 기준이 명확하지 않음 (오역 vs 의성어 구분 어려움)
- 장점: 구현 간단, 각 API 독립적으로 테스트 가능, 사용자가 직접 provider 선택
- 단점: 자동 fallback 없음 (수동으로 provider 변경 필요)
- Fallback: 나중에 "플렉스 사용자" 기능으로 추가 고려

**구현 방법: API 직접 사용 (libcurl 기반)**
- 이유: CLI 도구마다 인터페이스가 달라 유지보수 어려움
- 장점: 일관된 구현, 성능, 의존성 관리 용이

## API 후보 및 스펙

### 1. **Gemini API** (무료, 추천)
- **Endpoint**: `POST https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent?key=$API_KEY`
- **Models**:
  - `gemini-2.5-flash` (무료, 빠름, 추천)
  - `gemini-2.5-pro` (유료, 고품질)
  - `gemini-1.5-flash`, `gemini-1.5-pro` (이전 버전)
- **무료 할당량**: 15 RPM (requests per minute), 1M TPM (tokens per minute), 1500 RPD (requests per day)
- **문서**: https://ai.google.dev/api/generate-content
- **인증**: URL 파라미터 `?key=$API_KEY`
- **Request**:
  ```json
  {
    "contents": [{
      "parts": [{"text": "Translate to Korean: <text>"}]
    }]
  }
  ```
- **Response**: `candidates[0].content.parts[0].text`

### 2. **Claude API** (유료, 고품질)
- **Endpoint**: `POST https://api.anthropic.com/v1/messages`
- **Models**:
  - `claude-sonnet-4-5` (균형, 추천)
  - `claude-opus-4-5` (최고 품질, 느림)
  - `claude-haiku-4-5` (빠름, 저렴)
- **문서**: https://docs.anthropic.com/
- **인증**: Headers `x-api-key: $API_KEY`, `anthropic-version: 2023-06-01`
- **Request**:
  ```json
  {
    "model": "claude-sonnet-4-5",
    "max_tokens": 1024,
    "messages": [{"role": "user", "content": "Translate to Korean: <text>"}]
  }
  ```
- **Response**: `content[0].text`

### 3. **DeepL API** (기존 유지)
- 변경 없음, `[translation]` 섹션으로 통합만

## 수정/추가 파일 목록

### 1. Gemini Translator 구현
- [ ] `src/translator/gemini/gemini_translator.c` (신규)
- [ ] `src/translator/gemini/gemini_translator.h` (신규)
  - `bool gemini_translate_lyrics(struct lyrics_data *data)` - DeepL과 동일 인터페이스
  - `bool gemini_translator_init(void)` - CURL 초기화
  - `void gemini_translator_cleanup(void)` - CURL 정리
  - DeepL과 동일한 구조: async 번역, MD5 기반 캐싱, rate limiting
  - Model은 config에서 가져옴 (`gemini-2.5-flash` 등)

### 2. Claude Translator 구현
- [ ] `src/translator/claude/claude_translator.c` (신규)
- [ ] `src/translator/claude/claude_translator.h` (신규)
  - `bool claude_translate_lyrics(struct lyrics_data *data)` - DeepL과 동일 인터페이스
  - `bool claude_translator_init(void)` - CURL 초기화
  - `void claude_translator_cleanup(void)` - CURL 정리
  - DeepL과 동일한 구조: async 번역, MD5 기반 캐싱, rate limiting
  - Model은 config에서 가져옴 (`claude-sonnet-4-5` 등)

### 3. 설정 파일 통합 (`[deepl]` → `[translation]`)
- [ ] `src/user_experience/config/config.h`
  - `[deepl]` 섹션을 `[translation]`으로 변경
  ```c
  struct config {
      struct {
          char provider[64];  // "deepl", "gemini-2.5-flash", "claude-sonnet-4-5", "false"
          char api_key[256];
          char target_language[8];
          enum translation_display_mode translation_display;
          float translation_opacity;
      } translation;
  };
  ```

- [ ] `src/user_experience/config/config.c`
  - `[translation]` 섹션 파싱 추가
  - `[deepl]` 섹션 deprecated 처리 (호환성 유지, warning 출력)
  - Provider 값 파싱: `deepl`, `gemini-*`, `claude-*`, `false`

- [ ] `settings.ini.example`
  ```ini
  [translation]
  # Provider and model (format: provider or provider-model)
  # Examples:
  #   provider = deepl                 (DeepL API, no model selection)
  #   provider = gemini-2.5-flash      (Gemini API, free tier)
  #   provider = claude-sonnet-4-5     (Claude API, balanced)
  #   provider = false                 (disable translation)
  provider = false

  # API key (for the selected provider)
  api_key =

  # Target language (applies to all providers)
  # Common: EN, KO, JA, ZH-HANS, ZH-HANT, ES, FR, DE
  target_language = EN

  # Display settings
  translation_display = both
  translation_opacity = 0.7
  ```

### 4. Main 수정 (Provider 분기)
- [ ] `src/lyrics/lyrics_manager.c` or `src/main.c`
  - Provider 값 파싱하여 적절한 translator 호출
  - `strcmp(provider, "deepl") == 0` → `deepl_translate_lyrics()`
  - `strncmp(provider, "gemini-", 7) == 0` → `gemini_translate_lyrics()`
  - `strncmp(provider, "claude-", 7) == 0` → `claude_translate_lyrics()`
  - `strcmp(provider, "false") == 0` → 번역 안 함

### 5. 빌드 시스템
- [ ] `meson.build`
  ```meson
  translator_sources = files(
    'src/translator/deepl/deepl_translator.c',
    'src/translator/gemini/gemini_translator.c',  # 추가
    'src/translator/claude/claude_translator.c',  # 추가
  )
  ```

### 6. 문서 업데이트
- [ ] `CLAUDE.md`
  - Multi-provider translation 아키텍처 설명
  - 각 provider별 특징 (무료/유료, 품질, 속도)
  - Model 선택 가이드
- [ ] `README.md`
  - Gemini API key 발급: https://aistudio.google.com/apikey
  - Claude API key 발급: https://console.anthropic.com/
  - Provider 선택 가이드 (비용, 품질 비교)

## 구현 단계

### Phase 1: Gemini API 통합 (3-4시간)
1. ✅ Gemini API 스펙 확인
   - Endpoint, Request/Response 형식
   - Rate limits, 인증 방식

2. [ ] `gemini_translator.c/h` 구현
   - DeepL과 동일한 구조 참고 (async, cache, rate limit)
   - API endpoint: `https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent?key=$API_KEY`
   - Model은 provider 값에서 추출: `gemini-2.5-flash` → model name
   - Request/Response JSON 파싱
   - MD5 기반 캐싱 (`/tmp/wshowlyrics/translated/{md5}_{lang}.json`)
   - Rate limiting: 15 RPM → 4초당 1 req (250ms delay로 안전하게)

3. [ ] 단위 테스트
   - 정상 케이스 테스트
   - DeepL 실패 케이스: `ダメなとこも愛しき (いぇいいぇーい)`
   - 캐시 동작 확인

### Phase 2: Claude API 통합 (3-4시간)
1. ✅ Claude API 스펙 확인

2. [ ] `claude_translator.c/h` 구현
   - DeepL과 동일한 구조 참고
   - API endpoint: `https://api.anthropic.com/v1/messages`
   - Headers: `x-api-key`, `anthropic-version: 2023-06-01`
   - Model은 provider 값에서 추출: `claude-sonnet-4-5` → model name
   - Request/Response JSON 파싱
   - MD5 기반 캐싱
   - Rate limiting: Tier별 다름 (초기에는 보수적으로 500ms delay)

3. [ ] 단위 테스트
   - 정상 케이스 테스트
   - DeepL 실패 케이스 테스트
   - 캐시 동작 확인

### Phase 3: Config 통합 및 Main 수정 (2-3시간)
1. [ ] Config 구조 변경
   - `[deepl]` → `[translation]`으로 변경
   - Provider 파싱 로직 구현
   - 기존 `[deepl]` 섹션 deprecated 처리 (호환성)

2. [ ] Main에서 provider별 분기
   - Provider 값 확인하여 적절한 translator 호출
   - `deepl` → `deepl_translate_lyrics()`
   - `gemini-*` → `gemini_translate_lyrics()`
   - `claude-*` → `claude_translate_lyrics()`
   - `false` → 번역 안 함

3. [ ] `settings.ini.example` 업데이트
   - `[translation]` 섹션 작성
   - Provider 옵션 설명 추가
   - API key 발급 링크 추가

4. [ ] 통합 테스트
   - 각 provider 전환 테스트
   - 캐시 호환성 확인

### Phase 4: 문서화 (1시간)
- [ ] `CLAUDE.md` 업데이트
  - Multi-provider 아키텍처 설명
  - Provider별 특징 정리
- [ ] `README.md` 업데이트
  - Provider 선택 가이드
  - API key 발급 링크

### Phase 5: Fallback 구현 (미래, 선택적)
- Provider 실패 감지 기준 정립 필요
- 여러 API key 관리 방법 설계
- Rate limiting 조정
- **우선순위**: Low ("플렉스 사용자" 기능)

## 고려사항

### 1. Rate Limiting
- **DeepL**: 200ms 딜레이 (5 req/s) - 현재 구현
- **Gemini**: 15 RPM (4초당 1 req) → 250ms 딜레이로 안전하게 구현
- **Claude**: Tier별 다름, 보수적으로 500ms 딜레이로 시작

### 2. API Key 관리
- 단일 provider만 사용하므로 하나의 API key만 필요
- `api_key` 필드 하나로 통합
- Fallback 구현 시 provider별 key 필요 (미래)

### 3. 에러 핸들링
- API 호출 실패 시: 원문 그대로 표시
- 네트워크 에러, 인증 실패, Rate limit 초과 등 처리
- Translation progress에 실패 상태 표시

### 4. 비용
- **Gemini**: 무료 티어 (1500 RPD) - 일반 사용에 충분
- **Claude**: 유료지만 품질 우수
- **DeepL**: 기존과 동일

### 5. 캐시 호환성
- MD5 checksum 기반 캐싱은 provider 무관
- 동일 파일이라면 provider 전환 시에도 캐시 재사용 가능
- 캐시 파일명: `/tmp/wshowlyrics/translated/{md5}_{lang}.json`

## 테스트 케이스

### 정상 케이스
- 일반 LRC 가사 파일 번역 (63 lines)
- 캐시 히트/미스 동작 확인
- Translation progress 표시 확인

### DeepL 실패 케이스 (Provider 비교용)
```
1. ダメなとこも愛しき (いぇいいぇーい)
   → DeepL: "ダメなとこも愛しき (이이이...)" (실패)
   → Gemini/Claude: 완전 번역 기대

2. 謙遜 日本文化 (くりーちゃー?)
   → DeepL: "謙遜 日本文化 (くりーちゃー?)" (원문 유지)
   → Gemini/Claude: 번역 품질 비교

3. あふれる魅力 超をかし (いぇいいぇーい)
   → DeepL: 부분 실패
   → Gemini/Claude: 번역 품질 비교
```

## 참고 자료

- **Gemini API**: https://ai.google.dev/api/generate-content
- **Claude API**: https://docs.anthropic.com/claude/reference/getting-started-with-the-api
- **DeepL API**: https://developers.deepl.com/docs/api-reference/translate
- **Gemini API Key**: https://aistudio.google.com/apikey
- **Claude API Key**: https://console.anthropic.com/
- **현재 구현 (DeepL)**: commit 1388de2

## 상태

- **현재**: 진행 중 (API 리서치 완료, 구현 시작)
- **우선순위**: High (사용자 선택권 제공)
- **Fallback**: Low (나중에 "플렉스 사용자" 기능으로 추가)
