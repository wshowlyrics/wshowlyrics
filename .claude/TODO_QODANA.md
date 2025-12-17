# TODO: Qodana 통합

## 개요
Qodana는 JetBrains의 코드 품질 분석 플랫폼으로, Clang-Tidy 기반 C/C++ 정적 분석을 제공합니다.
Community 버전은 오픈소스와 상용 프로젝트 모두 완전 무료로 사용 가능합니다.

## 장점
- **완전 무료**: Community 라이선스, 오픈소스/상용 프로젝트 모두 무료
- **무제한 사용**: 프로젝트 수, 코드 라인 수 제한 없음
- **Clang-Tidy 기반**: 표준 C/C++ 정적 분석 (Clang 15-18 지원)
- **CLion 커스터마이징**: JetBrains CLion의 Clang-Tidy 개선사항 포함
- **GitHub Actions 네이티브 지원**: 공식 Action 제공
- **토큰 불필요**: Community 버전은 클라우드 토큰 없이도 작동
- **Docker 기반**: 간편한 CI/CD 통합

## Community vs Ultimate 비교

| 기능 | Community (무료) | Ultimate/Ultimate Plus (유료) |
|------|------------------|-------------------------------|
| Clang-Tidy 검사 | ✅ | ✅ |
| MISRA 표준 검사 | ❌ | ✅ |
| 데이터플로우 분석 | ❌ | ✅ |
| 자동 빌드 설정 | ❌ (수동 필요) | ✅ |
| 히스토리 보관 | 30일 | 180일 / 무제한 |
| Quick-Fix | ❌ | ✅ |
| 코드 커버리지 | ❌ | ✅ |

## 프로젝트 적합성 확인

### 현재 프로젝트
- **빌드 시스템**: Meson ✅ (compile_commands.json 생성 가능)
- **라이선스**: GPL-3.0 (OSI 승인) ✅
- **공개 여부**: GitHub 공개 저장소 ✅
- **코스트**: 완전 무료 ✅

### 요구사항
- ✅ Docker (GitHub Actions에 이미 설치됨)
- ✅ `compile_commands.json` (Meson으로 생성 가능)
- ✅ 최소 2GB Docker 메모리

## GitHub Actions 통합

```yaml
# .github/workflows/security.yml 에 추가
qodana:
  name: Qodana - C/C++ Analysis
  if: ${{ github.event.workflow_run.conclusion == 'success' }}
  runs-on: ubuntu-latest
  permissions:
    contents: write
    pull-requests: write
    checks: write
  steps:
    - name: Checkout code
      uses: actions/checkout@v4
      with:
        fetch-depth: 0  # Full history for pull request analysis

    - name: Install build dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y meson ninja-build pkg-config \
          libcairo2-dev libpango1.0-dev libwayland-dev \
          libcurl4-openssl-dev libssl-dev libappindicator3-dev \
          libgdk-pixbuf-2.0-dev libfontconfig-dev wayland-protocols

    - name: Configure project
      run: meson setup build

    - name: Generate compile_commands.json
      run: |
        meson compile -C build --ninja-args=-t compdb > compile_commands.json

    - name: Run Qodana Community for C/C++
      uses: JetBrains/qodana-action@v2025.2
      with:
        args: --image,jetbrains/qodana-clang:2025.2-eap
        fail-threshold: 0  # Don't fail build on issues (optional)

    - name: Upload Qodana results
      uses: github/codeql-action/upload-sarif@v4
      if: always()
      with:
        sarif_file: ${{ runner.temp }}/qodana/results/qodana.sarif.json
```

## 예상되는 버그 탐지

Qodana (Clang-Tidy 기반)가 현재 코드에서 잡을 수 있는 버그:

1. **메모리 관리**
   - Use after free
   - Memory leak (간단한 경우)
   - Double free
   - Null pointer dereference

2. **코딩 스타일**
   - Naming convention 위반
   - Unused variables/functions
   - Magic numbers

3. **성능 이슈**
   - Inefficient string operations
   - Unnecessary copies
   - Suboptimal algorithms

4. **보안 취약점 (기본)**
   - Buffer overflow (간단한 경우)
   - Format string vulnerabilities
   - Integer overflow

5. **Modern C++ 모범 사례**
   - Prefer `nullptr` over NULL
   - Use of deprecated functions
   - RAII 패턴 위반

## 현재 도구들과의 관계

```
일일 CI (빠름):
  - Gitleaks:  시크릿 스캔 (10초)
  - Semgrep:   보안 패턴 (30초)
  - Qodana:    Clang-Tidy 기반 (1-2분)

비교:
  Semgrep:     보안 패턴 매칭 (AST 기반)
  Qodana:      Clang-Tidy (컴파일러 기반 분석)
```

**중복 최소화**: Semgrep은 보안 패턴, Qodana는 코드 품질/버그

## 장단점

### 장점 ✅
- 완전 무료 (제한 없음)
- Clang-Tidy 기반 (업계 표준)
- JetBrains CLion 개선사항 포함
- GitHub Actions 네이티브 지원
- Docker 기반 (설치 간편)
- SARIF 출력 (GitHub Code Scanning 연동)
- 토큰 불필요 (Community 버전)

### 단점 ❌
- **Community 제한**: MISRA, 데이터플로우 분석 없음
- **속도**: Clang 기반이라 Semgrep보다 느림 (1-2분)
- **수동 설정**: compile_commands.json 생성 필요
- **Docker 의존**: Docker 환경 필수
- **히스토리 제한**: 30일만 보관

## 구현 우선순위

- Priority: **LOW-MEDIUM**
- Reason:
  - Semgrep으로 보안 패턴은 충분히 탐지 가능
  - Clang-Tidy는 코드 품질 향상에 유용하지만 필수는 아님
  - 무료이고 설정이 간단하므로 시도해볼 가치는 있음
- When: 다음 조건 중 하나 충족 시 고려
  1. 코드 품질 개선이 우선순위가 될 때
  2. Semgrep이 놓치는 C/C++ 특정 버그가 발견될 때
  3. CI/CD 파이프라인에 여유 시간이 생길 때 (현재 1-2분 추가)

## 대안: Clang-Tidy 직접 실행

Qodana 대신 Clang-Tidy를 직접 실행할 수도 있음:

```yaml
- name: Run Clang-Tidy
  run: |
    clang-tidy -p build src/**/*.c --checks='-*,bugprone-*,clang-analyzer-*,performance-*'
```

**장점**: Docker 불필요, 더 빠름
**단점**: 결과 포맷팅/보고서 생성 수동, SARIF 변환 필요

## 참고 링크

- 공식 사이트: https://www.jetbrains.com/qodana/
- C/C++ Community 문서: https://www.jetbrains.com/help/qodana/qodana-clang.html
- GitHub Action: https://github.com/JetBrains/qodana-action
- Pricing: https://www.jetbrains.com/help/qodana/pricing.html
- Clang-Tidy 문서: https://clang.llvm.org/extra/clang-tidy/
