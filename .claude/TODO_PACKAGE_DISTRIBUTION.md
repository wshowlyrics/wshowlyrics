# TODO: GitHub Release 패키지 배포 자동화

## 개요
GitHub 릴리스에 deb, rpm, AppImage 패키지를 자동으로 빌드하고 첨부하여 다양한 배포판에서 쉽게 설치할 수 있도록 함.

**현재 상태**: 계획 완료, 구현 대기 중
**우선순위**: Medium
**예상 작업 시간**:
- Phase 1-6 (필수): 7.5-9.5시간 (GPG 서명 추가로 +1.5시간)
- Phase 7 (선택): 2-3시간 추가

## 요구사항
- ✅ .deb (Ubuntu) 빌드 (x86_64 + aarch64)
- ✅ .rpm (Fedora/RHEL) 빌드 (x86_64 + aarch64)
- ✅ AppImage (Universal) 빌드 (x86_64 + aarch64)
- ✅ GPG 서명 추가
- ✅ Ubuntu 22.04 LTS 타겟
- ✅ ARM64 (aarch64) 아키텍처 지원

## 현재 상황
- ✅ 현재 릴리스: AUR 패키지만 자동 배포
- ✅ AUR ARM64 지원: wshowlyrics, wshowlyrics-git 모두 `arch=('x86_64' 'aarch64')` 설정 완료
- ✅ CI ARM64 빌드: GitHub Actions ARM64 runner (무료) 사용, 빌드 테스트 성공
- ⚠️ 바이너리 이름 불일치: `lyrics` 빌드 → `/usr/bin/wshowlyrics` 설치
- ⚠️ 버전 불일치: `meson.build`(0.6.5) vs 실제 태그(v0.6.10)

---

## 구현 체크리스트

### Phase 1: 사전 준비 (45분)

- [x] **GPG 키 생성 및 관리**
  - [x] GPG 키 생성: `B4CD9B155AA3124C` (wshowlyrics Package signing key)
  - [x] ASCII armored 형식으로 export
  - [x] GitHub Secrets에 추가:
    - `GPG_PRIVATE_KEY`: GPG 개인키 (패스프레이즈 없음, `--batch --yes` 사용)
  - [ ] **서명 대상** (GitHub Releases 패키지만):
    - `.deb` 패키지 (dpkg-sig)
    - `.rpm` 패키지 (rpm --addsign)
    - `AppImage` (GPG detached signature)
  - [ ] **서명 제외**:
    - AUR PKGBUILD - 선택사항 (stable: 낮은 우선순위, git: 불필요)
    - COPR nightly - COPR 자체 키로 자동 서명 (개발자 서명 불필요)

- [ ] **AUR 패키지 소스 검증 방식**
  - [ ] **Upstream 소스 서명 (validpgpkeys)**:
    - ❌ 사용 안함 (GitHub releases는 .sig 파일 미제공)
    - AUR Trusted User 리뷰 기준: 선택사항
  - [ ] **체크섬 검증** (AUR 표준 방식):
    - AUR stable: `updpkgsums` 명령으로 sha256sums 자동 계산 (필수)
    - AUR git: `sha256sums=('SKIP')` 유지 (VCS guidelines 표준)
  - [ ] **PKGBUILD 자체 서명** (makepkg --sign):
    - Stable: 선택사항 (낮은 우선순위)
    - Git: 불필요 (Git commit hash가 무결성 보장)

- [x] **버전 동기화**
  - [x] `meson.build` 버전 동기화 프로세스 문서화
  - [x] `src/constants.h` USER_AGENT_STRING 동기화 프로세스 문서화
  - [x] CLAUDE.md에 릴리스 전 체크리스트 추가 (다음 릴리스 때 반영)

### Phase 2: 워크플로우 생성 (3.5-4.5시간)

