# 🎯 SAST 도구 최종 구성

## ✅ 유지 (2개)

### 1. TODO_SONARCLOUD.md
**우선순위**: HIGH (즉시 구현)
- C/C++ 종합 품질 분석
- Technical Debt 계산
- GitHub PR 네이티브 통합
- 무제한 히스토리
- 완전 무료

### 2. TODO_COVERITY.md  
**우선순위**: LOW (필요시)
- Race condition, Deadlock 탐지
- 업계 최고 수준 심층 분석
- 주간 스캔 (느림)
- 멀티스레드 버그 의심 시 사용

## ❌ 제거 (2개)

### 1. TODO_PVS_STUDIO.md (제거됨)
**이유**:
- SonarCloud의 Duplications로 대체
- README 수정/라이선스 갱신 부담
- Copy-paste 버그 탐지는 SonarCloud로 충분

### 2. TODO_QODANA.md (제거됨)
**이유**:
- Modern C++ 특화 (현재 C 프로젝트)
- SonarCloud와 70-80% 기능 중복
- 30일 히스토리 제한
- Clang-Tidy 직접 실행이 더 효율적

## 📊 최종 도구 스택

```
현재 사용 중:
  ✅ Gitleaks    → 시크릿 스캔 (10초)
  ✅ Semgrep     → 보안 패턴 (30초)

즉시 추가:
  🎯 SonarCloud  → 종합 품질 분석 (2-3분)
     ├─ Bugs
     ├─ Vulnerabilities  
     ├─ Code Smells
     └─ Technical Debt

선택적 추가:
  📦 Coverity    → 멀티스레드 버그 (주간, 필요시)
  📦 Clang-Tidy  → 코드 스타일 (30초, 선택)
```

## 🔄 도구 간 역할 분담

| 도구 | 역할 | 속도 | 우선순위 |
|------|------|------|---------|
| Gitleaks | 시크릿 누출 | ⚡ 10초 | 필수 |
| Semgrep | 보안 패턴 (OWASP) | ⚡ 30초 | 필수 |
| SonarCloud | 종합 품질 + 기술부채 | 🐢 2-3분 | HIGH |
| Coverity | 멀티스레드 심층 분석 | 🐌 1-2시간 | LOW |
| Clang-Tidy | 코드 스타일 (선택) | ⚡ 30초-1분 | 선택 |

## 💡 추천 구현 순서

### Phase 1: 현재 (완료)
```bash
✅ Gitleaks
✅ Semgrep
```

### Phase 2: 즉시 (우선)
```bash
🎯 SonarCloud 구현
   └─ GitHub Actions 추가
   └─ Quality Gate 설정
   └─ PR Decoration 활성화
```

### Phase 3: 선택적 (나중에)
```bash
📦 Clang-Tidy 직접 실행 (원한다면)
   └─ lint 단계에 추가
   └─ 30초-1분 소요
   └─ SonarCloud 보완용

📦 Coverity (문제 발생 시)
   └─ Race condition 의심 시
   └─ 주간 스캔
```

## 🎓 결정 기준

### 유지 이유
- **SonarCloud**: 범용적, 무료, 히스토리 무제한, Technical Debt
- **Coverity**: 고유 기능 (멀티스레드), 필요시 유용

### 제거 이유
- **PVS-Studio**: SonarCloud로 대체 가능, 관리 부담
- **Qodana**: C++ 특화인데 C 프로젝트, SonarCloud 중복

### 핵심 원칙
> "도구는 최소화, 효과는 최대화"
- 3개 이상의 SAST는 과함 (현재 프로젝트 규모)
- SonarCloud 하나로 대부분의 요구사항 충족
- 나머지는 특수 상황에서만 사용
