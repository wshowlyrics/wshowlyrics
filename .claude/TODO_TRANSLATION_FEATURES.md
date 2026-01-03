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