- [ ] **`.github/workflows/build-packages.yml` 생성**

  - [ ] **1. prepare-build Job 작성**
    - [ ] 버전 태그 감지 로직 (`v*.*.*` 패턴)
    - [ ] 버전 정보 outputs 설정
    - [ ] 태그 없을 시 스킵 로직

  - [ ] **2. build-deb Job 작성 (Matrix: amd64 + arm64)**
    - [ ] Matrix 빌드 설정:
      - [ ] amd64: ubuntu-latest
      - [ ] arm64: ubuntu-24.04-arm (GitHub 무료 ARM runner)
    - [ ] Ubuntu 22.04 컨테이너 설정
    - [ ] 빌드 의존성 설치 스크립트
    - [ ] Meson 빌드 단계
    - [ ] 패키지 구조 생성:
      - [ ] `DEBIAN/control` 파일 (`Architecture: any`)
      - [ ] `DEBIAN/postinst` 스크립트 (systemd reload)
      - [ ] `/usr/bin/wshowlyrics` 바이너리 복사
      - [ ] `/etc/wshowlyrics/settings.ini` 설정 파일
      - [ ] `/usr/lib/systemd/user/wshowlyrics.service`
      - [ ] `/usr/share/doc/wshowlyrics/` 문서
    - [ ] `dpkg-deb --build` 실행
    - [ ] GPG 서명 추가 (`dpkg-sig`)
    - [ ] 아티팩트 업로드 (`wshowlyrics_0.6.10_amd64.deb`, `wshowlyrics_0.6.10_arm64.deb`)

  - [ ] **3. build-rpm Job 작성 (Matrix: x86_64 + aarch64)**
    - [ ] Matrix 빌드 설정:
      - [ ] x86_64: ubuntu-latest
      - [ ] aarch64: ubuntu-24.04-arm (GitHub 무료 ARM runner)
    - [ ] Fedora latest 컨테이너 설정
    - [ ] RPM 빌드 도구 설치
    - [ ] `.spec` 파일 생성:
      - [ ] BuildRequires 섹션
      - [ ] Requires 섹션
      - [ ] BuildArch 생략 (자동으로 현재 아키텍처 사용)
      - [ ] %build 섹션 (meson 매크로)
      - [ ] %install 섹션
      - [ ] %post 섹션 (systemd)
      - [ ] %files 섹션
    - [ ] 소스 tarball 생성 (`git archive`)
    - [ ] `rpmbuild -ba` 실행
    - [ ] GPG 서명 추가 (`rpm --addsign`)
    - [ ] 아티팩트 업로드 (`wshowlyrics-0.6.10-1.fc40.x86_64.rpm`, `wshowlyrics-0.6.10-1.fc40.aarch64.rpm`)

  - [ ] **4. build-appimage Job 작성 (Matrix: x86_64 + aarch64)**
    - [ ] Matrix 빌드 설정:
      - [ ] x86_64: ubuntu-latest
      - [ ] aarch64: ubuntu-24.04-arm (GitHub 무료 ARM runner)
    - [ ] Ubuntu 20.04 컨테이너 설정 (glibc 호환성)
    - [ ] 빌드 의존성 설치
    - [ ] Meson 빌드 (정적 링킹 옵션)
    - [ ] linuxdeploy 다운로드
    - [ ] AppDir 구조 생성:
      - [ ] `AppRun` 스크립트
      - [ ] `wshowlyrics.desktop` 파일
      - [ ] `wshowlyrics.png` 아이콘 (임시로 기본 아이콘 사용)
      - [ ] `/usr/bin/wshowlyrics` 바이너리
      - [ ] `/usr/bin/playerctl` 번들링
      - [ ] 설정 파일 및 서비스 파일 포함
    - [ ] linuxdeploy로 의존성 자동 번들링
    - [ ] AppImage 생성
    - [ ] GPG detached signature 생성:
      - [ ] `gpg --detach-sign --armor wshowlyrics-<version>-<arch>.AppImage`
      - [ ] 출력: `wshowlyrics-<version>-<arch>.AppImage.asc`
    - [ ] 아티팩트 업로드:
      - [ ] `wshowlyrics-0.6.10-x86_64.AppImage` + `.asc`
      - [ ] `wshowlyrics-0.6.10-aarch64.AppImage` + `.asc`

  - [ ] **5. upload-to-release Job 작성**
    - [ ] 모든 빌드 job 의존성 설정
    - [ ] 아티팩트 다운로드 (deb, rpm, appimage)
    - [ ] `softprops/action-gh-release` 설정
    - [ ] 파일 첨부 경로 설정

### Phase 3: 기존 워크플로우 수정 및 AUR git 초기 설정 (1.5시간)

