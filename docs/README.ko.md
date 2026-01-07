# Lyrics Overlay for Wayland
[![Build Status](https://github.com/unstable-code/lyrics/actions/workflows/ci.yml/badge.svg)](https://github.com/unstable-code/lyrics/actions/workflows/ci.yml)
[![GitHub release](https://img.shields.io/github/v/release/unstable-code/lyrics)](https://github.com/unstable-code/lyrics/releases)
[![AUR version](https://img.shields.io/aur/version/wshowlyrics-git)](https://aur.archlinux.org/packages/wshowlyrics-git)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux-green.svg)](https://www.linux.org/)
[![Wayland](https://img.shields.io/badge/Wayland-Only-orange.svg)](https://wayland.freedesktop.org/)
<a href="https://scan.coverity.com/projects/unstable-code-lyrics">
  <img alt="Coverity Scan Build Status" src="https://scan.coverity.com/projects/32766/badge.svg?flat=1">
</a>
<a href="https://sonarcloud.io/summary/new_code?id=unstable-code_lyrics">
  <img alt="Quality Gate Status"
       src="https://sonarcloud.io/api/project_badges/measure?project=unstable-code_lyrics&metric=alert_status"/>
</a>

<p align="center">
  <img src="https://img.shields.io/badge/Fedora-51A2DA?style=for-the-badge&logo=fedora&logoColor=white" alt="Fedora">
  <img src="https://img.shields.io/badge/Ubuntu-E95420?style=for-the-badge&logo=ubuntu&logoColor=white" alt="Ubuntu">
  <img src="https://img.shields.io/badge/Arch%20Linux-1793D1?style=for-the-badge&logo=arch-linux&logoColor=white" alt="Arch Linux">
</p>

<img width="696" height="77" alt="a65e765" src="https://github.com/user-attachments/assets/e33bb35e-24f3-4632-811c-b7e55a0660a1" />

Wayland 기반 가사 오버레이 프로그램입니다. [wshowkeys 프로젝트를 기반](https://github.com/unstable-code/wshowkeys)으로 제작되었으며, [LyricsX](https://github.com/ddddxxx/LyricsX)에서 영감을 받았습니다.

## 기능

- **MPRIS 통합**: playerctl을 통해 현재 재생중인 곡 자동 감지 (mpv, Spotify, VLC 등 모든 MPRIS 호환 플레이어)
- **시스템 트레이 통합**: 시스템 트레이에 앨범 아트 표시 (Swaybar/Waybar)
  - MPRIS 메타데이터에서 앨범 아트 자동 로드
  - **iTunes API 자동 검색**: MPRIS에서 앨범 아트를 제공하지 않을 경우 iTunes Search API에서 자동으로 가져오기
  - 모든 소스에서 앨범 아트를 가져올 수 없을 경우 기본 음악 아이콘 표시
  - **컨텍스트 메뉴**: 시스템 트레이 아이콘을 우클릭하여 접근 가능:
    - 현재 트랙 정보 표시 (아티스트 - 제목)
    - 오버레이 표시/숨김 토글
    - 타이밍 오프셋 서브메뉴 (+100ms, -100ms, 리셋)
    - 설정 편집 ($EDITOR, $TERMINAL 환경변수 필요)
    - 종료
- **스마트 가사 검색**:
  - **로컬 파일 검색**: 현재 재생중인 파일과 같은 디렉토리 우선 검색
  - **온라인 자동 검색**: 로컬에 가사 파일이 없으면 [lrclib.net](https://lrclib.net)에서 자동으로 가사 가져오기
  - URL 디코딩으로 유니코드(한글, 일본어 등) 경로 지원
  - 파일명 기반 자동 매칭
- **번역 지원**: 다중 AI 제공자 API를 활용한 자동 가사 번역
  - 여러 번역 제공자 지원: OpenAI, DeepL, Google Gemini, Anthropic Claude
  - 스마트 캐싱 시스템 - 한 번 번역하면 영구 저장
  - 언어 감지 최적화 - 이미 목표 언어인 텍스트는 자동 제외
  - 표시 모드 설정 가능 (원문+번역, 번역만 표시)
  - 번역 텍스트 투명도 조절 가능
  - LRC, SRT, VTT 포맷 지원 (LRCX 제외)
- **카라오케 모드**: LRCX 포맷으로 단어별 타이밍 및 점진적 채움 효과 지원
  - 지나간 단어: 일반 색상 (이미 불렀음)
  - 현재 단어: 왼쪽에서 오른쪽으로 점진적으로 채워짐 (지금 부르는 중)
  - 아직 안 지나간 단어: 흐림 (아직 안 불렀음)
- **동기화된 가사**: LRC, LRCX, SRT, VTT 포맷 지원
- **실시간 싱크**: 음악 재생 위치에 따라 가사 자동 표시
- **Spotify 위치 드리프트 수정**: 트랙 변경 시 자동 위치 보정 (설정 가능한 대기 시간 포함, 일반적인 Spotify 재생 위치 문제 해결)
- **듀얼 모드 타이밍 오프셋**: 전역(지속) 및 세션별 타이밍 조정 (시각 지표 포함)
- Wayland 프로토콜 사용 (wlr-layer-shell)
- 투명 배경 지원
- 한글, 중국어, 일본어 등 모든 유니코드 문자 지원 (Pango)
- 화면 위치 조정 가능 (상/하/좌/우)
- 색상 및 폰트 커스터마이징

## 설치

### Arch Linux (AUR)

**안정 버전:**

```bash
yay -S wshowlyrics
```

**개발 버전 (최신):**

```bash
yay -S wshowlyrics-git
```

### Ubuntu/Debian (PPA)

PPA를 추가하고 설치:

```bash
sudo add-apt-repository ppa:unstable-code/wshowlyrics
sudo apt update
sudo apt install wshowlyrics
```

또는 [릴리즈](https://github.com/unstable-code/lyrics/releases)에서 `.deb` 다운로드:

```bash
sudo dpkg -i wshowlyrics_*_amd64.deb
sudo apt-get install -f  # 의존성 설치
```

### Fedora/RHEL (COPR)

**안정 버전:**

```bash
sudo dnf copr enable unstable-code/wshowlyrics
sudo dnf install wshowlyrics
```

**나이틀리 빌드 (최신 개발 버전):**

```bash
sudo dnf copr enable unstable-code/wshowlyrics-nightly
sudo dnf install wshowlyrics
```

또는 [릴리즈](https://github.com/unstable-code/lyrics/releases)에서 `.rpm` 다운로드:

```bash
sudo dnf install wshowlyrics-*.rpm
```

### 수동 설치 (Arch Linux)

의존성 설치:

```bash
sudo pacman -S cairo curl fontconfig pango wayland wayland-protocols meson ninja playerctl \
               libappindicator-gtk3 gdk-pixbuf2
```

빌드 및 설치:

```bash
meson setup build
meson compile -C build
sudo install -Dm755 build/lyrics /usr/bin/wshowlyrics
```

### 수동 설치 (Fedora)

의존성 설치:

```bash
sudo dnf install cairo-devel curl libcurl-devel fontconfig-devel pango-devel \
                 wayland-devel wayland-protocols-devel \
                 libappindicator-gtk3-devel gdk-pixbuf2-devel \
                 json-c-devel openssl-devel \
                 meson ninja-build playerctl
```

빌드 및 설치:

```bash
meson setup build
meson compile -C build
sudo install -Dm755 build/lyrics /usr/bin/wshowlyrics
```

### 수동 설치 (Ubuntu/Debian)

의존성 설치:

```bash
sudo apt install libcairo2-dev libcurl4-openssl-dev libfontconfig1-dev libpango1.0-dev \
                 libwayland-dev wayland-protocols \
                 libappindicator3-dev libgdk-pixbuf2.0-dev \
                 meson ninja-build playerctl
```

빌드 및 설치:

```bash
meson setup build
meson compile -C build
sudo install -Dm755 build/lyrics /usr/bin/wshowlyrics
```

## 사용 방법

### systemd 유저 서비스로 실행 (권장)

설치 후, wshowlyrics를 systemd 유저 서비스로 관리할 수 있습니다:

```bash
# 로그인 시 자동 시작 활성화
systemctl --user enable wshowlyrics.service

# 서비스 시작
systemctl --user start wshowlyrics.service

# 서비스 상태 확인
systemctl --user status wshowlyrics.service

# 서비스 중지
systemctl --user stop wshowlyrics.service

# 자동 시작 비활성화
systemctl --user disable wshowlyrics.service
```

**journalctl로 로그 확인:**

```bash
# 전체 로그 보기
journalctl --user -u wshowlyrics

# 실시간 로그 추적
journalctl --user -u wshowlyrics -f

# 최근 50줄 보기
journalctl --user -u wshowlyrics -n 50

# 오늘 로그만 보기
journalctl --user -u wshowlyrics --since today

# 우선순위 레벨별 로그 보기 (err, warning, info)
journalctl --user -u wshowlyrics -p err
```

systemd 서비스는 모든 `log_info()`, `log_warn()`, `log_error()` 출력을 자동으로 systemd journal에 기록하며, 타임스탬프와 우선순위 레벨도 자동으로 관리합니다.

### 수동 실행

명령줄에서 직접 wshowlyrics를 실행할 수도 있습니다:

```bash
# 기본 실행 - 자동으로 현재 재생중인 곡의 가사를 찾아 표시
wshowlyrics

# mpv로 음악 재생
mpv --force-window=yes song.mp3

# Spotify, VLC 등 다른 MPRIS 호환 플레이어도 동작
```

### 환경변수

wshowlyrics에서 사용하는 환경변수는 다음과 같습니다:

- **$EDITOR**: 설정 파일을 편집할 텍스트 에디터 (예: `nvim`, `vim`, `nano`)
  - 시스템 트레이 컨텍스트 메뉴에서 "설정 편집"을 클릭할 때 사용됩니다
  - 설정 편집 기능이 정상 작동하려면 필수입니다
  - 예시:
    ```bash
    export EDITOR=nvim
    export EDITOR=vim
    export EDITOR=nano
    ```

- **$TERMINAL**: 에디터를 실행할 터미널 에뮬레이터 (예: `konsole`, `foot`, `gnome-terminal`)
  - 시스템 트레이 메뉴에서 설정 에디터를 열 때 사용됩니다
  - 설정 편집 기능이 정상 작동하려면 필수입니다
  - 데스크톱 환경별 예시:
    ```bash
    # Sway
    export TERMINAL=foot

    # KDE Plasma
    export TERMINAL=konsole

    # GNOME
    export TERMINAL=gnome-terminal

    # Wayland 공통 방식 (XDG_CURRENT_DESKTOP 활용)
    if [[ "$XDG_CURRENT_DESKTOP" == 'Sway' ]]; then
        export TERMINAL=foot
    elif [[ "$XDG_CURRENT_DESKTOP" == 'KDE' ]]; then
        export TERMINAL=konsole
    elif [[ "$XDG_CURRENT_DESKTOP" == 'GNOME' ]]; then
        export TERMINAL=gnome-terminal
    fi
    ```

셸 설정 파일(`~/.bashrc`, `~/.zshrc` 등)이나 systemd 유저 환경에 추가하세요.

### 옵션

| 짧은 형식 | 긴 형식 | 설명 | 기본값 |
|-----------|---------|------|--------|
| `-h` | `--help` | 도움말 표시 | - |
| `-b COLOR` | `--background=COLOR` | 배경색 (#RRGGBB[AA] 형식) | `#00000080` (검정, 50% 투명) |
| `-f COLOR` | `--foreground=COLOR` | 전경색/텍스트색 (#RRGGBB[AA] 형식) | `#FFFFFFFF` (흰색, 불투명) |
| `-F FONT` | `--font=FONT` | 폰트 설정 | `"Sans 20"` |
| `-a POSITION` | `--anchor=POSITION` | 화면 위치 (top/bottom/left/right) | `bottom` |
| `-m PIXELS` | `--margin=PIXELS` | 화면 가장자리 여백 (픽셀) | `32` |

**색상 형식:**
- `#RRGGBB`: RGB 값 (예: `#FF0000` = 빨강)
- `#RRGGBBAA`: RGB + 투명도 (예: `#FF000080` = 빨강 50% 투명)
- `AA` 값: `00` = 완전 투명, `FF` = 불투명, `80` = 50% 투명

**폰트 형식:**
- 기본: `"Sans 20"` (Sans 폰트, 20pt)
- 볼드: `"Sans Bold 24"`
- 한글: `"Noto Sans CJK KR 18"`
- 일본어: `"Noto Sans CJK JP Bold 22"`

### 예제

```bash
# 도움말 보기
wshowlyrics -h
wshowlyrics --help

# 모든 캐시 삭제 후 종료
wshowlyrics --purge
wshowlyrics --purge=all

# 번역 캐시만 삭제 (번역 제공자 변경 후 유용)
wshowlyrics --purge=translations

# 앨범 아트 캐시만 삭제
wshowlyrics --purge=album-art

# 가사 캐시만 삭제
wshowlyrics --purge=lyrics

# MPRIS 모드로 실행 (기본)
wshowlyrics

# 한글 폰트로 실행
wshowlyrics -F "Noto Sans CJK KR 20"
wshowlyrics --font="Noto Sans CJK KR 20"

# 화면 상단에 표시
wshowlyrics -a top -m 50
wshowlyrics --anchor=top --margin=50

# 투명한 배경에 노란색 텍스트
wshowlyrics -b 00000066 -f FFFF00FF
wshowlyrics --background=00000066 --foreground=FFFF00FF

# 큰 볼드 폰트, 화면 하단
wshowlyrics -F "Sans Bold 28" -a bottom -m 40
wshowlyrics --font="Sans Bold 28" --anchor=bottom --margin=40
```

## 타이밍 오프셋 조절

D-Bus 제어 인터페이스를 사용하여 wshowlyrics를 재시작하지 않고 실시간으로 가사 싱크 타이밍을 조절할 수 있습니다. 전역(곡 변경 시 유지) 및 세션 기반 타이밍 조정을 모두 지원합니다.

### 개요

가사가 음악과 약간 맞지 않을 때 (오디오 지연, 플레이어 버퍼 시간 차이, 불완전한 가사 파일 등으로 인해), 실시간으로 타이밍 오프셋을 조절할 수 있습니다:

```bash
# 세션 오프셋 (임시 조정, 곡 변경 시 전역 오프셋으로 리셋됨)
# 가사를 100ms 느리게 표시 (늦게)
wshowlyrics-offset +100

# 가사를 100ms 빠르게 표시 (빨리)
wshowlyrics-offset -100

# 절대값으로 오프셋 설정
wshowlyrics-offset 200    # 정확히 200ms 지연으로 설정

# 절대값 0으로 설정
wshowlyrics-offset 0

# 전역 오프셋으로 리셋 (settings.ini에서 설정한 값)
wshowlyrics-offset reset
```

**참고:** **전역 오프셋**(모든 곡에 지속 적용)을 설정하려면 `~/.config/wshowlyrics/settings.ini`를 편집하세요:
```ini
[lyrics]
global_offset_ms = 500    # 모든 곡에 기본적으로 +500ms 오프셋 적용
```

**시각적 지표:**
타이밍 오프셋 프로그래스 바는 듀얼 컬러 지표와 함께 표시됩니다:
- 노란색: 전역 오프셋 (곡 변경 시 유지, settings.ini에서 설정)
- 하얀색: 세션 오프셋 (wshowlyrics-offset 명령으로 임시 조정)

### wshowlyrics-offset 스크립트

`wshowlyrics-offset` 헬퍼 스크립트는 D-Bus 제어 서비스에 쉽게 접근하는 방법을 제공합니다:

```bash
# 편리한 스크립트 설치
chmod +x wshowlyrics-offset
sudo cp wshowlyrics-offset /usr/local/bin/

# 사용 예시
wshowlyrics-offset +100   # 100ms 증가
wshowlyrics-offset -100   # 100ms 감소
wshowlyrics-offset 200    # 200ms로 설정
wshowlyrics-offset 0      # 0ms로 리셋
wshowlyrics-offset toggle # 오버레이 표시/숨김 전환
```

### D-Bus 서비스 상세 정보

스크립트는 D-Bus를 통해 wshowlyrics와 통신합니다:

- **서비스**: `org.wshowlyrics.Control`
- **객체 경로**: `/org/wshowlyrics/Control`
- **인터페이스**: `org.wshowlyrics.Control`
- **메서드**:
  - `AdjustTimingOffset(int16 offset_ms)` - 현재 오프셋에 더하거나 뺌
  - `SetTimingOffset(int16 offset_ms)` - 절대값 오프셋 설정
  - `ToggleOverlay()` - 오버레이 표시/숨김 전환
  - `SetOverlay(boolean visible)` - 오버레이 표시/숨김

### Sway 통합

`~/.config/sway/config`에 다음 바인딩 추가:

```sway
# 타이밍 오프셋 조절
bindsym $mod+Plus exec wshowlyrics-offset +100
bindsym $mod+Minus exec wshowlyrics-offset -100
bindsym $mod+0 exec wshowlyrics-offset 0
```

### Hyprland 통합

`~/.config/hypr/hyprland.conf`에 다음 바인딩 추가:

```conf
# 타이밍 오프셋 조절 (넘패드 키 사용)
bind = $mainMod, KP_Add, exec, wshowlyrics-offset +100
bind = $mainMod, KP_Subtract, exec, wshowlyrics-offset -100
bind = $mainMod, KP_0, exec, wshowlyrics-offset 0

# 대안: 일반 키 사용
bind = $mainMod SHIFT, equal, exec, wshowlyrics-offset +100
bind = $mainMod, minus, exec, wshowlyrics-offset -100
bind = $mainMod, 0, exec, wshowlyrics-offset 0
```

### 동작 방식

- **누적 조정**: `+` 또는 `-` 접두사를 사용하여 현재 오프셋에 더하거나 뺌 (예: `+100` 후 `+100` = 총 `+200ms`)
- **절대값**: 접두사 없는 명령은 정확한 오프셋 값을 설정 (예: `500`은 오프셋을 정확히 500ms로 설정)
- **자동 리셋**: 새 트랙이 재생되면 오프셋이 자동으로 0ms로 리셋됨
- **범위**: 유효 범위는 -10000ms ~ +10000ms (-10초 ~ +10초)

## 오버레이 토글 (가사 숨김/표시)

D-Bus 제어 인터페이스를 사용하여 프로그램을 종료하지 않고 일시적으로 가사 오버레이를 숨기거나 표시할 수 있습니다.

### 개요

프레젠테이션, 화면 녹화, 프라이버시 보호 등을 위해 가사를 일시적으로 숨기고 싶을 때, 실시간으로 오버레이 표시 여부를 전환할 수 있습니다. 가사 로드, 번역, 앨범아트 캐싱 등 모든 백그라운드 작업은 계속 실행되므로, 오버레이를 다시 활성화하면 즉시 현재 가사가 표시됩니다.

```bash
# 오버레이 표시/숨김 토글
wshowlyrics-offset toggle

# 오버레이 표시
gdbus call --session --dest org.wshowlyrics.Control \
  --object-path /org/wshowlyrics/Control \
  --method org.wshowlyrics.Control.SetOverlay true

# 오버레이 숨김
gdbus call --session --dest org.wshowlyrics.Control \
  --object-path /org/wshowlyrics/Control \
  --method org.wshowlyrics.Control.SetOverlay false
```

### Sway 통합

`~/.config/sway/config`에 다음 바인딩 추가:

```sway
# 오버레이 표시/숨김 토글
bindsym $mod+Pause exec wshowlyrics-offset toggle
```

### Hyprland 통합

`~/.config/hypr/hyprland.conf`에 다음 바인딩 추가:

```conf
# 오버레이 표시/숨김 토글
bind = $mainMod, PAUSE, exec, wshowlyrics-offset toggle
```

### 동작 방식

- **시각적 피드백**: 오버레이가 숨겨지면 시스템 트레이 아이콘이 헤드폰 + 빨간 X 표시로 변경됩니다
- **백그라운드 작업**: 가사 로드, 번역, 앨범아트 캐싱은 계속 실행됩니다
- **즉시 복원**: 다시 활성화하면 지연 없이 즉시 현재 가사가 표시됩니다
- **자동 리셋**: 새 트랙이 재생되면 오버레이가 자동으로 다시 활성화됩니다
- **독립적 동작**: 타이밍 오프셋 조절과 함께 사용할 수 있습니다

### 다중 인스턴스 방지

wshowlyrics는 lock 파일(`/tmp/wshowlyrics.lock`)을 사용하여 여러 인스턴스가 동시에 실행되는 것을 방지합니다. 다른 인스턴스가 실행 중이라는 오류가 나타나지만 실제로는 실행 중이지 않다고 생각되면:

```bash
rm /tmp/wshowlyrics.lock
```

## 가사 파일 형식

### LRCX 형식 (카라오케)
<img width="332" height="90" alt="1f8b963" src="https://github.com/user-attachments/assets/32b4861a-7bec-43a8-bce7-67b0aaba4f6e" />

단어별 타이밍이 있는 카라오케 스타일 가사:

```lrcx
[ar:아티스트]
[ti:노래 제목]

[00:12.00][00:12.20]첫 [00:12.50]번째 [00:12.80]단어별 [00:13.00]타이밍
[00:17.00][00:17.15]카라오케 [00:17.40]스타일 [00:17.70]가사
```

- 첫 번째 타임스탬프: 줄 시작 시간
- 이후 타임스탬프들: 개별 단어 타이밍
- 단어들이 왼쪽에서 오른쪽으로 점진적으로 채워집니다
- `.lrcx` 파일 확장자 사용

**언필 효과 (깜빡임)**

특정 문자가 깜빡이거나 진동해야 하는 특수한 보컬 패턴에는 `[<MM:SS.xx]` 언필(unfill) 문법을 사용하세요:

```lrcx
[00:10.00]僕{ぼく}[00:10.50]の[00:11.00]S[<00:11.50][00:12.00][<00:12.50][00:13.00]OS[00:14.00]を
```

- `[<MM:SS.xx]`: 언필 타임스탬프 (`<` 접두사 주목)
- 언필 타임스탬프 직전의 문자가 0%와 50% 채움 사이에서 진동합니다
- 사용 예: 같은 문자가 보컬에서 길게 늘어나거나 반복될 때
- 언필 구간 동안 깜빡이는 효과 생성

**루비 텍스트(후리가나) 지원**

모든 가사 파일 형식(LRCX, LRC, SRT, VTT)에서 루비 텍스트를 지원합니다:

```lrcx
[00:12.00][00:12.34]心{こころ}[00:13.50]の[00:13.80]中{なか}
```

```lrc
[00:12.00]心{こころ}の中{なか}で　響{ひび}く
```

- 문법: `본문_텍스트{루비_텍스트}` - 루비 텍스트가 본문 위에 작은 글꼴로 표시됩니다
- 예시: `心{こころ}` - "心"(한자) 위에 "こころ"(히라가나) 표시
- 활용: 일본어 후리가나, 중국어 병음, 한국어 발음 표기
- 확장자와 무관하게 `{}` 문법을 사용하면 자동으로 루비 텍스트로 인식됩니다
- 루비 텍스트는 본문 텍스트 위에 절반 크기로 중앙 정렬되어 표시됩니다

### LRC 형식 (표준)
<img width="696" height="77" alt="a65e765" src="https://github.com/user-attachments/assets/b4e38b6d-f207-48e7-9d0b-36fb3d806869" />

동기화된 가사 파일 형식입니다:

```lrc
[ti:노래 제목]
[ar:아티스트]
[al:앨범]

[00:12.00]첫 번째 가사 줄
[00:17.50]두 번째 가사 줄
[00:23.00]세 번째 가사 줄
```

### SRT/VTT 형식
<img width="379" height="139" alt="image" src="https://github.com/user-attachments/assets/ec25f97d-5097-4cb9-b02a-3e682e66a2a5" />

자막 형식도 지원합니다:

**SRT 형식:**
```srt
1
00:00:12,000 --> 00:00:17,500
첫 번째 가사 줄

2
00:00:17,500 --> 00:00:23,000
두 번째 가사 줄
```

**VTT (WebVTT) 형식:**
```vtt
WEBVTT

00:00:12.000 --> 00:00:17.500
첫 번째 가사 줄

00:00:17.500 --> 00:00:23.000
두 번째 가사 줄
```

**인라인 번역 지원**

SRT와 VTT 파일 모두 줄 시작 부분에 `{번역}` 구문을 사용한 인라인 번역을 지원합니다:

```srt
1
00:00:12,000 --> 00:00:17,500
心の中で　響く
{마음속에서 울려 퍼진다}

2
00:00:17,500 --> 00:00:23,000
君の声が　聞こえる
{네 목소리가 들려}
```

- 문법: 자막 블록 내에서 **줄의 시작 부분**에 `{번역 텍스트}` 작성
- 번역은 원문 아래에 작은 글씨로 흐리게 표시됩니다
- DeepL API 번역과 독립적으로 작동 (API 키 불필요)
- 루비 텍스트(`본문{루비}`)와 인라인 번역을 함께 사용 가능
- SRT와 VTT 형식 모두 지원

### 가사 검색 과정

프로그램은 2단계 방식으로 가사를 찾습니다:

#### 1. 로컬 파일 검색 (우선순위)

`settings.ini`의 `extensions` 설정이 활성화되어 있으면 (기본값: 활성화), 프로그램은 다음 **우선순위**로 가사 파일을 자동으로 검색합니다:

1. **현재 재생중인 음악 파일과 같은 디렉토리** (최우선!)
   - 먼저 음악 파일명과 동일한 이름의 가사 파일 검색
   - 예: `song.mp3` → `song.lrcx`, `song.lrc`, 또는 `song.srt`
2. 제목 기반 검색 (현재 디렉토리부터)
3. `$XDG_MUSIC_DIR`
4. `~/.lyrics/`
5. `$HOME`

**로컬 검색 건너뛰기**: `extensions` 설정이 비어있으면 (비활성화), 프로그램은 로컬 파일 검색을 건너뛰고 온라인 제공자 폴백으로 바로 이동합니다.

파일명 형식 (검색 순서):
- `파일명.lrcx` / `파일명.lrc` / `파일명.srt` / `파일명.vtt` (추천! 음악 파일과 같은 이름)
- `제목.lrcx` / `제목.lrc` / `제목.srt` / `제목.vtt`
- `아티스트 - 제목.lrcx` / `아티스트 - 제목.lrc` / `아티스트 - 제목.srt` / `아티스트 - 제목.vtt`
- `아티스트/제목.lrcx` / `아티스트/제목.lrc` / `아티스트/제목.vtt`

**팁**:
- 가사 파일을 음악 파일과 **같은 이름, 같은 디렉토리**에 두세요!
- 유니코드(한글, 일본어 등) 파일명도 완벽 지원합니다
- 예: `/music/노래.mp3` + `/music/노래.lrc` ✅

#### 2. 온라인 가사 자동 가져오기

로컬에 가사 파일이 없으면 자동으로 [lrclib.net](https://lrclib.net)에서 동기화된 가사를 가져옵니다:

- **필수 조건**: 트랙에 **제목**과 **아티스트** 메타데이터가 모두 있어야 함
- **형식**: 타임스탬프가 있는 **동기화된 가사**(LRC 형식)만 사용
  - **일반 텍스트 가사는 무시됨** - 타임스탬프가 있는 동기화된 가사만 표시
  - 이를 통해 가사가 음악과 완벽하게 동기화됩니다
- **인터넷 연결이 없다면?** 온라인 검색을 건너뛰고 계속 진행
- **프라이버시**: 곡 메타데이터(제목, 아티스트, 앨범)만 lrclib.net API로 전송
- **개선된 오류 메시지**: 프로그램은 가사 검색 과정에서 시도한 작업을 명시합니다:
  - 검색했지만 찾지 못한 로컬 파일
  - 온라인 API 조회 (해당하는 경우)
  - 가사를 찾지 못한 이유 명확히 표시

### 앨범 아트

프로그램은 폴백 체인을 통해 시스템 트레이에 앨범 아트를 자동으로 표시합니다:

1. **MPRIS 메타데이터** (우선순위): 음악 플레이어가 제공하는 경우 MPRIS 메타데이터에서 앨범 아트 로드
2. **iTunes Search API 자동 검색**: MPRIS에서 앨범 아트를 제공하지 않으면 Apple iTunes Search API에서 자동으로 가져오기
3. **기본 아이콘**: 앨범 아트를 사용할 수 없는 경우 시스템 테마의 기본 음악 아이콘 표시

**플레이어 아이콘 배지**: 앨범 아트 표시에 플레이어 아이콘 배지가 포함되어 현재 재생 중인 미디어 플레이어를 표시합니다.

iTunes API 기능은 `settings.ini`에서 `enable_itunes = false`로 설정하여 비활성화할 수 있습니다.

## 번역 지원 (다중 제공자)

wshowlyrics는 여러 AI 제공자 API를 사용하여 LRC 포맷 가사 파일의 자동 번역을 지원합니다.

### 설정

`~/.config/wshowlyrics/settings.ini`에 다음 설정을 추가하세요:

```ini
[translation]
# 번역 제공자 및 모델
# 사용 가능한 제공자:
#   - gpt-4o-mini: OpenAI GPT-4o Mini (권장, 빠르고 비용 효율적)
#   - gpt-4o: OpenAI GPT-4o (더 높은 정확도)
#   - gpt-3.5-turbo: OpenAI GPT-3.5 Turbo (구형 모델, 비추천)
#   - deepl: DeepL API (https://www.deepl.com/docs-api)
#   - gemini-2.5-flash: Google Gemini 2.5 Flash 모델
#   - gemini-2.5-pro: Google Gemini 2.5 Pro 모델
#   - claude-sonnet-4-5: Anthropic Claude Sonnet 4.5
#   - claude-opus-4-5: Anthropic Claude Opus 4.5
#   - claude-haiku-4-5: Anthropic Claude Haiku 4.5
#   - false: 번역 비활성화
provider = gpt-4o-mini

# 선택한 제공자의 API 키
# 제공자별 웹사이트에서 획득 가능:
#   - OpenAI: https://platform.openai.com/api-keys
#   - DeepL: https://www.deepl.com/pro-api
#   - Google Gemini: https://aistudio.google.com/apikey
#   - Anthropic Claude: https://console.anthropic.com/
api_key = your-api-key-here

# 번역 대상 언어
# 지원 언어 코드:
#   - OpenAI: 언어 코드 사용 (예: EN, KO, JA, ZH, ES, FR, DE)
#   - DeepL: https://developers.deepl.com/docs/getting-started/supported-languages
#   - Google Gemini: 언어 코드 사용 (예: EN, KO, JA, ZH, ES, FR, DE)
#   - Anthropic Claude: 언어 코드 사용 (예: EN, KO, JA, ZH, ES, FR, DE)
# 주요 예시: EN, KO, JA, ZH-HANS (중국어 간체), ZH-HANT (중국어 번체), ES, FR, DE
target_language = KO

# 번역 표시 모드
# 옵션:
#   both - 원문과 번역 모두 표시 (번역은 원문 아래에 작은 글씨로 표시)
#   translation_only - 번역만 표시 (원문 숨김)
translation_display = both

# 번역 텍스트 투명도 (0.0 - 1.0)
# 번역 텍스트의 가시성 조절
# 0.7 = 70% 불투명도 (기본값, 약간 투명)
# 0.9 = 90% 불투명도 (더 진하게)
# 1.0 = 100% 불투명도 (원문과 같은 진하기)
translation_opacity = 0.7

# 언어 감지 최적화
# 활성화되면 이미 목표 언어인 텍스트의 번역을 자동 제외
# 혼합 언어 가사(예: 영어 + 한국어)의 API 비용을 절감합니다
# libexttextcat를 사용하여 감지 (선택적 의존성 - 없어도 정상 작동)
# 옵션: true (활성화, 기본값) 또는 false (비활성화)
detect_language = true

# 레이트 리밋 지연 (직관적 형식)
# API 레이트 리밋을 피하기 위한 번역 요청 간 지연 조절
# 예시:
#   200: 요청 간 200밀리초
#   5s: 요청 간 5초 (1000ms = 1s)
#   10m: 분당 10개 요청 (60000ms / 10)
# 제공자별 추천 값:
#   OpenAI: 1s (무료 등급 분당 500개 요청)
#   DeepL: 200 또는 5s (분당 300개 요청 지원)
#   Gemini 무료: 10m (분당 10개 요청 제한)
#   Claude: 50m 이상 (계정 등급에 따라)
rate_limit = 10m

# 레이트 리밋 오류 시 최대 재시도 횟수
# 기본값: 3
# 레이트 리밋이 걸린 계정의 경우 더 높은 값 사용
max_retries = 3
```

### 지원하는 제공자

**OpenAI API**
- 방문: https://platform.openai.com/api-keys
- 지원 모델:
  - `gpt-4o-mini`: 권장 - 번역에 최적화된 빠르고 비용 효율적인 모델
  - `gpt-4o`: 더 높은 정확도, 더 강력한 성능
  - `gpt-3.5-turbo`: 구형 모델, 비추천
- 요금: 사용한 토큰 기반 종량제
- 문서: https://platform.openai.com/docs/guides/gpt

**DeepL API**
- 방문: https://www.deepl.com/pro-api
- 지원 언어: https://developers.deepl.com/docs/getting-started/supported-languages
- 무료 플랜: 월 50만 자
- 유료 플랜: 더 높은 문자 제한과 더 경쟁력 있는 가격

**Google Gemini**
- 방문: https://aistudio.google.com/apikey
- 사용 가능한 모델:
  - `gemini-2.5-flash`: 가장 빠르고 저렴, 실시간 번역에 이상적
  - `gemini-2.5-pro`: 가장 우수한 성능, 높은 정확도
- 무료 등급: 분당 15개 요청
- 문서: https://ai.google.dev/gemini-api/docs/models

**Anthropic Claude**
- 방문: https://console.anthropic.com/
- 사용 가능한 모델:
  - `claude-haiku-4-5`: 가장 빠르고 비용 효율적
  - `claude-sonnet-4-5`: 성능과 비용의 균형
  - `claude-opus-4-5`: 가장 우수한 성능, 높은 정확도
- 무료 평가판 및 유료 옵션 제공
- 문서: https://claude.com/pricing#api

### 기능

- **다중 제공자 지원**: OpenAI, DeepL, Google Gemini, Anthropic Claude 중 선택
- **스마트 캐싱**: 번역은 `~/.cache/wshowlyrics/` (또는 `$XDG_CACHE_HOME/wshowlyrics/`)에 캐시되어 재생시 재사용됨
- **영구 캐시**: 번역 캐시가 재부팅 후에도 유지되어 자주 듣는 곡의 API 비용 절감
- **캐시 관리**: 필요시 `--purge=translations` 명령으로 번역 캐시 삭제 가능
- **언어 감지 최적화**: 이미 목표 언어인 텍스트는 자동으로 제외되어 혼합 언어 가사의 API 비용 절감
- **포맷 지원**: 제공자 기반 번역은 LRC 포맷 파일에 적용됩니다. SRT와 VTT 파일은 줄 시작 부분에 `{번역}` 구문을 사용한 인라인 번역을 지원합니다 (API 불필요). LRCX 포맷은 제외됩니다.
- **레이트 리밋**: API 할당량을 지키기 위한 설정 가능한 레이트 리밋
- **자동 재시도**: 레이트 리밋 오류 발생 시 자동 재시도 (시도 횟수 설정 가능)
- **비용 효율적**: 곡당 한 번만 번역되고 캐시 삭제 전까지 무한 재사용
- **표시 모드 설정**: 원문+번역 또는 번역만 표시 중 선택 가능
- **투명도 조절**: 번역 텍스트의 가시성 조절 가능

### 설정 예시

**OpenAI 설정:**
```ini
[translation]
provider = gpt-4o-mini
api_key = sk-proj-your-openai-api-key
target_language = KO
rate_limit = 1s
detect_language = true
```

**DeepL 설정:**
```ini
[translation]
provider = deepl
api_key = your-deepl-api-key
target_language = KO
rate_limit = 200
```

**Google Gemini 설정 (무료 등급):**
```ini
[translation]
provider = gemini-2.5-flash
api_key = your-gemini-api-key
target_language = KO
rate_limit = 10m
max_retries = 3
```

**Claude 설정:**
```ini
[translation]
provider = claude-sonnet-4-5
api_key = your-anthropic-api-key
target_language = KO
rate_limit = 50m
```

### 출력 예시

일본어 가사를 `target_language = KO`로 설정하면:

```
원문:     心の中で　響く
번역:     마음속에서 울려 퍼진다
```

번역은 원문 가사 아래에 작은 글씨로, 약간 흐리게 표시됩니다.

## 의존성

### 빌드 시 필요
- cairo
- curl (libcurl)
- fontconfig
- pango
- pangocairo
- wayland-client
- wayland-protocols
- wlr-layer-shell protocol
- libappindicator-gtk3 (appindicator3-0.1)
- gdk-pixbuf-2.0
- meson & ninja

### 실행 시 필요
- curl (온라인 가사 가져오기용)
- playerctl (MPRIS 모드 사용시)
- Wayland compositor with wlr-layer-shell support (Sway, Hyprland 등)

### 선택적 의존성
- **libexttextcat**: 번역에서 언어 감지 최적화 기능 사용 (혼합 언어 가사의 번역 효율성 개선)
- **Swaybar 사용자**:
  - `snixembed` - Swaybar용 SNI (StatusNotifierItem) 브릿지 (선택사항, 트레이 아이콘 표시에 권장)
  - Swaybar 설정에서 시스템 트레이 활성화:
    ```
    bar {
        tray {
            icon_theme Adwaita
            tray_padding 2
        }
    }
    ```
- **Waybar 사용자**: 시스템 트레이가 기본적으로 지원되므로 추가 설정 불필요

## 빌드 방법 (개발용)

프로젝트에 기여하거나 최신 변경사항을 테스트하려는 경우:

```bash
meson setup build
meson compile -C build
```

### 개발 사용법

설치하지 않고 컴파일된 바이너리를 직접 실행:

```bash
# 기본 실행 - 자동으로 현재 재생중인 곡의 가사를 찾아 표시
./build/lyrics

# mpv로 음악 재생
mpv --force-window=yes song.mp3

# 한글 폰트로 실행
./build/lyrics -F "Noto Sans CJK KR 20"

# 화면 상단에 표시
./build/lyrics -a top -m 50

# 위의 사용 방법 섹션의 모든 옵션이 ./build/lyrics 에서도 동작합니다
```

## 법적 고지

> [!NOTE]
> wshowlyrics는 가사 표시 도구일 뿐입니다. 개발자는 가사 콘텐츠를 제공, 호스팅 또는 배포하지 않습니다.
>
> - **로컬 파일**: 사용자는 합법적인 출처로부터 가사 파일을 얻을 책임이 있습니다.
> - **온라인 가사**: 로컬 파일을 찾을 수 없는 경우, 이 도구는 [lrclib.net](https://lrclib.net) API(MIT 라이선스)에서 공개적으로 제공되는 가사를 가져옵니다. 개발자는 이 콘텐츠를 호스팅, 수정 또는 재배포하지 않으며, 가사는 lrclib.net에서 직접 가져와 사용자에게 실시간으로 표시됩니다.

> [!NOTE]
> **번역 서비스**: 이 도구는 제3자 AI 번역 API(OpenAI, DeepL, Gemini, Claude)와 통합됩니다. 사용자는 다음에 대한 책임이 있습니다:
> - 자신의 API 키 획득 및 보안
> - 제공업체의 가격 정책에 따른 API 사용 비용
> - 번역 품질 및 정확성 (AI 번역에는 오류가 있을 수 있음)
> - 번역을 위해 외부 API 서비스로 전송되는 가사 텍스트 (개인적 처리 목적이며 공개 재배포가 아님)

> [!CAUTION]
> **유해 콘텐츠 경고**: 가사 및 번역에는 욕설, 성적 표현, 폭력, 약물 사용 등의 노골적인 콘텐츠가 포함될 수 있습니다. 이 도구는 콘텐츠 필터링 기능을 제공하지 않습니다. 미성년자의 경우 보호자의 지도가 권장됩니다.

> [!WARNING]
> 가사 콘텐츠를 외부에 공개하거나 재배포하는 행위(예: 웹사이트 업로드, 공개 저장소 공유)는 저작권법 위반에 해당할 수 있습니다. 사용자는 해당 관할권의 저작권법을 준수할 전적인 책임이 있습니다.

## 라이선스

GNU General Public License v3.0 (GPL-3.0)

이 프로젝트는 wshowkeys를 기반으로 하며, 동일한 GPL-3.0 라이선스를 따릅니다.
