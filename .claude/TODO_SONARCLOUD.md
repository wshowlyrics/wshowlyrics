# TODO: SonarCloud 통합

## 개요
SonarCloud는 SonarSource의 클라우드 기반 코드 품질 플랫폼으로, 공개 오픈소스 프로젝트는 완전 무료로 사용 가능합니다.
GitHub 네이티브 통합을 제공하며, C/C++를 포함한 30+ 언어를 지원합니다.

## 장점
- **완전 무료**: 공개 오픈소스 프로젝트 무제한 무료
- **C/C++ 지원**: Community Edition과 달리 C/C++ 분석 가능
- **GitHub 네이티브**: Pull Request Decoration, Status Checks 자동
- **Technical Debt**: 기술 부채 계산 및 시각화
- **서버 불필요**: 클라우드 기반, 별도 인프라 필요 없음
- **히스토리 추적**: 코드 품질 트렌드 분석, 무제한 보관
- **다중 언어**: C, C++, JavaScript, Python, Java 등 30+ 언어
- **SARIF 지원**: GitHub Code Scanning 연동

## 프로젝트 적합성 확인

### 현재 프로젝트
- **언어**: C ✅
- **LOC**: ~80,000줄 ✅
- **라이선스**: GPL-3.0 (OSI 승인) ✅
- **공개 여부**: GitHub 공개 저장소 ✅
- **비용**: 완전 무료 ✅

### 무료 사용 조건
- ✅ 공개 오픈소스 프로젝트
- ✅ GitHub/GitLab/Bitbucket 중 하나
- ✅ 코드 라인 수 제한 없음
- ❌ 비공개 프로젝트는 유료

## 등록 및 설정

### 1. SonarCloud 등록
1. https://sonarcloud.io/ 방문
2. GitHub 계정으로 로그인
3. "Analyze new project" 클릭
4. 조직 선택: `unstable-code`
5. 프로젝트 선택: `lyrics`
6. 분석 방법 선택: "With GitHub Actions"

### 2. GitHub Secrets 추가
```
SONAR_TOKEN  # SonarCloud에서 생성한 토큰
```

## GitHub Actions 통합

```yaml
# .github/workflows/sonarcloud.yml
name: SonarCloud Analysis

on:
  push:
    branches:
      - master
  pull_request:
    types: [opened, synchronize, reopened]
  workflow_dispatch:

jobs:
  sonarcloud:
    name: SonarCloud
    runs-on: ubuntu-latest
    env:
      SONAR_SCANNER_VERSION: 5.0.1.3006
      BUILD_WRAPPER_OUT_DIR: build_wrapper_output_directory
    steps:
      - name: Checkout code
        uses: actions/checkout@v4
        with:
          fetch-depth: 0  # Shallow clones should be disabled for better analysis

      - name: Install build dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y meson ninja-build pkg-config \
            libcairo2-dev libpango1.0-dev libwayland-dev \
            libcurl4-openssl-dev libssl-dev libappindicator3-dev \
            libgdk-pixbuf-2.0-dev libfontconfig-dev wayland-protocols

      - name: Install SonarCloud Build Wrapper
        run: |
          mkdir -p $HOME/.sonar
          curl -sSLo $HOME/.sonar/build-wrapper-linux-x86.zip \
            https://sonarcloud.io/static/cpp/build-wrapper-linux-x86.zip
          unzip -o $HOME/.sonar/build-wrapper-linux-x86.zip -d $HOME/.sonar/
          echo "$HOME/.sonar/build-wrapper-linux-x86" >> $GITHUB_PATH

      - name: Install SonarScanner
        run: |
          curl -sSLo $HOME/.sonar/sonar-scanner.zip \
            https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-${{ env.SONAR_SCANNER_VERSION }}-linux.zip
          unzip -o $HOME/.sonar/sonar-scanner.zip -d $HOME/.sonar/
          echo "$HOME/.sonar/sonar-scanner-${{ env.SONAR_SCANNER_VERSION }}-linux/bin" >> $GITHUB_PATH

      - name: Configure project
        run: meson setup build

      - name: Build with Build Wrapper
        run: |
          build-wrapper-linux-x86-64 --out-dir ${{ env.BUILD_WRAPPER_OUT_DIR }} \
            meson compile -C build

      - name: Run SonarCloud analysis
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          SONAR_TOKEN: ${{ secrets.SONAR_TOKEN }}
        run: |
          sonar-scanner \
            --define sonar.organization=unstable-code \
            --define sonar.projectKey=unstable-code_lyrics \
            --define sonar.sources=src \
            --define sonar.cfamily.build-wrapper-output=${{ env.BUILD_WRAPPER_OUT_DIR }} \
            --define sonar.host.url=https://sonarcloud.io
```