- [ ] **`.github/workflows/release.yml` 수정**
  - [ ] 릴리스 본문 템플릿 업데이트
  - [ ] Debian/Ubuntu 설치 방법 추가
  - [ ] Fedora/RHEL 설치 방법 추가
  - [ ] AppImage 사용 방법 추가
  - [ ] 기존 AUR 로직 유지
  - [ ] **AUR stable 패키지 체크섬 추가**:
    - [ ] 버전 업데이트: `sed -i "s/^pkgver=.*/pkgver=$VERSION/" PKGBUILD`
    - [ ] **체크섬 자동 계산**: `updpkgsums` (sha256sums 업데이트, 필수)
    - [ ] `.SRCINFO` 재생성
    - [ ] 커밋: `PKGBUILD`, `.SRCINFO`
    - [ ] AUR에 푸시
    - [ ] **보안**: GitHub tarball 무결성 검증 (MITM 공격 방지)

  - [ ] **AUR git 패키지 현재 상태 확인**:
    - ✅ `aur-publish.yml`이 매 master 푸시마다 자동 실행
    - ✅ pkgver 자동 계산: `r$(git rev-list --count HEAD).$(git rev-parse --short HEAD)`
    - ✅ PKGBUILD pkgver 필드 업데이트
    - ✅ `sha256sums=('SKIP')` 사용 (VCS guidelines 표준)
    - ✅ .SRCINFO 재생성 및 AUR 푸시
    - ✅ 추가 작업 불필요 (Git commit hash가 무결성 보장)

### Phase 4: 문서 업데이트 (1시간)

- [ ] **`README.md` 수정**
  - [ ] Installation 섹션 확장
  - [ ] Debian/Ubuntu 설치 가이드
  - [ ] Fedora/RHEL 설치 가이드
  - [ ] AppImage 사용 가이드
  - [ ] 각 방법의 장단점 설명
  - [ ] systemd 서비스 활성화 방법

- [ ] **`meson.build` 수정**
  - [ ] 버전을 `0.6.10`으로 업데이트
  - [ ] 다음 릴리스부터 태그와 동기화 유지

### Phase 5: 테스트 및 검증 (2-3시간)

- [ ] **로컬 Docker 테스트**
  - [ ] Ubuntu 22.04에서 .deb 빌드 테스트
  - [ ] Fedora에서 .rpm 빌드 테스트
  - [ ] Ubuntu 20.04에서 AppImage 빌드 테스트
  - [ ] 각 패키지 lintian/rpmlint 검증

- [ ] **CI/CD 테스트**
  - [ ] 테스트 브랜치에 커밋 및 푸시
  - [ ] 테스트 태그 생성 (예: `v0.6.11-test`)
  - [ ] 워크플로우 실행 모니터링
  - [ ] 빌드 로그 확인
  - [ ] 아티팩트 다운로드 및 검사

- [ ] **실제 릴리스 테스트**
  - [ ] v0.6.11 태그 생성
  - [ ] GitHub 릴리스 페이지 확인
  - [ ] 각 패키지 다운로드
  - [ ] Ubuntu에서 .deb 설치 테스트
  - [ ] Fedora에서 .rpm 설치 테스트
  - [ ] 다른 배포판에서 AppImage 실행 테스트
  - [ ] 의존성 자동 설치 확인
  - [ ] systemd 서비스 동작 확인

### Phase 6: 배포 및 공지 (30분)

- [ ] **공식 릴리스**
  - [ ] 모든 테스트 통과 확인
  - [ ] 릴리스 노트 최종 검토
  - [ ] 커밋 및 푸시
  - [ ] 공식 릴리스 발표

- [ ] **문서화**
  - [ ] AUR 패키지 설명 업데이트
  - [ ] GitHub README 배지 추가 (다운로드 수 등)
  - [ ] 릴리스 공지 (Reddit, Hacker News 등)

### Phase 7: 패키지 레포지토리 추가 (선택사항, 2-3시간)

> **참고**: GitHub Releases로 충분할 수 있으나, 패키지 매니저로 직접 설치/업데이트를 원하는 사용자를 위해 공식 레포지토리 추가를 고려

#### 7.1 Launchpad PPA (Ubuntu) 설정

**개요**: 태그 릴리스만 PPA에 업로드 (stable only, 롤링 릴리스 없음)

- [x] **Launchpad 계정 설정**
  - [x] https://launchpad.net 계정 생성
  - [x] PPA 생성: `ppa:unstable-code/wshowlyrics` (stable만)
  - [x] GPG 키 Launchpad에 등록
  - [x] Keyserver 업로드 및 동기화 완료

