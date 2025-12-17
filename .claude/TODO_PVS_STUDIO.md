# TODO: PVS-Studio 통합

## 개요
PVS-Studio는 C/C++ 정적 분석기로, 오픈소스 프로젝트는 무료 라이선스를 받을 수 있습니다.

## 장점
- **매우 낮은 오탐률**: 타이포, copy-paste 버그 탐지 특화
- **빠른 속도**: Clang보다 빠르고 Cppcheck보다 약간 느림
- **고급 분석**: Cppcheck보다 깊은 데이터 플로우 분석
- **SARIF 지원**: GitHub Code Scanning 네이티브 연동
- **무료 오픈소스 라이선스**: 조건 충족 시 영구 무료

## 오픈소스 무료 라이선스 조건

### 1. README.md 수정 필요
```markdown
## SAST Tools

[PVS-Studio](https://pvs-studio.com/en/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) - static analyzer for C, C++, C#, and Java code.
```

### 2. 커밋/PR 멘션
**PVS-Studio로 찾은 버그를 고칠 때만** 다음과 같이 커밋 메시지에 언급:
```
Fixed issues found by PVS-Studio
```

### 3. 제약사항
- ✅ 개인 창작 프로젝트만 (wshowlyrics 충족)
- ✅ GPL-3.0 오픈소스 라이선스 (충족)
- ⚠️ 상업 프로젝트/기업 프로젝트/포크는 불가
- 1년 유효 (매년 갱신 가능)
- 프로젝트당 1개 라이선스

## 라이선스 신청 방법

1. https://pvs-studio.com/en/order/open-source-license/ 방문
2. "Request Free License" 클릭
3. 프로젝트 GitHub URL 제출: `https://github.com/unstable-code/lyrics`
4. 라이선스 키 이메일로 수신 (즉시)
5. GitHub Secrets에 `PVS_STUDIO_LICENSE_KEY` 추가

## GitHub Actions 통합 예시

```yaml
sast-pvs-studio:
  name: SAST - PVS-Studio
  if: ${{ github.event.workflow_run.conclusion == 'success' }}
  runs-on: ubuntu-latest
  permissions:
    contents: read
    security-events: write
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

    - name: Install PVS-Studio
      run: |
        wget -q -O - https://files.pvs-studio.com/etc/pubkey.txt | sudo apt-key add -
        sudo wget -O /etc/apt/sources.list.d/viva64.list \
          https://files.pvs-studio.com/etc/viva64.list
        sudo apt-get update
        sudo apt-get install -y pvs-studio

    - name: Activate PVS-Studio license
      run: |
        pvs-studio-analyzer credentials ${{ secrets.PVS_STUDIO_LICENSE_KEY }}

    - name: Configure project
      run: meson setup build

    - name: Create compile_commands.json
      run: meson compile -C build --ninja-args=-t compdb > compile_commands.json

    - name: Run PVS-Studio analysis
      run: |
        pvs-studio-analyzer analyze \
          -j8 \
          -o project.log \
          --compile-commands compile_commands.json

    - name: Convert to SARIF
      run: |
        plog-converter -t sarif \
          -o pvs-studio.sarif \
          project.log

    - name: Upload SARIF file
      uses: github/codeql-action/upload-sarif@v4
      if: always()
      with:
        sarif_file: pvs-studio.sarif
        category: pvs-studio
```

## 예상되는 버그 탐지

PVS-Studio가 현재 코드에서 잡을 수 있는 버그:

1. **Memory leak** (Cppcheck이 놓칠 수 있는 복잡한 경우)
2. **Copy-paste errors** (변수명만 바뀌고 로직은 같은 경우)
3. **Suspicious type casts**
4. **Null pointer dereference** (더 정확한 탐지)
5. **Buffer overflow** (static analysis)

## 현재 Semgrep + Cppcheck와의 관계

```
Semgrep:       보안 취약점 (OWASP, path injection 등)
Cppcheck:      기본 C/C++ 버그 (메모리 누수, 버퍼 오버플로우)
PVS-Studio:    고급 버그 (copy-paste, 복잡한 데이터 플로우)
```

**3중 방어선 구축 가능**

## 구현 우선순위

- Priority: **MEDIUM**
- Reason: Cppcheck으로 기본 버그는 충분히 탐지 가능, PVS-Studio는 추가 보험
- When: Cppcheck 실행 결과를 확인한 후 오탐이 많거나 놓치는 버그가 있으면 추가

## 참고 링크

- 공식 사이트: https://pvs-studio.com/
- 오픈소스 라이선스: https://pvs-studio.com/en/order/open-source-license/
- 문서: https://pvs-studio.com/en/docs/
- GitHub Actions 예시: https://pvs-studio.com/en/docs/manual/0039/
