# TODO: Coverity Scan 통합

## 개요
Coverity Scan은 Synopsys의 상용 정적 분석기로, 오픈소스 프로젝트는 무료로 사용 가능합니다.
Linux Kernel, PostgreSQL 등 대규모 프로젝트에서 사용하는 업계 최고 수준의 분석 도구입니다.

## 장점
- **업계 최고 정확도**: 수학적 모델링 기반, 매우 깊은 분석
- **낮은 오탐률**: 확실한 버그만 보고
- **보안 표준 준수**: CWE, CERT, OWASP 커버리지
- **웹 대시보드**: 팀 협업 기능, 트렌드 분석, 히스토리 추적
- **멀티스레드 버그**: Race condition, deadlock 탐지
- **무료 오픈소스**: 공개 프로젝트 무료 사용

## 프로젝트 적합성 확인

### 현재 프로젝트
- **LOC**: ~80,000줄
- **라이선스**: GPL-3.0 (OSI 승인)
- **공개 여부**: GitHub 공개 저장소 ✅

### LOC 제한 및 빌드 쿼터
| LOC 범위 | 주간 빌드 | 일일 빌드 |
|----------|-----------|-----------|
| **< 100K** | 28회/주 | 4회/일 |
| 100K-500K | 21회/주 | 3회/일 |
| 500K-1M | 14회/주 | 2회/일 |
| > 1M | 7회/주 | 1회/일 |

**당신의 프로젝트**: ~80K LOC → **28회/주, 4회/일** ✅

## 등록 방법

1. https://scan.coverity.com/ 방문
2. GitHub 계정으로 로그인
3. "Add project" 클릭
4. 프로젝트 선택: `unstable-code/lyrics`
5. 프로젝트 언어: C/C++
6. 빌드 시스템: Meson
7. 승인 대기 (보통 1-2일)

## GitHub Actions 통합 (주간 스캔)

```yaml
# .github/workflows/coverity-scan.yml
name: Coverity Scan

on:
  schedule:
    # 매주 일요일 03:00 UTC (한국 시간 12:00)
    - cron: '0 3 * * 0'
  workflow_dispatch:  # 수동 실행 가능

jobs:
  coverity:
    name: Coverity Scan Analysis
    runs-on: ubuntu-latest
    steps:
      - name: Checkout code
        uses: actions/checkout@v4

      - name: Install build dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y meson ninja-build pkg-config \
            libcairo2-dev libpango1.0-dev libwayland-dev \
            libcurl4-openssl-dev libssl-dev libappindicator3-dev \
            libgdk-pixbuf-2.0-dev libfontconfig-dev wayland-protocols

      - name: Download Coverity Build Tool
        run: |
          wget -q https://scan.coverity.com/download/linux64 \
            --post-data "token=${{ secrets.COVERITY_SCAN_TOKEN }}&project=wshowlyrics" \
            -O coverity_tool.tgz
          tar xzf coverity_tool.tgz
          echo "$(pwd)/cov-analysis-linux64-*/bin" >> $GITHUB_PATH

      - name: Configure project
        run: meson setup build

      - name: Build with Coverity
        run: |
          cov-build --dir cov-int meson compile -C build

      - name: Submit to Coverity Scan
        run: |
          tar czf wshowlyrics.tgz cov-int
          curl --form token=${{ secrets.COVERITY_SCAN_TOKEN }} \
            --form email=${{ secrets.COVERITY_EMAIL }} \
            --form file=@wshowlyrics.tgz \
            --form version="$(git rev-parse --short HEAD)" \
            --form description="Automated weekly scan" \
            https://scan.coverity.com/builds?project=wshowlyrics

      - name: Upload build log (on failure)
        if: failure()
        uses: actions/upload-artifact@v4
        with:
          name: coverity-build-log
          path: cov-int/build-log.txt
```

## GitHub Secrets 설정 필요

Coverity Scan 등록 후 다음 secrets를 추가해야 합니다:

```
COVERITY_SCAN_TOKEN   # Coverity 프로젝트 페이지에서 확인
COVERITY_EMAIL        # 등록한 이메일 주소
```

## 분석 결과 확인

1. https://scan.coverity.com/projects/wshowlyrics 방문
2. 웹 대시보드에서 결과 확인:
   - Defects: 발견된 버그 목록
   - Outstanding: 아직 수정 안 된 버그
   - Fixed: 수정 완료된 버그
   - Dismissed: False positive로 처리한 버그
3. 각 버그마다 상세 설명, 코드 경로, 수정 제안 제공

## 예상되는 버그 탐지

Coverity가 현재 코드에서 잡을 수 있는 고급 버그:

1. **Race conditions** (pthread 사용 코드)
   - `src/translator/*/`의 async 번역 스레드
   - `src/main.c`의 메인 루프와 번역 스레드 간 경쟁

2. **Use after free** (복잡한 경우)
   - `lyrics_data` 구조체 해제 후 번역 스레드에서 접근

3. **Resource leak** (CURL, FILE 등)
   - 에러 경로에서 리소스 해제 누락

4. **Null dereference** (깊은 호출 체인)
   - 여러 함수를 거쳐 전달되는 포인터

5. **Buffer overflow** (정적 분석으로 탐지 가능한 경우)

## 현재 도구들과의 관계

```
일일 CI (빠름):
  - Semgrep:   보안 패턴 (30초)
  - Cppcheck:  기본 C 버그 (20초)

주간 CI (느림):
  - Coverity:  심층 분석 (1-2시간)
    → 웹 대시보드에서 결과 확인
    → GitHub PR에는 영향 없음
```

## 장단점

### 장점 ✅
- 최고 수준 분석 정확도
- Race condition, deadlock 탐지
- 웹 대시보드로 히스토리 추적
- False positive 매우 낮음
- 대규모 프로젝트 검증 (Linux Kernel 등)

### 단점 ❌
- **매우 느림**: 첫 분석 1-2시간
- **주 1회 제한**: 일일 CI에 사용 불가
- **복잡한 설정**: Coverity Build Tool 다운로드 필요
- **별도 서버**: scan.coverity.com에서만 결과 확인 (GitHub Security 탭 아님)
- **SARIF 미지원**: GitHub Code Scanning 연동 불가

## 구현 우선순위

- Priority: **LOW**
- Reason:
  - Cppcheck으로 기본 메모리 버그는 충분히 탐지 가능
  - 주간 스캔이라 빠른 피드백 불가
  - 별도 웹사이트 확인 필요 (불편함)
- When: 다음 조건 중 하나 충족 시 고려
  1. 멀티스레드 버그가 의심될 때
  2. 복잡한 버그가 프로덕션에서 발견될 때
  3. 프로젝트가 안정화되고 장기 유지보수 단계에 진입할 때

## 대안: Clang Thread Sanitizer

Coverity 대신 런타임 분석 도구 사용 가능:

```yaml
# .github/workflows/test.yml
- name: Run with Thread Sanitizer
  run: |
    CC=clang CFLAGS="-fsanitize=thread -g" meson setup build_tsan
    meson compile -C build_tsan
    ./build_tsan/lyrics --test
```

**장점**: Race condition을 런타임에 즉시 탐지
**단점**: 실제 실행 필요, 코드 커버리지에 의존

## 참고 링크

- 공식 사이트: https://scan.coverity.com/
- FAQ: https://scan.coverity.com/faq
- GitHub Actions 예시: https://scan.coverity.com/github_integration
- Synopsys 문서: https://www.synopsys.com/software-integrity/security-testing/static-analysis-sast.html