- [ ] **debian/ 디렉토리 구조 생성**

  **debian/control** (패키지 메타데이터):
  - [ ] Source 섹션: Build-Depends 목록
  - [ ] Package 섹션: Runtime Depends 목록
  - [ ] Architecture: any (amd64, arm64 자동 빌드)

  **debian/rules** (빌드 규칙):
  ```makefile
  #!/usr/bin/make -f
  %:
      dh $@ --buildsystem=meson

  override_dh_auto_configure:
      dh_auto_configure -- --prefix=/usr
  ```

  **debian/changelog** (버전 히스토리):
  - [ ] `dch` 명령으로 자동 생성
  - [ ] 형식: `wshowlyrics (0.6.10-1~jammy1) jammy; urgency=medium`

  **debian/copyright** (라이선스):
  - [ ] GPL-3.0-or-later 명시

  **debian/compat**:
  - [ ] `13` (debhelper 버전)

  **debian/source/format**:
  - [ ] `3.0 (quilt)` (표준 소스 포맷)

- [ ] **Ubuntu 버전별 타겟**
  - [ ] 22.04 LTS (Jammy Jellyfish) - 2027년까지 지원
  - [ ] 24.04 LTS (Noble Numbat) - 2029년까지 지원
  - [ ] 각 버전별로 별도 소스 패키지 빌드

- [ ] **PPA 업로드 자동화**

  **`.github/workflows/ppa-upload.yml` 생성**:

  - [ ] **트리거**: 태그 릴리스 (`v*.*.*`) 감지 시에만
  - [ ] **의존성 설치**: `devscripts`, `debhelper`, `dput-ng`
  - [ ] **GPG 키 가져오기**:
    ```yaml
    - name: Import GPG key
      run: |
        echo "${{ secrets.GPG_PRIVATE_KEY }}" | base64 -d | gpg --import
        echo "use-agent" >> ~/.gnupg/gpg.conf
        echo "pinentry-mode loopback" >> ~/.gnupg/gpg.conf
    ```

  - [ ] **debian/changelog 업데이트**:
    ```bash
    VERSION="0.6.10"
    for RELEASE in jammy noble; do
      dch -v "${VERSION}-1~${RELEASE}1" \
          -D ${RELEASE} \
          "Release ${VERSION}" \
          --force-distribution
    done
    ```

  - [ ] **소스 패키지 빌드**:
    ```bash
    debuild -S -sa -k<GPG_KEY_ID>
    # -S: source only
    # -sa: include original tarball
    # -k: GPG key for signing
    ```

  - [ ] **dput 설정**:
    ```ini
    [wshowlyrics-ppa]
    fqdn = ppa.launchpad.net
    method = ftp
    incoming = ~unstable-code/ubuntu/wshowlyrics/
    login = anonymous
    allow_unsigned_uploads = 0
    ```

  - [ ] **PPA 업로드**:
    ```bash
    for CHANGES in ../*.changes; do
      dput wshowlyrics-ppa "$CHANGES"
    done
    ```

  - [ ] **Launchpad 자동 빌드 대기**: 각 Ubuntu 버전별로 amd64, arm64 빌드

- [ ] **사용자 설치 방법 문서화**
  ```bash
  sudo add-apt-repository ppa:unstable-code/wshowlyrics
  sudo apt update
  sudo apt install wshowlyrics
  ```

**장점**:
- Ubuntu 사용자가 `apt` 명령으로 설치 가능
- 자동 업데이트 (`apt upgrade`)
- Launchpad가 자동으로 빌드 (amd64, arm64)
- 여러 Ubuntu 버전 지원 (LTS 위주)

**단점**:
- Ubuntu만 지원
- 초기 설정 복잡 (debian/ 디렉토리 전체 구조)
- 빌드 시간 지연 (Launchpad 빌드 큐)
- **롤링 릴리스 없음** (태그 릴리스만, nightly 미제공)

**참고**: Ubuntu 사용자가 nightly 빌드를 원할 경우:
- 소스에서 직접 빌드 권장
- 또는 GitHub Actions artifacts (개발자용, 7일 보관)

#### 7.2 COPR (Fedora/RHEL) 설정

**개요**: Fedora 공식 커뮤니티 빌드 시스템, AUR/PPA와 유사

- [x] **COPR 계정 설정**
  - [x] https://copr.fedorainfracloud.org 계정 생성 (Fedora Account)
  - [x] 프로젝트 생성:
    - [x] `unstable-code/wshowlyrics` (stable, 태그 릴리스만)
  - [x] **API 토큰 생성 및 GitHub Secrets 추가** (GitHub Actions 자동화용)
    - [x] COPR Settings → API → "Generate new token"
    - [x] `~/.config/copr` 파일 다운로드
    - [x] GitHub Secrets 등록:
      - [x] `COPR_CONFIG`: 전체 copr 설정 파일 내용 (180일마다 갱신 필요)
      - 형식: `~/.config/copr` 파일 전체를 복사-붙여넣기
    - [x] API 문서: https://copr.fedorainfracloud.org/api/
  - [x] 빌드 타겟 선택:
    - [x] Fedora 42, 43 (Follow Fedora branching 활성화)
  - [x] 빌드 아키텍처 선택 (Build Chroots):
    - [x] x86_64 (필수)
    - [x] aarch64 (ARM64 지원)
    - [x] COPR가 각 아키텍처별로 자동 빌드

