# TODO: GitHub Release 패키지 배포 자동화

## 개요
GitHub 릴리스에 deb, rpm, AppImage 패키지를 자동으로 빌드하고 첨부하며, COPR/PPA를 통해 패키지 레포지토리를 제공하여 다양한 배포판에서 쉽게 설치할 수 있도록 함.

**현재 상태**: Phase 1-6 완료 (AppImage 제외), Phase 7 완료 (COPR/PPA)
**우선순위**: Low (AppImage만 남음)
**남은 작업 시간**: AppImage 구현 (2-3시간)

## 요구사항
- ✅ .deb (Ubuntu) 빌드 (x86_64 + aarch64) - **완료**
- ✅ .rpm (Fedora/RHEL) 빌드 (x86_64 + aarch64) - **완료**
- ❌ AppImage (Universal) 빌드 (x86_64 + aarch64) - **미구현**
- ✅ GPG 서명 추가 - **완료** (deb, rpm)
- ✅ Ubuntu 22.04 LTS 타겟 - **완료**
- ✅ ARM64 (aarch64) 아키텍처 지원 - **완료**
- ✅ COPR 레포지토리 (Fedora) - **완료**
- ✅ PPA 레포지토리 (Ubuntu) - **완료**

## 현재 상황

### ✅ 완료된 배포 채널
1. **AUR (Arch User Repository)**
   - wshowlyrics (stable): 태그 릴리스, sha256sums 체크섬 검증
   - wshowlyrics-git (nightly): master 푸시마다 자동 업데이트
   - ARM64 지원: `arch=('x86_64' 'aarch64')`

2. **GitHub Releases**
   - .deb 패키지: amd64, arm64 (GPG 서명)
   - .rpm 패키지: x86_64, aarch64 (GPG 서명)
   - 자동 업로드 및 릴리스 노트 생성

3. **COPR (Fedora/RHEL)**
   - copr-release.yml: 태그 릴리스 시 stable 배포
   - copr-publish.yml: master 푸시 시 nightly 배포
   - 자동 빌드: x86_64, aarch64

4. **PPA (Ubuntu)**
   - ppa-release.yml: 태그 릴리스 시 배포
   - Ubuntu 22.04 LTS, 24.04 LTS 지원
   - 자동 빌드: amd64, arm64

### ❌ 미구현
- **AppImage**: x86_64, aarch64 빌드 및 GPG 서명

### ⚠️ 해결된 이슈
- 바이너리 이름 불일치: `lyrics` 빌드 → `/usr/bin/wshowlyrics` 설치 (패키지에서 해결됨)

---

## 완료된 작업 (Phase 1-7) ✅

- **Phase 1-2**: GPG 서명 (B4CD9B155AA3124C), GitHub Releases 자동 빌드 (.deb, .rpm)
- **Phase 3-6**: AUR 패키지 자동 업데이트 (stable + git), 문서화, 테스트 완료
- **Phase 7**: 패키지 레포지토리 - COPR (Fedora stable/nightly), PPA (Ubuntu stable)

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
depends=('wayland' 'cairo' 'pango' 'curl' 'fontconfig' 'libappindicator-gtk3' 'gdk-pixbuf2' 'glib2' 'json-c' 'openssl')
optdepends=('snixembed: System tray support for Swaybar'
            'libexttextcat: Language detection for translation')
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

**참조**: `meson.build` 및 `.github/workflows/build-packages.yml`에 정의됨

**주요 의존성** (AppImage 번들링 참고용):
- 렌더링: cairo, pango, pangocairo, fontconfig
- Wayland: wayland-client, wayland-protocols
- 네트워크: libcurl, openssl
- 데이터: json-c
- 시스템 트레이: libappindicator-gtk3, gdk-pixbuf-2.0
- DBus/MPRIS: glib2, gio-2.0 (playerctl 대체)
- 언어 감지 (선택): libexttextcat

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

## 남은 작업: AppImage 구현

### 목표
x86_64, aarch64 AppImage 빌드 및 GPG 서명 자동화

### 구현 계획 (2-3시간)

1. **build-appimage Job 추가** (`.github/workflows/build-packages.yml`)
   - [ ] Matrix 빌드: x86_64 (ubuntu-latest), aarch64 (ubuntu-24.04-arm)
   - [ ] Ubuntu 20.04 컨테이너 (glibc 호환성)
   - [ ] linuxdeploy 다운로드 및 AppDir 구조 생성
   - [ ] 의존성 자동 번들링:
     - cairo, pango, pangocairo (렌더링)
     - wayland-client (Wayland 통신)
     - libcurl, openssl (네트워크)
     - json-c (JSON 파싱)
     - libappindicator-gtk3, gdk-pixbuf-2.0 (시스템 트레이)
     - fontconfig (폰트 탐색)
     - glib2/gio-2.0 (DBus/MPRIS 통신, playerctl 대체)
     - libexttextcat (선택적, 언어 감지)
   - [ ] AppImage 생성 및 GPG detached signature (`.asc`)
   - [ ] 아티팩트 업로드

2. **upload-to-release Job 수정**
   - [ ] AppImage 의존성 추가
   - [ ] AppImage + .asc 파일 업로드
   - [ ] 릴리스 노트에 AppImage 검증 방법 추가

3. **테스트**
   - [ ] 로컬 Docker 테스트
   - [ ] CI/CD 검증
   - [ ] 실제 배포판에서 실행 테스트

### 예상 결과
```
wshowlyrics-0.6.11-x86_64.AppImage
wshowlyrics-0.6.11-x86_64.AppImage.asc
wshowlyrics-0.6.11-aarch64.AppImage
wshowlyrics-0.6.11-aarch64.AppImage.asc
```

---

**마지막 업데이트**: 2025-12-21
**상태**: Phase 1-7 완료 (deb, rpm, COPR, PPA), AppImage만 남음
**다음 단계**: build-appimage Job 구현
