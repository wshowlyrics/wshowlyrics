# Lyrics Overlay for Wayland

Wayland 기반 가사 오버레이 프로그램입니다. [wshowkeys 프로젝트를 기반](https://github.com/unstable-code/wshowkeys)으로 제작되었으며, [LyricsX](https://github.com/ddddxxx/LyricsX)에서 영감을 받았습니다.

## 기능

- **MPRIS 통합**: playerctl을 통해 현재 재생중인 곡 자동 감지 (mpv, Spotify, VLC 등 모든 MPRIS 호환 플레이어)
- **스마트 가사 검색**:
  - **로컬 파일 검색**: 현재 재생중인 파일과 같은 디렉토리 우선 검색
  - **온라인 자동 검색**: 로컬에 가사 파일이 없으면 [lrclib.net](https://lrclib.net)에서 자동으로 가사 가져오기
  - URL 디코딩으로 유니코드(한글, 일본어 등) 경로 지원
  - 파일명 기반 자동 매칭
- **동기화된 가사**: LRC 및 SRT 포맷 지원
- **실시간 싱크**: 음악 재생 위치에 따라 가사 자동 표시
- Wayland 프로토콜 사용 (wlr-layer-shell)
- 투명 배경 지원
- 한글, 중국어, 일본어 등 모든 유니코드 문자 지원 (Pango)
- 화면 위치 조정 가능 (상/하/좌/우)
- 색상 및 폰트 커스터마이징

## 빌드 방법

```bash
meson setup build
meson compile -C build
```

## 사용 방법

### MPRIS 모드 (기본)

현재 재생중인 음악에 자동으로 가사를 표시합니다:

```bash
# 기본 실행 - 자동으로 현재 재생중인 곡의 가사를 찾아 표시
./build/lyrics

# mpv로 음악 재생
mpv --force-window=yes song.mp3

# Spotify, VLC 등 다른 MPRIS 호환 플레이어도 동작
```

### 수동 모드

특정 LRC/SRT 파일을 표시:

```bash
./build/lyrics -l sample.lrc
```

### 옵션

| 짧은 형식 | 긴 형식 | 설명 | 기본값 |
|-----------|---------|------|--------|
| `-h` | `--help` | 도움말 표시 | - |
| `-b COLOR` | `--background=COLOR` | 배경색 (#RRGGBB[AA] 형식) | `#00000080` (검정, 50% 투명) |
| `-f COLOR` | `--foreground=COLOR` | 전경색/텍스트색 (#RRGGBB[AA] 형식) | `#FFFFFFFF` (흰색, 불투명) |
| `-F FONT` | `--font=FONT` | 폰트 설정 | `"Sans 20"` |
| `-a POSITION` | `--anchor=POSITION` | 화면 위치 (top/bottom/left/right) | `bottom` |
| `-m PIXELS` | `--margin=PIXELS` | 화면 가장자리 여백 (픽셀) | `32` |
| `-l FILE` | `--lyrics-file=FILE` | 특정 가사 파일 로드 (.lrc/.srt/.vtt) | MPRIS 자동 감지 |

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
./build/lyrics -h
./build/lyrics --help

# MPRIS 모드로 실행 (기본)
./build/lyrics

# 한글 폰트로 실행
./build/lyrics -F "Noto Sans CJK KR 20"
./build/lyrics --font="Noto Sans CJK KR 20"

# 화면 상단에 표시
./build/lyrics -a top -m 50
./build/lyrics --anchor=top --margin=50

# 투명한 배경에 노란색 텍스트
./build/lyrics -b 00000066 -f FFFF00FF
./build/lyrics --background=00000066 --foreground=FFFF00FF

# 특정 LRC 파일 표시
./build/lyrics -l "Artist - Song.lrc"
./build/lyrics --lyrics-file="Artist - Song.lrc"

# 큰 볼드 폰트, 화면 하단
./build/lyrics -F "Sans Bold 28" -a bottom -m 40
./build/lyrics --font="Sans Bold 28" --anchor=bottom --margin=40
```

## 가사 파일 형식

### LRC 형식 (권장)

동기화된 가사 파일 형식입니다:

```lrc
[ti:노래 제목]
[ar:아티스트]
[al:앨범]

[00:12.00]첫 번째 가사 줄
[00:17.50]두 번째 가사 줄
[00:23.00]세 번째 가사 줄
```

### SRT 형식

자막 형식도 지원합니다:

```srt
1
00:00:12,000 --> 00:00:17,500
첫 번째 가사 줄

2
00:00:17,500 --> 00:00:23,000
두 번째 가사 줄
```

### 가사 검색 과정

프로그램은 2단계 방식으로 가사를 찾습니다:

#### 1. 로컬 파일 검색 (우선순위)

다음 **우선순위**로 가사 파일을 자동으로 검색합니다:

1. **현재 재생중인 음악 파일과 같은 디렉토리** (최우선!)
   - 먼저 음악 파일명과 동일한 이름의 가사 파일 검색
   - 예: `song.mp3` → `song.lrc` 또는 `song.srt`
2. 제목 기반 검색 (현재 디렉토리부터)
3. `$XDG_MUSIC_DIR`
4. `~/.lyrics/`
5. `$HOME`

파일명 형식:
- `파일명.lrc` / `파일명.srt` (추천! 음악 파일과 같은 이름)
- `제목.lrc` / `제목.srt`
- `아티스트 - 제목.lrc` / `아티스트 - 제목.srt`
- `아티스트/제목.lrc`

**팁**:
- 가사 파일을 음악 파일과 **같은 이름, 같은 디렉토리**에 두세요!
- 유니코드(한글, 일본어 등) 파일명도 완벽 지원합니다
- 예: `/music/노래.mp3` + `/music/노래.lrc` ✅

#### 2. 온라인 가사 자동 가져오기

로컬에 가사 파일이 없으면 자동으로 [lrclib.net](https://lrclib.net)에서 동기화된 가사를 가져옵니다:

- **필수 조건**: 트랙에 **제목**과 **아티스트** 메타데이터가 모두 있어야 함
- **형식**: 타임스탬프가 있는 동기화된 가사(LRC 형식)만 사용
- **인터넷 연결이 없다면?** 온라인 검색을 건너뛰고 계속 진행
- **프라이버시**: 곡 메타데이터(제목, 아티스트, 앨범)만 lrclib.net API로 전송

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
- meson & ninja

### 실행 시 필요
- curl (온라인 가사 가져오기용)
- playerctl (MPRIS 모드 사용시)
- Wayland compositor with wlr-layer-shell support (Sway, Hyprland 등)

## 설치

### Arch Linux (AUR)

AUR 헬퍼(예: `yay`)를 사용하여 설치:

```bash
yay -S wshowlyrics-git
```

수동 설치:

```bash
git clone https://aur.archlinux.org/wshowlyrics-git.git
cd wshowlyrics-git
makepkg -si
```

### 수동 설치 (Arch Linux)

의존성 설치:

```bash
sudo pacman -S cairo curl fontconfig pango wayland wayland-protocols meson ninja playerctl
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
                 meson ninja-build playerctl
```

빌드 및 설치:

```bash
meson setup build
meson compile -C build
sudo install -Dm755 build/lyrics /usr/bin/wshowlyrics
```

## 라이선스

GNU General Public License v3.0 (GPL-3.0)

이 프로젝트는 wshowkeys를 기반으로 하며, 동일한 GPL-3.0 라이선스를 따릅니다.