- [ ] **SRPM (소스 RPM) 준비**
  - [ ] `.spec` 파일 개선 (COPR 요구사항)
  - [ ] Stable용: 소스 tarball URL 설정 (GitHub releases)
  - [ ] Nightly용: Git 소스 설정 (master 브랜치)
  - [ ] SRPM 생성 스크립트

- [ ] **COPR 자동 업로드 (Stable)**
  - [ ] `.github/workflows/copr-upload.yml` 생성
  - [ ] 태그 릴리스 시에만 실행
  - [ ] COPR 설정 파일 생성:
    ```yaml
    - name: Setup COPR config
      run: |
        mkdir -p ~/.config
        echo "${{ secrets.COPR_CONFIG }}" > ~/.config/copr
    ```
  - [ ] `copr-cli` 도구로 SRPM 업로드
  - [ ] COPR에서 자동 빌드 (x86_64, aarch64 등)
  - [ ] 빌드 성공 시 레포지토리 배포
  - [ ] **GPG 서명**: COPR가 자체 키로 자동 서명 (별도 작업 불필요)

- [ ] **COPR Auto-rebuild (Nightly/Git 버전)**
  - [ ] COPR Settings → Integrations에서 webhook URL 생성
  - [ ] GitHub repository에 webhook 추가
  - [ ] Auto-rebuild 활성화
  - [ ] Trigger: master 브랜치 push 이벤트
  - [ ] 빌드 소스: Git clone (latest commit)
  - [ ] AUR의 `wshowlyrics-git`와 동일한 역할
  - [ ] **GPG 서명**: COPR 자체 키로 자동 서명 (매 커밋마다 개발자 서명 불필요)

- [ ] **사용자 설치 방법 문서화**

  **Stable 버전**:
  ```bash
  sudo dnf copr enable unstable-code/wshowlyrics
  sudo dnf install wshowlyrics
  ```

  **Nightly/개발 버전**:
  ```bash
  sudo dnf copr enable unstable-code/wshowlyrics-nightly
  sudo dnf install wshowlyrics
  ```

**장점**:
- Fedora/RHEL 사용자가 `dnf` 명령으로 설치 가능
- 자동 업데이트 (`dnf upgrade`)
- 여러 아키텍처 자동 빌드
- GitHub releases 연동 가능
- **Git 통합 및 Auto-rebuild 지원** (AUR -git 패키지와 동일)
- Stable + Nightly 두 레포지토리 운영 가능

**단점**:
- Fedora 계열만 지원
- 초기 .spec 파일 검증 필요
- 빌드 실패 시 디버깅 복잡

#### 7.3 자동화 전략 (권장)

**Option A: GitHub Actions 완전 자동화 (권장)**
```yaml
# .github/workflows/ppa-upload.yml
# 태그 릴리스 시 실행
jobs:
  upload-ppa:
    - debian/changelog 자동 업데이트
    - debuild -S로 소스 패키지 빌드
    - dput으로 Launchpad PPA 업로드
    - Ubuntu 22.04, 24.04 LTS만 (stable only)

# .github/workflows/copr-upload.yml
# 태그 릴리스 시 실행
jobs:
  upload-copr-stable:
    - copr-cli로 stable 프로젝트에 SRPM 업로드

# COPR webhook 설정 (수동, 한 번만)
# master 브랜치 push 시 자동 빌드
  copr-nightly:
    - GitHub webhook → COPR auto-rebuild
    - wshowlyrics-nightly 프로젝트
    - AUR의 wshowlyrics-git와 동일한 역할
```

**Option B: COPR 우선, PPA는 나중에**
- **COPR Nightly**: GitHub webhook으로 완전 자동 (우선 구현)
- **COPR Stable**: GitHub Actions로 자동 업로드
- **PPA Stable**: 나중에 추가 (Ubuntu 사용자 수요 확인 후)

**Option C: 수동 업로드**
- 릴리스 태그 생성 후 로컬에서 수동 업로드
- 안정성 우선, 자동화는 나중에

