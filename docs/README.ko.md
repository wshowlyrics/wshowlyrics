# Lyrics Overlay for Wayland
<img width="696" height="77" alt="a65e765" src="https://github.com/user-attachments/assets/e33bb35e-24f3-4632-811c-b7e55a0660a1" />

Wayland 기반 가사 오버레이 프로그램입니다. [wshowkeys 프로젝트를 기반](https://github.com/unstable-code/wshowkeys)으로 제작되었으며, [LyricsX](https://github.com/ddddxxx/LyricsX)에서 영감을 받았습니다.

## 기능

- **MPRIS 통합**: playerctl을 통해 현재 재생중인 곡 자동 감지 (mpv, Spotify, VLC 등 모든 MPRIS 호환 플레이어)
- **시스템 트레이 통합**: 시스템 트레이에 앨범 아트 표시 (Swaybar/Waybar)
  - MPRIS 메타데이터에서 앨범 아트 자동 로드
  - **iTunes API 자동 검색**: MPRIS에서 앨범 아트를 제공하지 않을 경우 iTunes Search API에서 자동으로 가져오기
  - 모든 소스에서 앨범 아트를 가져올 수 없을 경우 기본 음악 아이콘 표시
  - 툴팁으로 현재 트랙 정보 표시 (아티스트 - 제목)
- **스마트 가사 검색**:
  - **로컬 파일 검색**: 현재 재생중인 파일과 같은 디렉토리 우선 검색
  - **온라인 자동 검색**: 로컬에 가사 파일이 없으면 [lrclib.net](https://lrclib.net)에서 자동으로 가사 가져오기
  - URL 디코딩으로 유니코드(한글, 일본어 등) 경로 지원
  - 파일명 기반 자동 매칭
- **번역 지원 (0.6.0 신규 기능)**: DeepL API를 활용한 자동 가사 번역
  - 무료 및 유료 API 키 모두 지원, 엔드포인트 자동 감지
  - 스마트 캐싱 시스템 - 한 번 번역하면 영구 저장
  - 표시 모드 설정 가능 (원문+번역, 번역만 표시)
  - 번역 텍스트 투명도 조절 가능
  - LRC 및 SRT 포맷 지원 (VTT/LRCX 제외)
- **카라오케 모드**: LRCX 포맷으로 단어별 타이밍 및 점진적 채움 효과 지원
  - 지나간 단어: 일반 색상 (이미 불렀음)
  - 현재 단어: 왼쪽에서 오른쪽으로 점진적으로 채워짐 (지금 부르는 중)
  - 아직 안 지나간 단어: 흐림 (아직 안 불렀음)
- **동기화된 가사**: LRC, LRCX, SRT 포맷 지원
- **실시간 싱크**: 음악 재생 위치에 따라 가사 자동 표시
- Wayland 프로토콜 사용 (wlr-layer-shell)
- 투명 배경 지원
- 한글, 중국어, 일본어 등 모든 유니코드 문자 지원 (Pango)
- 화면 위치 조정 가능 (상/하/좌/우)
- 색상 및 폰트 커스터마이징

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
sudo pacman -S cairo curl fontconfig pango wayland wayland-protocols meson ninja playerctl \
               libappindicator-gtk3 gdk-pixbuf2
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

모든 가사 파일 형식(LRCX, LRC, SRT)에서 루비 텍스트를 지원합니다:

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

### SRT 형식
<img width="361" height="131" alt="4cfd477" src="https://github.com/user-attachments/assets/09e7b68d-16ab-441b-b1a9-87bedda7547a" />

자막 형식도 지원합니다:

```srt
1
00:00:12,000 --> 00:00:17,500
첫 번째 가사 줄

2
00:00:17,500 --> 00:00:23,000
두 번째 가사 줄
```

**인라인 번역 지원**

SRT 파일은 `{번역}` 구문을 사용한 인라인 번역을 지원합니다:

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

- 문법: 자막 블록 내에서 별도 라인에 `{번역 텍스트}` 작성
- 번역은 원문 아래에 작은 글씨로 흐리게 표시됩니다
- DeepL API 번역과 독립적으로 작동 (API 키 불필요)
- 루비 텍스트(`본문{루비}`)와 인라인 번역을 함께 사용 가능

### 가사 검색 과정

프로그램은 2단계 방식으로 가사를 찾습니다:

#### 1. 로컬 파일 검색 (우선순위)

다음 **우선순위**로 가사 파일을 자동으로 검색합니다:

1. **현재 재생중인 음악 파일과 같은 디렉토리** (최우선!)
   - 먼저 음악 파일명과 동일한 이름의 가사 파일 검색
   - 예: `song.mp3` → `song.lrcx`, `song.lrc`, 또는 `song.srt`
2. 제목 기반 검색 (현재 디렉토리부터)
3. `$XDG_MUSIC_DIR`
4. `~/.lyrics/`
5. `$HOME`

파일명 형식 (검색 순서):
- `파일명.lrcx` / `파일명.lrc` / `파일명.srt` (추천! 음악 파일과 같은 이름)
- `제목.lrcx` / `제목.lrc` / `제목.srt`
- `아티스트 - 제목.lrcx` / `아티스트 - 제목.lrc` / `아티스트 - 제목.srt`
- `아티스트/제목.lrcx` / `아티스트/제목.lrc`

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

### 앨범 아트

프로그램은 폴백 체인을 통해 시스템 트레이에 앨범 아트를 자동으로 표시합니다:

1. **MPRIS 메타데이터** (우선순위): 음악 플레이어가 제공하는 경우 MPRIS 메타데이터에서 앨범 아트 로드
2. **iTunes Search API 자동 검색**: MPRIS에서 앨범 아트를 제공하지 않으면 Apple iTunes Search API에서 자동으로 가져오기
3. **기본 아이콘**: 앨범 아트를 사용할 수 없는 경우 시스템 테마의 기본 음악 아이콘 표시

iTunes API 기능은 `settings.ini`에서 `enable_itunes = false`로 설정하여 비활성화할 수 있습니다.

## 번역 지원 (DeepL API)

wshowlyrics는 LRC 포맷 가사 파일에 대해 DeepL API를 사용한 자동 번역을 지원합니다.

### 설정

`~/.config/wshowlyrics/settings.ini`에 다음 설정을 추가하세요:

```ini
[deepl]
# DeepL 번역 기능 활성화
enable_deepl = true

# DeepL API 키 (https://www.deepl.com/pro-api 에서 발급)
# 무료 API 키는 :fx로 끝남 (예: "abc123-def456:fx")
# 유료 API 키는 이 접미사가 없음
# API 키 형식에 따라 올바른 엔드포인트가 자동으로 선택됨
api_key = your-api-key-here

# 번역 대상 언어
# 지원 언어: BG, CS, DA, DE, EL, EN, ES, ET, FI, FR, HU, ID, IT, JA, KO,
#            LT, LV, NL, PL, PT, RO, RU, SK, SL, SV, TR, UK, ZH
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
```

### 기능

- **자동 엔드포인트 감지**: API 키 형식에 따라 무료 또는 유료 API 엔드포인트 자동 선택
- **스마트 캐싱**: 번역은 `/tmp/wshowlyrics/translated/`에 캐시되어 재생시 재사용됨
- **포맷 지원**: DeepL API 번역은 LRC 포맷 파일에 적용됩니다. SRT 파일은 `{번역}` 구문을 사용한 인라인 번역을 지원합니다 (API 불필요). VTT 및 LRCX 포맷은 제외됩니다.
- **비용 효율적**: 곡당 한 번만 번역되고 재부팅 전까지 캐시됨
- **표시 모드 설정**: 원문+번역 또는 번역만 표시 중 선택 가능
- **투명도 조절**: 번역 텍스트의 가시성 조절 가능

### DeepL API 키 발급

1. https://www.deepl.com/pro-api 방문
2. 무료 또는 유료 계정 가입
3. 무료 플랜은 월 50만 자 제공
4. API 키를 복사하여 설정 파일에 입력

### 예시

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

## 라이선스

GNU General Public License v3.0 (GPL-3.0)

이 프로젝트는 wshowkeys를 기반으로 하며, 동일한 GPL-3.0 라이선스를 따릅니다.
