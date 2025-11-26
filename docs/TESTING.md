# Testing Guide

## Quick Test

### 1. 가사 파일과 음악 파일을 같은 디렉토리에 배치

```bash
# Example directory structure
/home/user/music/
├── song.mp3
└── song.lrc
```

### 2. 음악 파일 재생

```bash
# mpv로 재생
mpv --force-window=yes /home/user/music/song.mp3

# 또는 다른 MPRIS 호환 플레이어 사용
# Spotify, VLC, etc.
```

### 3. lyrics 프로그램 실행

```bash
cd /home/hm/Documents/Release/lyrics
./build/lyrics
```

### 4. 디버그 출력 확인

프로그램이 다음과 같은 메시지를 출력해야 합니다:

```
Compositor: wayland-1
Using compositor interfaces...
MPRIS mode enabled - will track currently playing music
Screen resolution: 1920x1080
Set primary output

=== Track changed ===
Title: song
Artist: Artist Name
Album: Album Name
File location: file:///home/user/music/song.mp3

Searching lyrics for: Artist Name - song
Extracted directory from URL: /home/user/music
Trying provider: local
Trying: /home/user/music/song.lrc
Loaded LRC file: /home/user/music/song.lrc
Found lyrics via local provider
Loaded 50 lines of lyrics

Line 1/50: First lyrics line...
```

## Unicode Test (한글/일본어)

### 테스트 파일 구조

```bash
/home/user/음악/
├── 노래.mp3
└── 노래.lrc
```

### 예상 출력

```
File location: file:///home/user/%EC%9D%8C%EC%95%85/%EB%85%B8%EB%9E%98.mp3
Extracted directory from URL: /home/user/음악
Trying: /home/user/음악/노래.lrc
Loaded LRC file: /home/user/음악/노래.lrc
```

## Troubleshooting

### 가사가 표시되지 않을 때

1. **MPRIS 확인**:
   ```bash
   playerctl metadata
   ```
   제목, 아티스트, URL이 올바른지 확인

2. **파일 위치 확인**:
   ```bash
   playerctl metadata xesam:url
   ```
   출력된 URL에서 파일 경로 확인

3. **가사 파일 존재 확인**:
   ```bash
   # URL이 file:///home/user/music/song.mp3 라면
   ls -la /home/user/music/song.lrc
   ls -la /home/user/music/song.srt
   ```

4. **디버그 모드로 실행**:
   프로그램이 어떤 경로를 시도하는지 확인
   모든 "Trying: ..." 메시지를 확인

### position 확인

```bash
# 재생 위치 확인 (마이크로초)
playerctl metadata -f '{{position}}'

# 재생 상태 확인
playerctl status
```

## Sample LRC File

테스트용 `sample.lrc` 생성:

```bash
cat > test.lrc << 'EOF'
[ti:Test Song]
[ar:Test Artist]
[al:Test Album]

[00:00.00]First line
[00:05.00]Second line
[00:10.00]Third line
[00:15.00]한글 가사
[00:20.00]日本語の歌詞
EOF
```

## Expected Behavior

✅ **정상 동작**:
- 음악 파일과 같은 디렉토리의 .lrc/.srt 파일을 즉시 찾음
- 유니코드 경로 완벽 지원
- 재생 위치에 따라 가사 자동 업데이트
- 빈 창 없이 가사 즉시 표시

❌ **비정상 동작 (보고 필요)**:
- 가사 파일이 있는데 "No lyrics found"
- URL 디코딩 실패
- 창은 뜨지만 가사 표시 안됨
- 타이밍이 맞지 않음

## Debug Commands

```bash
# 1. MPRIS 메타데이터 확인
playerctl metadata --format '
Title: {{title}}
Artist: {{artist}}
Album: {{album}}
URL: {{xesam:url}}
Position: {{position}}
Length: {{mpris:length}}
'

# 2. 디렉토리 구조 확인
playerctl metadata xesam:url | sed 's|file://||; s|/[^/]*$||' | xargs ls -la

# 3. URL 디코딩 테스트
playerctl metadata xesam:url | python3 -c "import sys; from urllib.parse import unquote; print(unquote(sys.stdin.read()))"
```

## Performance Test

시간 측정:

```bash
time ./build/lyrics &
# MPRIS 감지 및 가사 로드 시간 확인
# 일반적으로 0.1초 이내여야 함
```