**권장 접근**:
1. **Phase 1-6 먼저 완료** (GitHub Releases - deb, rpm, AppImage)
2. **사용자 피드백 수집** (어떤 배포판 사용자가 많은지)
3. **수요가 많은 레포지토리부터 추가**:
   - **Fedora 사용자 많음** → COPR stable + nightly (AUR처럼 롤링 가능)
   - **Ubuntu 사용자 많음** → PPA stable만 (nightly 미제공)
4. **최종 배포 전략**:
   - **Arch**:
     - Stable: AUR wshowlyrics (sha256sums 검증) ✅
     - Nightly: AUR wshowlyrics-git (sha256sums=SKIP, Git commit hash) ✅
   - **Fedora**:
     - Stable: COPR wshowlyrics (COPR 자동 서명)
     - Nightly: COPR wshowlyrics-nightly (COPR 자동 서명)
   - **Ubuntu**:
     - Stable: PPA (개발자 GPG 서명)
     - Nightly: 없음 (PPA 제약)
   - **GitHub Releases** (모든 배포판):
     - 태그 릴리스 시: `.deb`, `.rpm`, `AppImage` (모두 개발자 GPG 서명)

---

## 상세 구현 노트

### AUR git 패키지 PKGBUILD 예시

**wshowlyrics-git/PKGBUILD**:
```bash
# Maintainer: unstable-code <assa0620@gmail.com>
pkgname=wshowlyrics-git
pkgver=r123.abc1234  # GitHub Actions가 매 푸시마다 업데이트
pkgrel=1
pkgdesc="Wayland-native lyrics display for MPD with online fallback (git version)"
arch=('x86_64')
url="https://github.com/unstable-code/lyrics"
license=('GPL-3.0-or-later')
depends=('wayland' 'cairo' 'pango' 'curl' 'fontconfig' 'libappindicator-gtk3' 'gdk-pixbuf2' 'playerctl')
optdepends=('snixembed: System tray support for Swaybar')
makedepends=('git' 'meson' 'ninja' 'wayland-protocols')
provides=('wshowlyrics')
conflicts=('wshowlyrics')
source=("git+https://github.com/unstable-code/lyrics.git")
sha256sums=('SKIP')  # Git 소스는 체크섬 불가능 (매번 변함)

build() {
    cd lyrics
    arch-meson . build
    meson compile -C build
}

package() {
    cd lyrics
    meson install -C build --destdir="$pkgdir"
}
```

**자동화 구조** (`.github/workflows/aur-publish.yml`):
```bash
# 매 master 푸시마다 실행:
# 1. pkgver 계산: r$(git rev-list --count HEAD).$(git rev-parse --short HEAD)
# 2. PKGBUILD 업데이트: sed -i "s/^pkgver=.*/pkgver=$VER/" PKGBUILD
# 3. .SRCINFO 재생성: makepkg --printsrcinfo > .SRCINFO
# 4. AUR 푸시: git commit -m "Update to $VER" && git push
```

**참고**:
- ✅ 매 커밋마다 PKGBUILD pkgver 필드 업데이트됨
- ✅ `sha256sums=('SKIP')` 사용 (VCS guidelines 표준)
- ✅ .SRCINFO도 매번 재생성됨
- ✅ AUR 사용자가 `yay -Syu`로 업데이트 감지 가능
- ✅ Git commit hash가 소스 무결성 보장 (추가 서명 불필요)

### 의존성 목록

**Debian/Ubuntu (.deb)**:
```
Depends: libwayland-client0, libcairo2, libpango-1.0-0, libcurl4,
         libfontconfig1, libappindicator3-1, libgdk-pixbuf-2.0-0,
         playerctl, libjson-c5, libssl3
Recommends: sway | hyprland
```

**Fedora/RHEL (.rpm)**:
```
Requires: wayland, cairo, pango, libcurl, fontconfig,
          libappindicator-gtk3, gdk-pixbuf2, playerctl,
          openssl-libs, json-c
Recommends: sway OR hyprland
```

### 패키지 명명 규칙

```
Debian:   wshowlyrics_<VERSION>_amd64.deb
          예: wshowlyrics_0.6.10_amd64.deb

RPM:      wshowlyrics-<VERSION>-<RELEASE>.<DIST>.<ARCH>.rpm
          예: wshowlyrics-0.6.10-1.fc40.x86_64.rpm

AppImage: wshowlyrics-<VERSION>-<ARCH>.AppImage
          예: wshowlyrics-0.6.10-x86_64.AppImage
```

### Critical Files

**새로 생성**:
- `.github/workflows/build-packages.yml` (~300줄)