### sonar-project.properties 파일
```properties
# Project identification
sonar.organization=unstable-code
sonar.projectKey=unstable-code_lyrics
sonar.projectName=wshowlyrics
sonar.projectVersion=1.0

# Source code
sonar.sources=src
sonar.sourceEncoding=UTF-8

# C/C++ specific
sonar.cfamily.cache.enabled=true
sonar.cfamily.threads=2

# Exclusions
sonar.exclusions=build/**,protocols/**

# Coverage (optional, if you add tests)
# sonar.cfamily.gcov.reportsPath=coverage
```

## 분석 결과 확인

### 웹 대시보드
1. https://sonarcloud.io/project/overview?id=unstable-code_lyrics
2. 주요 메트릭:
   - **Bugs**: 잠재적 버그
   - **Vulnerabilities**: 보안 취약점
   - **Code Smells**: 코드 품질 이슈
   - **Coverage**: 테스트 커버리지
   - **Duplications**: 중복 코드
   - **Technical Debt**: 기술 부채 (수정 소요 시간)

### GitHub Pull Request
- PR에 자동으로 분석 결과 댓글 추가
- Quality Gate 통과/실패 status check
- 새로 추가된 이슈만 표시 (기존 이슈 무시)

## 예상되는 탐지 항목

### 1. Bugs
- Null pointer dereference
- Memory leak
- Resource leak (FILE, CURL 등)
- Use after free
- Integer overflow
- Division by zero

### 2. Vulnerabilities
- Buffer overflow
- SQL injection (준비된 문이 없는 경우)
- Command injection
- Path traversal
- Insecure random number

### 3. Code Smells
- Too many parameters (함수 파라미터 많음)
- Cognitive complexity (복잡한 로직)
- Duplicated code blocks
- Long functions
- Magic numbers
- Unused variables

### 4. Security Hotspots
- Hard-coded credentials
- Weak cryptography
- Insecure file permissions
- Unsafe string operations

## 현재 도구들과의 비교

```
보안 패턴 (빠름):
  Gitleaks  → 시크릿 탐지 (10초)
  Semgrep   → OWASP 패턴 (30초)

코드 품질 + 보안 (포괄):
  SonarCloud → C/C++ 종합 분석 (2-3분)
    ├─ Bugs
    ├─ Vulnerabilities
    ├─ Code Smells
    └─ Technical Debt

심층 분석 (선택):
  Coverity  → Race condition, Deadlock (주간)
  Qodana    → Clang-Tidy (코드 스타일)
```

**역할 분담**:
- **Semgrep**: 빠른 보안 패턴 (PR 블로킹용)
- **SonarCloud**: 종합 품질 분석 (대시보드, 트렌드)
- **Coverity**: 심층 멀티스레드 분석 (주기적)

## 장단점

### 장점 ✅
- 완전 무료 (공개 프로젝트)
- C/C++ 지원 (SonarQube Community와 차이)
- GitHub 네이티브 통합
- Technical Debt 시각화
- PR별 새 이슈만 표시
- 히스토리 무제한 보관
- SARIF 출력 (GitHub Code Scanning)
- 다중 언어 지원 (향후 확장성)

### 단점 ❌
- **빌드 필요**: Build Wrapper로 컴파일 필수 (느림)
- **속도**: 2-3분 소요 (Semgrep보다 느림)
- **오탐 가능**: Code Smells는 주관적일 수 있음
- **학습 곡선**: 초기 설정 복잡
- **클라우드 의존**: 인터넷 필요, SonarCloud 서비스 의존

## 구현 우선순위

- Priority: **HIGH**
- Reason:
  - 현재 Semgrep만으로는 C/C++ 특정 버그 탐지 부족
  - Technical Debt 추적으로 장기 유지보수 개선
  - 완전 무료이고 GitHub 통합 우수
  - Coverity/PVS-Studio보다 범용적이고 접근성 좋음
- When: 즉시 구현 권장
  1. Semgrep 이후 2단계 방어선으로 추가
  2. PR Quality Gate로 코드 품질 기준 설정
  3. 대시보드로 프로젝트 건강도 모니터링

## 대안: SonarQube Community (자체 호스팅)

SonarCloud 대신 SonarQube Community Edition을 자체 호스팅할 수도 있지만:

❌ **C/C++ 미지원** - Community Edition은 Java, Python만 가능
✅ SonarCloud를 사용하는 게 훨씬 나음

## Quality Gate 설정 예시

```yaml
# sonar-project.properties에 추가
sonar.qualitygate.wait=true
```

기본 Quality Gate 조건:
- Coverage < 80% → 실패
- Duplications > 3% → 실패
- Maintainability Rating < A → 실패
- Reliability Rating < A → 실패
- Security Rating < A → 실패

## 참고 링크

- 공식 사이트: https://sonarcloud.io/
- C/C++ 문서: https://docs.sonarcloud.io/advanced-setup/languages/c-c-objective-c/
- GitHub Action: https://github.com/SonarSource/sonarcloud-github-action
- 가격 정책: https://www.sonarsource.com/plans-and-pricing/sonarcloud/
- Build Wrapper 가이드: https://docs.sonarcloud.io/advanced-setup/languages/c-c-objective-c/running-analysis/
