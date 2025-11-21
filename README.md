# Lyrics Overlay for Wayland

Wayland 기반 가사 오버레이 프로그램입니다. wshowkeys 프로젝트를 기반으로 제작되었으며, LyricsX에서 영감을 받았습니다.

## 기능

- **MPRIS 통합**: playerctl을 통해 현재 재생중인 곡 자동 감지 (mpv, Spotify, VLC 등 모든 MPRIS 호환 플레이어)
- **스마트 로컬 가사 검색**:
  - 현재 재생중인 파일과 같은 디렉토리 우선 검색
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

- `-l <파일>`: 가사 파일 경로 (지정시 MPRIS 비활성화)
- `-F <폰트>`: 폰트 설정 (기본: "Sans Bold 32")
- `-b <색상>`: 배경색 (기본: #000000CC)
- `-f <색상>`: 전경색/텍스트색 (기본: #FFFFFFFF)
- `-a <위치>`: 화면 위치 - top, bottom, left, right (기본: bottom)
- `-m <픽셀>`: 화면 가장자리로부터의 여백 (기본: 32)

### 예제

```bash
# MPRIS 모드로 실행
./build/lyrics

# 한글 폰트로 실행
./build/lyrics -F "Noto Sans CJK KR Bold 48"

# 화면 상단에 표시
./build/lyrics -a top

# 흰색 배경에 검은색 텍스트
./build/lyrics -b FFFFFFFF -f 000000FF

# 특정 LRC 파일 표시
./build/lyrics -l "Artist - Song.lrc"
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

### 로컬 가사 파일 검색 경로

프로그램은 다음 **우선순위**로 가사 파일을 자동으로 검색합니다:

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

## 의존성

### 빌드 시 필요
- cairo
- fontconfig
- pango
- pangocairo
- wayland-client
- wayland-protocols
- wlr-layer-shell protocol
- libcurl
- meson & ninja

### 실행 시 필요
- playerctl (MPRIS 모드 사용시)
- Wayland compositor with wlr-layer-shell support (Sway, Hyprland 등)

### 설치 (Arch Linux)

```bash
sudo pacman -S cairo fontconfig pango wayland wayland-protocols curl meson ninja playerctl
```

### 설치 (Ubuntu/Debian)

```bash
sudo apt install libcairo2-dev libfontconfig1-dev libpango1.0-dev \
                 libwayland-dev wayland-protocols libcurl4-openssl-dev \
                 meson ninja-build playerctl
```

## 라이선스

GNU General Public License v3.0 (GPL-3.0)

이 프로젝트는 wshowkeys를 기반으로 하며, 동일한 GPL-3.0 라이선스를 따릅니다.