**수정 필요**:
- `.github/workflows/release.yml` (릴리스 본문 ~20줄)
- `README.md` (Installation 섹션 ~50줄)
- `meson.build` (버전 1줄)

**참조 파일**:
- `meson.build` (의존성 목록, 빌드 설정)
- `wshowlyrics.service` (systemd 서비스)
- `settings.ini.example` (설정 템플릿)
- `.github/workflows/ci.yml` (빌드 참조)

### 알려진 이슈 및 해결책

**Issue 1: 바이너리 이름 불일치**
- 문제: `meson.build`는 `lyrics`로 빌드
- 해결: 패키지 빌드 시 `/usr/bin/wshowlyrics`로 복사

**Issue 2: AppImage systemd 서비스**
- 문제: AppImage는 시스템 파일 설치 불가
- 해결: 사용자 가이드 제공 (수동 복사)

**Issue 3: GPG 서명 키 관리**
- 문제: GitHub Actions에서 GPG 키 보안
- 해결: GitHub Secrets 사용, 워크플로우에서만 접근

---

## 예상 결과

릴리스 페이지 Assets:
```
✅ wshowlyrics_0.6.10_amd64.deb (내장 GPG 서명)
✅ wshowlyrics-0.6.10-1.fc40.x86_64.rpm (내장 GPG 서명)
✅ wshowlyrics-0.6.10-x86_64.AppImage
✅ wshowlyrics-0.6.10-x86_64.AppImage.asc (detached GPG 서명)
✅ Source code (zip)
✅ Source code (tar.gz)
```

**패키지 검증 방법**:
```bash
# .deb 패키지 GPG 서명 확인
dpkg-sig --verify wshowlyrics_0.6.10_amd64.deb

# .rpm 패키지 GPG 서명 확인
rpm --checksig wshowlyrics-0.6.10-1.fc40.x86_64.rpm

# AppImage GPG 서명 확인
gpg --verify wshowlyrics-0.6.10-x86_64.AppImage.asc wshowlyrics-0.6.10-x86_64.AppImage

# AUR stable 체크섬 확인 (자동)
git clone https://aur.archlinux.org/wshowlyrics.git
cd wshowlyrics
makepkg -si  # sha256sums 자동 검증

# AUR git 소스 확인 (자동)
git clone https://aur.archlinux.org/wshowlyrics-git.git
cd wshowlyrics-git
makepkg -si  # Git commit hash로 무결성 보장
```

사용자 혜택:
- Arch 외 배포판 사용자도 쉽게 설치 가능
- AUR 없이도 최신 버전 사용 가능
- 배포판별 패키지 매니저로 자동 의존성 해결

**Phase 7 완료 시 추가 혜택**:
- Ubuntu: `apt install wshowlyrics` (PPA, stable만)
- Fedora/RHEL: `dnf install wshowlyrics` (COPR stable + nightly)
- 자동 업데이트 지원 (`apt upgrade`, `dnf upgrade`)

**패키지 검증 정책**:
- ✅ **개발자 GPG 서명** (GitHub Releases만):
  - `.deb`, `.rpm`, `AppImage` (태그 릴리스)
  - PPA: 소스 패키지 (debuild -S)
- ✅ **체크섬 검증** (AUR):
  - AUR stable: sha256sums (필수, updpkgsums 자동 계산)
  - AUR git: sha256sums=('SKIP') (VCS guidelines 표준)
- ✅ **COPR 자동 서명** (COPR 인프라 키):
  - COPR stable: 자동 서명
  - COPR nightly: 자동 서명

**Nightly 빌드 제공 현황**:
- Arch: ✅ AUR wshowlyrics-git (sha256sums=SKIP, Git commit hash 검증)
- Fedora: ✅ COPR wshowlyrics-nightly (COPR 자동 서명)
- Ubuntu: ❌ 미제공 (stable만, PPA 제약)

---

## 아키텍처별 빌드 방식 (x86_64 + aarch64)

### AUR (Arch User Repository)
**방식**: 단순히 `arch` 필드에 추가
```bash
arch=('x86_64' 'aarch64')
```

**특징**:
- ✅ 설정이 가장 간단 (한 줄 추가)
- ✅ 사용자가 직접 빌드하므로 별도 바이너리 불필요
- ✅ makepkg가 현재 시스템 아키텍처에서 자동 빌드
- ✅ 완료: wshowlyrics, wshowlyrics-git 모두 적용됨

### GitHub Releases (deb, rpm, AppImage)
**방식**: 각 아키텍처별로 **별도 빌드 필요**

**Matrix 빌드 전략**:
```yaml
strategy:
  matrix:
    include:
      - arch: x86_64
        runner: ubuntu-latest
      - arch: aarch64
        runner: ubuntu-24.04-arm  # GitHub 무료 ARM runner
```

**결과물**:
- `.deb`: `wshowlyrics_0.6.10_amd64.deb`, `wshowlyrics_0.6.10_arm64.deb`
- `.rpm`: `wshowlyrics-0.6.10-1.fc40.x86_64.rpm`, `wshowlyrics-0.6.10-1.fc40.aarch64.rpm`
- `AppImage`: `wshowlyrics-0.6.10-x86_64.AppImage`, `wshowlyrics-0.6.10-aarch64.AppImage`

**특징**:
- ⚠️ 각 아키텍처별로 빌드 job 실행
- ⚠️ 빌드 시간 2배 (병렬 실행으로 실제 시간은 약간만 증가)
- ✅ 사용자에게 바이너리 직접 제공 (빌드 불필요)

### COPR (Fedora/RHEL)
**방식**: Build Chroots 설정에서 아키텍처 선택

**설정 방법**:
1. COPR 프로젝트 설정 → Settings → Build Chroots
2. 각 배포판별로 x86_64, aarch64 체크박스 선택:
   - `fedora-40-x86_64` ✅
   - `fedora-40-aarch64` ✅
   - `epel-9-x86_64` ✅
   - `epel-9-aarch64` ✅

**특징**:
- ✅ 소스 패키지(SRPM) 하나만 업로드
- ✅ COPR가 선택된 모든 아키텍처에서 자동 빌드
- ✅ 빌드 실패 시 해당 아키텍처만 제외하고 배포 가능
- ✅ COPR 자체 GPG 키로 자동 서명

### PPA (Launchpad)
**방식**: `Architecture: any` 설정, Launchpad가 자동 빌드

**설정 방법**:
```debian
# debian/control
Architecture: any
```

**특징**:
- ✅ 소스 패키지 하나만 업로드
- ✅ Launchpad가 amd64, arm64 등 자동으로 빌드
- ✅ 각 Ubuntu 버전별 + 각 아키텍처별 = 조합 모두 자동 빌드
  - 예: jammy(22.04) amd64, jammy arm64, noble(24.04) amd64, noble arm64
- ⚠️ Nightly 빌드 미지원 (stable만)

### 요약 비교

| 패키지 형식 | 아키텍처 추가 방법 | 빌드 주체 | 복잡도 |
|------------|-------------------|-----------|--------|
| **AUR** | `arch=()` 필드 | 사용자 | ⭐ (매우 쉬움) |
| **GitHub Releases** | Matrix 빌드 | GitHub Actions | ⭐⭐⭐ (복잡) |
| **COPR** | Build Chroots 선택 | COPR 서버 | ⭐⭐ (쉬움) |
| **PPA** | `Architecture: any` | Launchpad | ⭐⭐ (쉬움) |

**결론**: AUR이 가장 간단하고, COPR/PPA는 서버가 자동으로 빌드해주므로 관리가 편함. GitHub Releases는 직접 빌드해야 하지만 완전한 제어 가능.

---

## 참고 자료

**패키지 빌드**:
- Debian Policy: https://www.debian.org/doc/debian-policy/
- RPM Packaging Guide: https://rpm-packaging-guide.github.io/
- AppImage Documentation: https://docs.appimage.org/
- linuxdeploy: https://github.com/linuxdeploy/linuxdeploy

**레포지토리 서비스**:
- Launchpad PPA: https://launchpad.net/
  - PPA 생성 가이드: https://help.launchpad.net/Packaging/PPA
  - dput 사용법: https://wiki.debian.org/dput
- Fedora COPR: https://copr.fedorainfracloud.org/
  - COPR 문서: https://docs.pagure.org/copr.copr/user_documentation.html
  - copr-cli: https://developer.fedoraproject.org/deployment/copr/copr-cli.html
  - Auto-rebuild (Git 통합): https://docs.pagure.org/copr.copr/user_documentation.html#scm-1
  - Webhook 설정: Settings → Integrations (GitHub, GitLab, Pagure 지원)

**CI/CD**:
- GitHub Actions: https://docs.github.com/en/actions
- GitHub Secrets: https://docs.github.com/en/actions/security-guides/encrypted-secrets

---

**마지막 업데이트**: 2025-12-20
**상태**: Phase 1 완료, Phase 2 (워크플로우 생성) 대기 중
**다음 단계**: `.github/workflows/build-packages.yml` 생성
