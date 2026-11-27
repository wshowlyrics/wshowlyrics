# TODO: Code Quality & Security Improvements

## 현재 상태 (2026-11-27, **v0.12.0 릴리스** — 태그 592d168, 파이프라인 920 성공, SonarCloud 0건·게이트 OK, Coverity @`fa52690` 0건·@`164c044` 제출됨)

감사 범위: `1d3800a..28e6f06` (69커밋, 그중 `src/`·`meson.build` 변경 29커밋 / 82파일). 이전 감사(2026-07-06)는 1d3800a까지. 11-23에 후속 6커밋(`28e6f06..3a6b2f4`)을 추가 검토.

### 📊 SonarCloud 현황 (2026-11-23 API 재조회, 마지막 분석 2026-11-23 @ 2be2227 — 11-22 조회와 동일한 18건. af72173 이후 커밋은 미푸시라 아직 분석 전. **18건 전부 코드상 해소됨 (e4a990a, 1c8afd8, e9eef27) — 푸시 후 재분석에서 0건이 되어야 한다.** 새로 넣은 코드에서 새 이슈가 나오는지도 그때 확인)

**2026-11-26 재조회 2 (분석 @ 79154a9, 파이프라인 908 성공):** 미해결 **0건**, 품질 게이트 OK. #67 닫힘.

**2026-11-26 재조회 (분석 @ bc7ed64, 파이프라인 906 성공):** 미해결 **0건**, 품질 게이트 OK. eb34b02에서 나온 S859 5건은 e9c00e4로 해소, 11-25의 S995 2건도 목록에 없음. 새 코드(`lyrics_file_watch.c`, `thread_cancel.c`)에서 나온 이슈 없음.

**2026-11-25 재조회 (분석 @ 5066304, 파이프라인 897 성공):** 11-23의 18건은 전부 해소. 미해결 2건, 둘 다 새 코드의 `c:S995`(MINOR) — `string_utils.c` `strip_media_extension(char *name)`·`strip_file_extension(char *filename)`. 두 함수는 `strrchr`로 얻은 포인터로 문자열을 잘라 쓰므로(`*dot = 0`) const로 바꿀 수 없음 → 오탐, falsepositive 표시 대상. **표시 실패: `~/.config/sonarcloud/token`이 없음(디렉터리째 없음) — 토큰을 다시 두거나 웹 UI에서 표시.** 이슈 키 `AaISbL43b25juRoYUx3L`(26행), `AaISbL43b25juRoYUx3M`(39행). CLAUDE.md 표시 정책 표에 이 경우를 추가(로컬 커밋, 미푸시). 새 결함 1건: CID 1709922 (TOCTOU, Low) — `run_ffmpeg_extract`의 `stat()` 후 읽기·unlink. 뒤의 `read_art_file`이 연 디스크립터에 `fstat`으로 같은 검사를 하므로 `stat()`을 제거(로컬 커밋, 미푸시, 실행 미확인). Coverity: 2026-11-25 수동 실행(run 45610385367, 5066304) 제출 성공 — 분석 결과는 대시보드에서 확인 필요. 품질 게이트 OK, 검토 대기 핫스팟 0.

> **2026-11-24 갱신: fdb2e1d 재분석 기준 미해결 0건** (Quality Gate OK, hotspot 0). 아래 표는 11-22~23 감사 시점의 18건 기록이다. 18건은 e4a990a·1c8afd8·e9eef27로, 수정 중 새로 생긴 `c:S924` 1건은 fdb2e1d로 해소.

| 심각도 (구) | 마킹 처리 후 (05-02) | 07-06 | **현재 (11-22)** | 내용 |
|--------|:---:|:---:|:---:|---|
| BLOCKER | 0 | 0 | **1** | `c:S912` main.c:346 (`&&` 우변 `(*update_counter)++`) — 335f000에서 유입 |
| CRITICAL | 0 | 0 | **2** | `c:S134` parser_utils.c:466, wayland_manager.c:181 (중첩 4단) |
| MAJOR | 1 (S1820) | 4 | **6** | `c:S1121` ×5 lyrics_manager.c:213/216, `c:S107` system_tray.c:1278 (9 파라미터) |
| MINOR | 0 | 1 | **9** | `c:S995` ×4, `c:S5350` ×4, `c:S886` ×1 |
| **합계** | **1** | **5** | **18** | 전부 CODE_SMELL |

MQR 심각도(`impactSeverities`) 기준: BLOCKER 1 · HIGH 2 · MEDIUM 6 · LOW 9 · INFO 0.

| Hotspots TO_REVIEW | 0 |
|---|:---:|

**Quality Gate**: GREEN (`OK`) — bugs 0, vulnerabilities 0, code smells 18
**Ratings**: Reliability **A(1.0)**, Security **A(1.0)**, Maintainability **A(1.0)**
**참고**: 07-06의 5건(S1121 ×4, S995 ×1)은 R13(6fcfc72)으로 해소됐고, 07-07 리뷰 후속으로 생긴 BLOCKER/HIGH 3건(S912, S5443, S5847)도 b7f50a8로 해소됨. 위 18건은 전부 그 이후(worker thread 도입, 루비 파서, flush 수정) 유입분 → **R21**. 18건 중 Standing marking policy로 마킹할 대상은 없음 (S107은 콜백이 아닌 자체 함수 `system_tray_update_icon_fast`).

### 📊 Coverity 현황

- 결함 수는 인증 없는 API가 없어 조회하지 못함 (대시보드 로그인 필요). 문서에 기록된 CID 643610(b3a410a), CID 1697552(1fc4295)는 수정 완료.
- 주간 스캔 워크플로는 2026-11-22 실행분(run 44991552505)이 `libgdk-pixbuf2.0-dev` 부재(Ubuntu 26.04)로 실패했고, **CI-1로 수정됨 (424c2cf)**. 2be2227 기준 수동 실행(run 45116677248, 11-23)이 빌드·제출까지 전 단계 성공. 액션 메이저 업데이트(2be2227: checkout v7, cache v6, upload-artifact v7)도 같은 실행으로 검증됨.

### 처리 이력
- 2026-05-02: GitLab 메인 전환으로 SonarCloud 프로젝트 새 ID(`AZzxDhZL...`)로 재분석.
  Won't Fix / SAFE 마킹 전부 리셋. 코드 변경 없이 카운트만 33+48 증가.
- 2026-05-02: 일괄 마킹 스크립트로 32 issues + 47 hotspots 처리. 정책은 CLAUDE.md "Marking Issues / Hotspots" 표 참조.
- 2026-05-02: S5332 HTTP 1건은 SAFE + acknowledgement 코멘트로 처리 (외부 URL 통제 불가, API가 ACKNOWLEDGED 미지원).
- 2026-07-06: 43a6366 이후 변경분(15커밋) 중심 리팩토링 + 보안 재감사. 보안 MEDIUM 2건(SEC-1, SEC-2) 및 신규 리팩토링 5건(R13–R17) 발견. `wshowlyrics-offset` / dbus_control / translator 캐시 / lock_file / runtime_dir / 파서는 클린 판정.
- 2026-07-06: 감사 발견 항목 전량 처리 (6커밋). SEC-1 (38d1a85) → R13+R17 (6fcfc72) → SEC-2 (89530c9) → R14~R16 (bb8b0cc) → SEC-3/4 (f992c86) → R7 (1d3800a). 매 단계 `werror=true` 빌드 클린, SEC-1/SEC-2/R7은 실행 검증. **SEC-2 IP 차단은 로컬 동일유저 위협모델 분석 결과 실효 없음 + 자체호스팅(LAN/Tailscale/localhost) 파괴로 의도적 제외**, 대신 매직바이트 포맷 allowlist로 디코더 표면 축소. R7은 GitLab 이슈 #2 vtable 설계.
- 2026-07-07: fuzz 커버리지 확장 — `url_decode_string`(MPRIS URI 퍼센트 디코더, SEC-4에서 수정)을 `src/utils/url/url_utils.{c,h}`로 추출하고 `fuzz_url_decode` libFuzzer 타겟 + 시드 코퍼스 추가. ASan로 ~1,900만 exec 크래시·누수 0 확인. 아울러 세션 8커밋의 `Co-Authored-By`를 하드코딩된 "Claude Fable 5" → 실제 모델명(Claude Opus 4.8 (1M context))으로 재작성(filter-branch, 해시 변경 — 위 해시들은 재작성 후 기준).
- 2026-07-07: 위 07-06 세션 커밋 전부 origin/master에 push 완료 (`git branch -r --contains 1d3800a` → origin/master, downstream/master). 문서의 모든 커밋 해시가 HEAD에서 resolve됨을 11-22에 재검증.
- 2026-07-07: 리뷰 후속 수정이 새로 만든 SonarCloud BLOCKER/HIGH 3건 해소 (b7f50a8) — `c:S912`(`&&` 우변 부작용), `c:S5443`(symlink 폴백이 world-writable `/dev/shm` 허용 → `/run`·`$XDG_RUNTIME_DIR`만), `c:S5847`(stat→fopen TOCTOU → open 후 fstat).
- 2026-07-07: NUR patchPhase용 `"/etc/"`·`"/usr/share/"` 리터럴 복원 (afa4a6a). SEC-3에서 슬래시를 뗀 것이 `nur-packages` `generic.nix`의 `substituteInPlace --replace-fail`을 깨뜨렸음. **config.c의 이 리터럴 모양은 건드리지 말 것.**
- 2026-08-03: Coverity CID 1697552 수정 (1fc4295) — `system_tray_apply_cached_icon`의 `gdk_pixbuf_save` 반환값 미확인.
- 2026-11-22: `1d3800a..HEAD` 재감사 (worker thread, ffmpeg 아트 추출, translator, curl 하드닝, 가사 경로). MEDIUM 5건(R18–R21, CI-1), LOW 7건(R22–R26, SEC-5, SEC-6) 발견. **코드 수정은 아직 없음 — 항목별 승인 대기.**
- 2026-11-23: CI-1 완료 (424c2cf, 2be2227) — Coverity 스캔 복구, run 45116677248 성공으로 확인.
- 2026-11-23: 후속 4커밋 검토 (`28e6f06..2be2227`). 71dfdf5(translation_only 렌더 수정)·424c2cf·2be2227·21fb295(COPR을 Alpine+spec 업로드로 전환, 파이프라인 #867에서 `publish:copr-nightly` 성공) 모두 클린. SonarCloud는 2be2227 재분석 후에도 18건 그대로(신규 유입 없음). CI-2(COPR 잡 버전 미고정)는 제안했으나 기각됨.
- 2026-11-23: SEC-5 수정 (af72173, 이슈 #49) — `notify-send` argv에 `"--"` 추가. 같은 인자로 직접 실행해 재현했고, `ALBUM=-ERROR` 태그 파일을 mpv로 재생해 알림이 뜨는 것까지 확인. 로컬 master 커밋, 미푸시.
- 2026-11-23: 3a6b2f4(`--allow-browsers`, 이슈 #50) 검토 — 클린. 플래그는 `mpris_init()` 전에 설정되고(main.c:637 → 646), `playerctld`는 계속 제외됨. 브라우저의 `mpris:artUrl`은 실측상 로컬 임시 파일(`file:///run/user/1000/tmp/..com.vivaldi.Vivaldi.*`)이라 웹 페이지가 임의 URL을 받게 하는 경로가 아니고, 기존 `realpath`+정규 파일+매직바이트 검사를 그대로 거침 → **SEC-2의 IP 차단 제외 결정을 다시 열 근거 없음**. 브라우저의 `xesam:url`은 `file://`이 아니라 로컬 가사 디렉터리·ffmpeg 단계는 건너뜀. 이 플래그로 웹 페이지가 제목·아티스트·앨범 문자열을 정할 수 있게 되므로 SEC-5를 먼저 고친 순서가 맞았고, 같은 이유로 **SEC-7**을 추가. 로컬 master 커밋, 미푸시.
- 2026-11-24: R21 중 BLOCKER/HIGH 3건 수정 — `c:S912` main.c (e4a990a: 카운터 증가를 `&&` 밖으로), `c:S134` ×2 (1c8afd8: `skip_digits_backward()` 추출, flush 루프의 `pr <= 0` 블록을 두 갈래로 폄). `werror` 빌드 클린, `build-fuzz`를 지우고 다시 빌드한 뒤 fuzz_lrc/lrcx/srt 각 40초 크래시·누수 없음, `./build/lyrics` 실행으로 가사 표시·SIGINT 종료 확인. Wayland 재연결 경로는 실행되지 않음(로그에 `Reconnection needed` 없음). 로컬 master 커밋, 미푸시.
- 2026-11-24: R18+R19 수정 (8fa788e, 이슈 #53) — 번역 스레드의 대기를 `translator_sleep_cancellable()`(50ms 단위)로 바꾸고, 공용 curl 핸들에 타임아웃(60s/연결 5s)과 취소 콜백 추가. 감사 때 적지 않았던 두 가지도 포함: DeepL의 자체 핸들·재시도 대기에도 같은 처리, 취소로 끊긴 요청은 줄의 실패 시도로 세지 않음(세면 트랙을 넘길 때마다 재시도 예산이 줄어듦). "재시도 대기 상한"은 불필요해져 뺌. `pthread_cancel` 폴백은 그대로 둠 — 제거 여부는 미결정. 로컬 master 커밋, 미푸시.
- 2026-11-24: R22 수정 (0383670, 이슈 #54) — 플레이어가 사라질 때 가사 검색·아트 조회를 취소. 실행으로 확인. 로컬 master 커밋, 미푸시. 테스트 로그에서 `.mka` 확장자 미처리(R27)를 발견해 이슈 #55로 등재.
- 2026-11-24: `cancel_and_wait_translation()`의 5초 대기 + `pthread_cancel` 폴백 제거 (949a700, 이슈 #56, #53과 연결). `pthread_join`만 남기고 1초를 넘으면 경고. libcurl/OpenSSL 안의 스레드를 죽이면 내부 잠금이 남을 수 있고 정리·캐시 저장을 건너뛰기 때문. 요청 중 취소와 대기 중 취소(`rate_limit` 60초, `[1/56] Translated` 뒤 종료 → `Translation cancelled (1/56 completed)`, 지연 경고 없음) 모두 실행으로 확인. 로컬 master 커밋, 미푸시.
- 2026-11-24: R21 나머지 15건 (e9eef27), SEC-7·R24·SEC-6 (b718934), R23 (0396394), R26(a) (a19df73) 커밋. 이슈 없이 진행. 각 단계 `werror` 빌드 클린, 실행으로 트레이·알림 아이콘 저장, 썸네일 추출, 번역 시작·취소 확인. 미확인: `&`가 든 알림 본문의 표시, `file://` artUrl. 로컬 master 커밋, 미푸시.
- 2026-11-24: 로컬 영상 썸네일을 3초 지점 + `thumbnail` 필터에서 **길이의 20% 지점(반올림)의 한 프레임**으로 변경 (4f2c558, 이슈 #57). 감사 항목이 아닌 개선. `thumbnail` 필터는 컷이 빠른 영상에서 임의의 컷을 고르는 것을 실측으로 확인해 뺌. Dotfiles의 Quickshell 미디어 카드와 같은 프레임이 나옴. `CLAUDE.md` System Tray 항목도 갱신. 움직이는 아트 아이디어는 peter/desktop/dotfiles#36 으로 등재(wshowlyrics에서는 하지 않음).
- 2026-11-24: 푸시 전 `/simplify`(재사용·단순화·효율·수정 깊이 4관점) → 정리 커밋 287b423: `lyrics_manager_cancel_track_work()`로 취소 3단계를 한 곳에 모음, 번역기의 빈 init/cleanup 6개 제거(인터페이스에서 NULL 허용), 취소 로그 한 곳으로, 남은 include 4줄 제거. 범위가 큰 후속 3건은 이슈로 등재: #58(알림 헬퍼, `hold::decision`), #59(DeepL을 공용 요청 경로로), #60(curl 취소 콜백 네 벌 통합).
- 2026-11-24: `2be2227..287b423` 13커밋 origin/master에 push. #49·#50·#53·#54·#56·#57 자동 종료. 파이프라인 #872 전 잡 성공(build:makepkg, build:fuzz, semgrep, gitleaks, aur-git, copr-nightly, nur-unstable).
- 2026-11-24: SonarCloud 재분석(287b423): **18건 → 1건**, Quality Gate OK, hotspot 0. 남은 1건은 이번 수정이 새로 만든 `c:S924` translator_common.c:822 (번역 worker 루프에 `break` 2개 — 8fa788e에서 대기 취소용 `break`를 더한 결과). `cancelled` 플래그로 루프 조건을 바꿔 수정 (fdb2e1d).
- 2026-11-24: fdb2e1d push 후 재확인 — **SonarCloud 미해결 0건**, Quality Gate OK, hotspot 0. 파이프라인 #873 전 잡 성공. Coverity Scan 수동 실행(run 45223727943, fdb2e1d 기준) 빌드·제출 성공 — 분석 결과(결함 수)는 대시보드에서만 볼 수 있어 미확인.
- 2026-11-25: **R20 (a)(b)** 수정 (80669d4, 이슈 #61) — 트레이 아트 조회에서 막힐 수 있는 단계(artUrl 다운로드, ffmpeg 두 단계, iTunes)를 전부 art worker로 옮김. 메인 스레드는 캐시 조회와 "직전과 같은 artUrl이면 이전 캐시 복사"만 함. ffmpeg는 비동기 실행 + 50ms 폴링, 취소 또는 10초 초과 시 종료. 두 ffmpeg 단계를 `try_local_media_art`로 합쳐 시간 초과 때 둘째 단계를 건너뜀. 실행 확인: 캐시 적중, 영상 썸네일, 오디오 전용 파일의 iTunes까지 가는 체인, 아트 조회 중 번역이 먼저 시작됨(메인 스레드 비차단). **미확인: 내장 커버, http artUrl과 같은 URL 재사용, ffmpeg 실행 중 취소, 10초 시간 초과.** 로컬 master 커밋, 미푸시.
- 2026-11-25: 위 작업의 정리 단계에서 나온 후속 2건을 이슈로 등재 — #62(멈춘 마운트에서 worker의 `realpath`/`stat`이 멈추면 트랙 변경 시 메인 스레드의 `pthread_join`도 멈춤), #63(아트 조회를 트레이 쪽 작업 구조체로 묶기, #62와 함께). #60에는 취소 플래그를 읽는 곳이 ffmpeg 대기로 늘었다는 메모를 남김.
- 2026-11-25: **R27** 수정 (1d831b0, 이슈 #55) — URL 파일명은 목록 없이 마지막 확장자(영숫자 1~5자)를 뗌. 확장자 목록이 감사 때 적은 두 벌이 아니라 **세 벌**(`remove_extension`, `sanitize_title`, `lyrics_manager_clean_title`)이었고 서로 달랐음 → `strip_media_extension` 하나로 합치고 `.mka` 등 14개 추가. `r27-test.mka`, `r27 Ver.2 test.mka`로 실행 확인.
- 2026-11-25: 같은 원인의 후속 (5b852ec, 이슈 없음) — 제목의 확장자를 **MPRIS 메타데이터를 읽는 한 곳**(`strip_title_extension`, mpris.c)에서만 뗌. 제목이 `file://` URL의 파일명과 같으면 어떤 확장자든, 아니면 목록의 확장자만. 하류 세 곳의 확장자 처리는 삭제. 달라지는 점: 가사 파일 `[ti:]` 제목은 정리되지 않음, 태그 없는 파일의 아트 캐시 키가 바뀜. `CLAUDE.md` MPRIS 항목 갱신. 실행 확인(태그 없는 `.mka`, 태그 있는 `.mkv`). 미확인: 알림 제목 끝의 YouTube ID 제거, 목록 판정 쪽(제목≠파일명).
- 2026-11-25: **R20 (c)** 수정 (e2bccaf, 이슈 #64) — 간주 중 재검색을 `lyrics_manager_begin_reload()`로 worker에서 실행. `same_track` 표시로 아트 조회·트랙 알림을 건너뛰고 싱크 오프셋·오버레이 상태를 유지. 틀린 로그("keeping existing lyrics displayed") 삭제. 실행 확인: `.lrc`를 `~/.lyrics`로 옮긴 경우(다시 찾음), 지운 경우(lrclib까지 가서 못 찾음), 둘 다 알림·아트 조회 없음. 미확인: 재검색 중 트랙 넘기기.
- 2026-11-25: R20 (c)의 정리 단계에서 **같은 종류의 멈춤이 재로드 경로 두 곳에 더 있는 것**을 발견 — 가사 파일 저장(`file_monitor_reload_lyrics`)과 번역 설정 변경(`check_translation_config_changed`)이 부르는 `lyrics_manager_load_lyrics()`가 메인 스레드에서 전체 프로바이더 체인을 돎(가사 없는 트랙에서 설정 변경, 저장 도중의 빈 파일이면 lrclib까지). 또 재로드를 새 트랙처럼 처리해 저장할 때마다 오프셋 리셋·오버레이 켜짐·알림 재전송. 감사 때 R20에 적지 못한 경로. **#64의 범위에 넣어 별도 커밋으로 수정 (69a7eb1)** — 두 호출을 `begin_reload`로 바꾸고 `load_lyrics` 삭제. 설정 파일 검사를 가사 파일 검사보다 먼저 하도록 순서를 바꿈(가사 재로드가 띄운 worker가 `g_config`를 읽는 동안 `config_free`가 일어나지 않게). `cancel_and_wait_translation`은 파일 내부용으로. 실행 확인: 재생 중 `.lrc`를 고쳐 저장 → 바뀐 줄 표시, 번역 재시작, 알림·아트 조회 없음, +1750ms 오프셋 유지. **미확인: 번역 설정 변경 경로**(설정 파일이 home-manager 관리라 재빌드 없이 못 바꿈). 정리 단계에서 나온 기존 문제 2건은 고치지 않고 여기에만 기록: (1) `file_monitor_reload_config`가 설정을 비우고 다시 읽는 사이에 번역 스레드가 `max_retries`·`rate_limit_ms`·`cache_policy`를 0으로 읽을 수 있음(메모리 오류 아님, 그 순간 한 줄을 건너뛰거나 간격 없이 요청) — 고치려면 그 세 값을 번역 스레드 인자로 스냅숏. (2) 설정 로드 실패 시 `keeping old settings` 로그가 틀림(이미 기본값으로 바뀐 뒤) — 임시 구조체에 읽고 성공하면 교체.
- 2026-11-25: 감사 항목이 아닌 열린 이슈 **#41**(이미 재생 중인 채로 나타난 플레이어로 전환하지 않음) 수정 — `handle_player_appeared`에서 우선순위가 전환하지 않기로 했어도 새 플레이어가 `Playing`이면 `handle_player_started_playing`을 부름(현재 플레이어가 Paused/Stopped일 때만 넘어감). 로컬 커밋 e8acea4, 미푸시. /simplify에서 현재 플레이어 상태 확인을 `current_player_is_idle()`로 뽑아 D-Bus 조회보다 먼저 하도록 함. 실행 확인(가짜 MPRIS 플레이어 `smtc_mpris_bridge.FakeTest`): mpv 일시정지 → 전환됨, mpv 재생 중 → 전환 없음. **미확인: 실제 브리지 재시작** — 브리지가 이름을 먼저 얻고 객체를 나중에 내보내면 등장 순간의 `PlaybackStatus` 조회가 실패해 전환되지 않는다. 테스트 로그에서 본 기존 문제 1건은 고치지 않고 여기에만 기록: 플레이어 배지 아이콘을 테마에서 못 찾으면 트랙마다 `ERR: Failed to load any fallback icon`이 찍힘(알림은 정상 발송) — 로그 등급이 과함, 한 번만 찍거나 WRN으로.
- 2026-11-25: 열린 이슈 **#59**(DeepL을 공용 요청 경로로) 작업 — `deepl_translator.c`를 OpenAI 번역기와 같은 모양으로 다시 씀(`translator_perform_http_request` + `translator_translate_lyrics_generic`, 334→125줄). **작업 중 발견한 실제 버그**: `deepl_translate_lyrics`가 `translation_thread_active`를 켜지 않아 `cancel_and_wait_translation`이 DeepL 스레드를 취소·join하지 않았음 → 번역 중 트랙 변경 시 해제된 가사를 씀(코드로 확인, 재현은 안 함). 공용 진입 함수로 옮기며 해소, #59 본문에 범위 추가. 공용 경로에 `User-Agent` 추가(번역기 넷 모두), HTTP 456(DeepL 할당량 소진)은 재시도 안 함. `HTTP_REQUEST_BODY_SIZE` 삭제. **로컬 커밋 2f6232d, 미푸시. /simplify 완료**(DeepL이 마지막 사용자였던 `translator_provider`의 `init`/`cleanup` 훅과 `main.c`의 두 루프 삭제; `translator_thread_args`·`translator_async_worker`·`*_ex` 3개·`translator_curl_cancel_cb`·`json_extract_string_from`을 파일 내부용으로). 실행 확인(임시 `XDG_CONFIG_HOME`, 실제 DeepL 키): 56줄 중 28줄 번역 후 플레이어 종료 → `Translation cancelled (28/56 completed)`, 멈춤·크래시 없음. 틀린 키 → `Authentication error (HTTP 403)`, 요청 안에서는 재시도 없음(확인). 미확인: 456. **이 테스트에서 드러난 기존 문제(전 번역기 공통, 미수정)**: 401/403/456처럼 다시 보내도 소용없는 응답을 받아도 worker가 멈추지 않고 남은 줄마다 요청을 한 번씩 더 보냄(56줄이면 403이 56번) — 치명 오류 플래그를 두고 worker가 루프를 끝내게. 같이 볼 것: 공용 경로가 200·401·403·456 외의 모든 코드를 재시도함(400·404·413 포함) → 429·5xx만 재시도하는 일반 규칙으로 바꾸면 456 특례도 필요 없음. 정리 후보(미적용): 번역기 넷의 `translate_single_line`·헤더 빌더가 같은 모양. 참고: 설정 파일을 못 바꿔 건너뛴 테스트는 `XDG_CONFIG_HOME=<임시 디렉터리> ./build/lyrics`로 할 수 있음(69a7eb1의 번역 설정 변경 재로드 포함).
- 2026-11-25: #59의 후속(같은 이슈, 별도 커밋, **로컬 커밋·미푸시**. 실행 확인: 틀린 DeepL 키 → 403 한 번 + 중단; 맞는 키 → 정상 번역; Gemini에 없는 모델 이름 → 404 한 번 + 본문 로그 + 중단. 미확인: 429·5xx 재시도, 400. **"설정을 고친 뒤에도 번역이 안 되던 것을 막음"은 코드 근거뿐이고 실행으로 재현·확인하지 않음** — 맞는 키 테스트가 보여 준 것은 "캐시 없이 1번 줄부터 정상 번역"까지. **코드를 따라가며 알게 된 기존 동작(미수정)**: 진행 카운터(`current`)가 성공이 아니라 시도한 줄을 세므로, 한 곡의 모든 줄이 실패해도 N/N "완료"로 판정되어 번역은 비고 `retry_counts`만 든 캐시가 저장됨. 키·모델 거부는 이번에 막혔지만 그 밖의 실패(전 줄 400, 전 줄 같은 언어 판정 등)에는 남아 있음 — 완료 판정을 번역된 줄 수로 하거나, 성공 0건이면 캐시하지 않게) — 바로 위 항목의 "드러난 기존 문제" 두 건을 수정. (1) `handle_translator_http_response`: 재시도는 429와 5xx만. 그 밖의 4xx는 응답 본문 앞 200자를 로그에 남기고 그 줄만 실패 처리. (2) 401·403·404·456이면 `g_translator_config_rejected`를 세우고 worker가 루프를 끝냄(그 줄의 재시도 횟수는 세지 않음), `Translation stopped, the API rejected the key or model` 로그(404는 틀린 모델 이름 — 줄마다 실패 횟수가 쌓여 설정을 고친 뒤에도 번역이 안 되던 것을 막음). /simplify 완료. 번역기 넷 공통. 확인 방법: 틀린 DeepL 키로 403이 한 번만 찍히고 끝나는지.
- 2026-11-25: 열린 이슈 **#60**(취소 플래그 네 벌 합치기) 작업, **로컬 커밋·미푸시, /simplify 완료**(스레드 함수 둘은 `src/utils/thread/thread_cancel.{c,h}`로 분리, `curl_install_thread_cancel`만 `curl_utils`에; `translator_sleep_cancellable`은 파일 내부용으로; CLAUDE.md에 패턴 추가). 실행 확인(정리 전 코드): 번역 중 트랙 넘김 2회 → `Request aborted (translation cancelled)`; lrclib 조회·http 아트 다운로드는 정상 완료(콜백 설치가 요청을 깨지 않음). 미확인: lrclib·아트·ffmpeg 도중의 취소 — 이슈 본문의 "플래그를 인자로 내리기" 대신 **스레드별 플래그**로 구현(사용자 결정: 취소 단위가 요청이 아니라 worker 스레드이고, 인자 방식은 함수 20개 안팎의 시그니처가 바뀜). `curl_utils`에 `thread_cancel_bind`·`thread_cancel_requested`·`curl_install_thread_cancel`(`_Thread_local` 포인터 하나). lrclib·iTunes·트레이·번역의 전역 포인터 4·setter 4·콜백 4와 `lyrics_manager.c`의 짝 맞추기 호출 14곳 삭제, worker 진입점 3곳(가사 조회, 아트, 번역)이 한 번씩 bind. 동기 대체 경로(`start_fetch`의 pthread_create 실패 시)는 메인 스레드에서 bind 없이 돌므로 남는 플래그 없음. 확인할 것: lrclib 검색 중·아트 다운로드/ffmpeg 중·번역 요청 중 트랙 넘기기.
- 2026-11-26: 열린 이슈 **#63**(아트 조회를 트레이 소유의 작업 구조체로) 작업, **로컬 커밋·미푸시, /simplify 완료**(알림용 사본 `notify_pending`/`notif_title`/`notif_player`도 제거 — 트랙이 바뀌면 조회가 먼저 취소되므로 알림 시점의 `current_track`은 그 작업의 트랙; 정리 후 알림도 실행 확인). 실행 확인(정리 전): 캐시 적중, 로컬 영상 썸네일, Spotify artUrl 다운로드, 같은 artUrl 재사용, 알림의 앨범·아티스트 — `struct tray_art_job`(불투명)과 `system_tray_art_prepare`/`run`/`apply`/`request`/`free`. `lyrics_manager.c`는 md5·cache_path·from_art_url·iTunes 여부를 더 이상 다루지 않고, `async_art_fetch`에는 스레드·ready·should_cancel·job·알림 제목/플레이어만 남음(`notif_artist`/`notif_album`은 job의 요청에서 읽음). `enum tray_art_status` 삭제(prepare가 NULL이면 아이콘이 정해진 것). 동작 변화 의도 없음. #62(취소 시 worker를 기다리지 않고 버리기)는 2026-11-26 우산 이슈로 바꾸고 Task 넷으로 쪼갬(사용자 제안): #65 재현 수단 → #66 아트 worker, #67 가사 조회 worker의 설정 사본(`g_config` 읽기 제거) → #68 가사 조회 worker(#66·#67에 blocked). 아트만 고쳐서는 증상이 안 사라짐(가사 조회가 같은 마운트를 읽음).
- 2026-11-26: `5066304..eb34b02` 4개 커밋 푸시(파이프라인 904 성공, #60·#63 닫힘). **SonarCloud @ eb34b02: 새 이슈 5건, 전부 `c:S859`(CRITICAL)** — #63에서 넣은 `system_tray_art_free`의 `free((char *)job->request.…)` 캐스트(const 제거). 작업이 사본을 `char *strings[5]`로 따로 갖고 `request`는 그것을 가리키게 고침(**미커밋**, 빌드 통과). 11-25의 S995 2건은 목록에서 사라짐. 품질 게이트 OK.
- 2026-11-26: **#65 재현 성공** — 루트 없이 멈출 수 있는 마운트(sshfs `-o passive` + 로컬 sftp-server에 SIGSTOP; 스크립트 핵심은 #65 코멘트에). 마운트 위의 가사를 띄운 채 freeze → **메인 스레드가 `fstatat64`에서 멈춤**(gdb, Thread 1; 직전 로그 `Instrumental break` → `handle_instrumental_break`의 `stat()`). worker와 무관, 트랙을 넘기지 않아도 발생. 2초마다의 가사 파일 md5(`calculate_file_md5`)도 메인 스레드(소스로만 확인). 실제 마운트는 CIFS `soft`, `echo_interval=60`. → #62 아래 새 Task **#69**(가사 파일 확인을 감시 스레드로) 추가, 맨 앞.
- 2026-11-26: **#69** 작업, **bc7ed64(S859 수정은 e9c00e4), /simplify 완료, 푸시됨**(재로드 직전에 `lyrics_file_watch_set(NULL)`로 상태를 버림 — 옛 내용의 md5가 새로 읽은 가사와 비교돼 재로드가 한 번 더 도는 것을 막음; 캐시 디렉터리의 파일은 md5를 읽지 않음; S859 수정은 슬롯 배열 대신 이름 있는 `char *` 다섯 개로). 실행 확인(정리 전): 가사 줄·간주가 계속 진행, `.lrc` 저장 → `Lyrics file changed, reloading` → 번역 재시작, 파일 옮김 → `Lyrics file was deleted or moved`. **freeze 중 확인(정리 후 빌드, `freeze-test.sh`): 간주 구간에서 마운트를 8초 멈춘 상태로 D-Bus `AdjustTimingOffset`이 47 ms에 응답 → 메인 스레드 살아 있음(수정 전에는 같은 자리에서 `fstatat64`에 멈춤).** 검토에서 확인: 평상시 메인 스레드에 남은 마운트 접근 없음(가사 경로는 문자열 연산뿐, 아트의 realpath/stat은 worker). 남은 틈: `start_fetch`의 pthread_create 실패 시 동기 대체 경로가 메인에서 로컬 검색을 돎(드묾, 미수정). 미적용: 감시 스레드의 대기를 CLOCK_MONOTONIC으로(지금은 REALTIME — 시계가 뛰면 한 번의 대기가 늘거나 줄 뿐) — `src/monitor/lyrics_file_watch.{c,h}`: 분리된(detached) 감시 스레드가 2초마다 가사 파일을 `stat` + md5, 잠금 없이 파일 호출. 메인은 `lyrics_file_watch_set(path)`(매 틱, 경로 같으면 no-op)과 `lyrics_file_watch_get(&status)`만. `handle_instrumental_break`의 `stat()`과 2초 md5 읽기를 그 결과로 대체(`file_monitor_reload_if_changed` 분리). 상태를 모르면(막 로드됨, 파일 시스템 무응답) 아무것도 바뀌지 않은 것으로 봄. `stat`이 ENOENT/ENOTDIR 외의 오류면 상태를 갱신하지 않음(마운트 오류를 "파일 삭제"로 오인해 가사를 버리지 않게 — 기존 동작에서 달라지는 점).
- 2026-11-26: 사용자 결정 — **#22·#10은 나중에**("지금 잠깐 잘못된 가사도 잘 찾잖아": lrclib이 지금은 실패 없이 응답함). #62의 Task를 먼저. 테스트 중 본 기존 문제 1건, 고치지 않고 여기에만 기록: lrclib 검색 API가 제목만으로(`stuck-a`) 길이가 **111초 다른** 곡(`STUCK (A COLORS SHOW)`, 201 s vs 90 s)을 "best match"로 골라 가사를 캐시함(`Selected best match with duration diff: 110992 ms`) — 길이 차이에 상한이 없음. 아티스트·앨범이 없는 파일에서 엉뚱한 가사가 뜨고 캐시에 남음.
- 2026-11-26: **#65 시나리오 B 재현**(`scenario-b.sh`: 마운트를 멈춘 뒤 lyrics를 띄워 가사 조회 worker를 막고, 다른 플레이어로 넘김) — 판정 1 ALIVE(104 ms) → 트랙 넘김 → **판정 2 STUCK** → thaw → 판정 3 ALIVE. 막힌 것은 가사 조회 worker의 로컬 검색(`Trying: …/mnt/stuck-a.srt`), 메인은 `lyrics_manager_cancel_fetch`의 join. #66~#68의 전제 확인, 순서는 **#67 → #68 → #66**(가사 조회가 아트보다 먼저 걸림). 위 "잘못된 가사" 건은 사용자 지시로 이슈 등재 예정 — **GitLab이 19.6.1→19.6.2 업그레이드 중이라 쓰기 불가**, 본문은 `scratchpad/i70/body.md`에 준비. 푸시(e9c00e4, bc7ed64)와 #65 코멘트·#62 본문 갱신도 같은 이유로 대기.
- 2026-11-26: GitLab 업그레이드(19.6.2) 완료 후 처리 — e9c00e4·bc7ed64 푸시(#69 닫힘, 파이프라인 906), **#71** 등재(lrclib 검색 API가 길이 차이 상한 없이 채택 → 잘못된 가사·캐시; #22와 relates_to, `priority::low`), #65는 완료 조건 충족으로 **Done**(실제 CIFS에서의 멈춤 길이만 미측정). #62의 남은 순서를 **#67 → #68 → #66**으로 확정(사용자 동의): 링크를 `#68 blocks #66`으로 뒤집고, 수명 구조는 #68에서 만들며 #66은 `blocked:prereq`. #68의 선행은 #67만.
- 2026-11-26: Coverity 수동 실행(run 45687984049, bc7ed64) 제출 성공 — CID 1709922 소멸 여부와 새 결함은 대시보드 확인 필요. **#67** 작업, **79154a9 푸시됨, /simplify 완료**(검토가 **내가 놓친 읽기 하나를 찾음**: `try_load_lyrics_file` → `config_is_extension_enabled()`(config.c 안에서 `g_config.lyrics.extensions`를 strdup) — grep을 프로바이더 파일에만 해서 빠졌다. 후보 경로가 전부 검색의 확장자 목록에서 만들어지므로 검사 자체가 중복이라 호출과 함수를 삭제. `lrclib_provider.c`의 config.h include 제거). 실행 확인(정리 전): 로컬 가사, lrclib 폴백, 기본 검색 디렉터리 순서; 정리 후에도 로컬 가사 검색 확인 — `struct lyrics_search_options`(extensions·search_dirs·ignore_dirs·enable_lrclib)를 `start_fetch`(메인)에서 `lyrics_search_options_capture`로 복사해 `lyrics_find_for_track(track, data, options)`에 넘김. `lyrics_provider.c` 안에서는 스레드 로컬 `t_options`(호출 동안만 설정)로 읽음 — 인자로 내리면 `build_search_directories`가 8개 인자가 되어 S107에 걸림. 프로바이더 `is_enabled` 훅은 옵션을 인자로 받음. 가사 조회 worker 경로에 `g_config` 읽기 없음(남은 2곳은 메인 스레드: 번역 시작, capture). `monitor_track_and_files`의 `!async_fetch.thread_active` 조건은 **남김** — 이제 메모리 안전 때문이 아니라, 조회 중에 재로드가 시작되면 새 트랙의 첫 로드가 same-track 재로드로 바뀌어 아트·알림이 빠지기 때문(주석 갱신). CLAUDE.md에 "worker는 설정을 읽지 않는다" 패턴 추가.
- 2026-11-26: **#68**(취소된 가사 조회 worker를 기다리지 않고 버림) 작업, **2d725fd, /simplify 완료, 푸시됨(파이프라인 910 성공, SonarCloud 0건·게이트 OK, 정리 후 재확인 완료)**(실행 확인: 정리 전 빌드로 `scenario-b.sh` 판정 2 ALIVE 7 ms, `EXIT_WHILE_FROZEN=1` 2113 ms 뒤 종료. 정리로 바뀐 것 — 스레드 생성 실패 시의 메인 스레드 동기 검색 제거(가사 없음으로 처리, #67 때 적은 '남은 틈' 해소), `try_load_lyrics_file` 맨 앞 취소 확인, lrclib 캐시를 임시 이름에 쓰고 rename, 스레드 이름 `lyrics-fetch`, 버려진 조회가 끝나면 로그 한 줄. 정리 후 재확인 대기). 조회마다 힙에 `struct lyrics_fetch_job`(`lyrics_manager.c` 내부)을 만들고 worker는 detach. 상태는 CAS로 한 번만 `RUNNING → DONE`(worker) 또는 `RUNNING → ABANDONED`(메인, `lyrics_manager_cancel_fetch`) — 진 쪽이 해제. `struct async_lyrics_fetch`에는 `job` 포인터만 남음(join·`thread_active`·`ready` 없음). 버려진 조회는 다음 provider로 넘어가지 않고 lrclib 캐시도 쓰지 않음(`try_providers`의 `thread_cancel_requested()`). 종료 시 `lyrics_manager_shutdown`이 fetch worker를 최대 2 s 기다린 뒤 진행(막힌 worker는 두고 종료 — 이전에는 ^C가 join에서 멈춤). `main.c`의 2초 점검 조건은 `!async_fetch.job`으로 유지(이유 동일). 검증 수단: `scenario-b.sh`(판정 2), `EXIT_WHILE_FROZEN=1 scenario-b.sh`(멈춘 채 SIGINT). 남는 것: 아트·번역 worker는 여전히 join(#66), 버려진 worker는 마운트가 돌아올 때까지 스레드 하나를 차지. 알려진 한계: 조회가 막혀 있는 동안 `main.c`의 2초 점검이 건너뛰어져 settings.ini 저장이 반영되지 않음(이전에도 같음). #66 때 전이·카운터를 공용 헬퍼로 뽑을 것(종료 대기는 프로세스에 하나). 아래 'worker ↔ 메인 공유 상태' 항목의 join 서술은 이제 아트·번역에만 해당.
- 2026-11-26: **#66**(취소된 아트 worker를 기다리지 않고 버림) 작업, **f149966(헬퍼 분리)·11810f4, /simplify 완료, 푸시됨** — 정리 후 재확인 완료(`ART=1` 판정 2 ALIVE 96 ms, 실제 사용에서 썸네일 추출·Spotify artUrl·lrclib 캐시 쓰기 정상). **#62(우산)는 Task 다섯이 모두 끝나 Done으로 닫음**(정리 전 실행 확인: `ART=1` 판정 2 ALIVE 95 ms·thaw 뒤 'An artwork lookup that was dropped has ended'·stuck-a 알림 없음, `ART=1 EXIT_WHILE_FROZEN=1` 2098 ms, 기본 시나리오 ALIVE 97 ms, 실제 사용 캐시 적중. 정리로 바뀐 것 — 임시 이름+rename을 `fopen_private_tmp`/`fclose_into_place`(file_utils)로 모음(lrclib 캐시는 쓰기 오류 검사도 얻음), `file://` artUrl의 realpath 뒤 취소 확인, `worker_handoff_done` const, 옛 join 주석 정리). 두 단계 — (1) #68의 전이·카운터를 `src/utils/thread/worker_handoff.{c,h}`로 분리(`start`/`enter`/`finish`/`done`/`abandon`/`wait_all`), 가사 조회가 그것을 사용(동작 변화 없음; `scratchpad/i66/step1.patch`에 이 단계만 보관). (2) 아트 worker를 같은 구조로: `struct art_fetch_job`(handoff + `tray_art_job`), `async_art_fetch`에는 `job` 포인터만, `lyrics_manager_cancel_art_fetch`는 join 없음. ffmpeg 임시 출력은 `extracted-art.<tid>.png`, `save_png_private`는 임시 이름에 쓰고 rename(모든 PNG 쓰기에 적용), 버려진 조회는 ffmpeg를 시작하지 않음. 종료 순서를 `lyrics_manager_shutdown` → `system_tray_cleanup`으로 바꿈(아트 worker가 쓰는 libcurl을 트레이 정리가 내림). 이제 `cancel_track_work`가 기다리는 것은 번역 스레드뿐. 검증 수단: `ART=1 scenario-b.sh`(로컬 검색·lrclib·iTunes를 끄고 빈 캐시로 띄워 아트 worker가 마운트에서 막히게 함), `ART=1 EXIT_WHILE_FROZEN=1`. 남긴 것: SIGKILL 뒤의 블로킹 `waitpid`(ffmpeg 자체가 마운트에서 못 죽으면 worker가 머묾 — 같은 트랙이 계속 재생 중이면 그 트랙의 알림·아이콘이 안 나옴, 이전부터 같음), 종료 시 못 끝난 worker의 `extracted-art.<tid>.png`(tmpfs), 포인터 하나짜리 `async_*_fetch` 래퍼. 아래 'worker ↔ 메인 공유 상태' 항목의 join 서술은 이제 번역 스레드에만 해당.
- 2026-11-26: 11810f4 푸시 뒤 — 파이프라인 912는 6개 잡 성공, `publish:copr-nightly`만 실패(`Unable to connect to https://copr.fedorainfracloud.org/api_3/`, 코드 무관). 재시도 잡(2544)은 빌드 11394009를 제출했으나 COPR 대기열에서 30분 넘게 pending인 채 감시 중 API 연결이 다시 끊겨 실패 — 빌드는 COPR에 남아 있으므로 **다시 재시도하지 않음**(중복 제출). SonarCloud @11810f4 0건·게이트 OK. Coverity 수동 실행(run 45820082904, 11810f4) 제출 성공, 45개 컴파일 단위 — 결과는 대시보드 확인 필요(CID 1709922 소멸 여부, #60~#66의 새 결함). **사용자 확인(2026-11-26): 대시보드 결함 0건.**
- 2026-11-26: **#71**(lrclib 검색 결과를 길이 차이와 무관하게 채택) 작업, **f053e5e 로컬 커밋·미푸시, 정리 검토 완료**(실행 확인: 90 s 태그 없음 → 110992 ms 차이로 거름·캐시 없음, 150 s → 89992 ms로 거름, 272 s → 8 ms로 채택·캐시 1개. 검토가 짚은 것: **캐시 조회(`try_load_from_cache`)는 길이를 보지 않는다** — 이미 캐시된 틀린 가사는 계속 나오고, 아티스트 없는 파일은 키가 제목뿐이라 길이가 다른 파일이 한 항목을 공유. 이슈는 닫지 않고 `Part of #71`, 처리 방법은 사용자 결정 대기). `lrclib_provider.c`만: 검색 API로 고른 결과의 길이 차이가 `SEARCH_MAX_DURATION_DIFF_MS`(5000)를 넘으면 쓰지 않음(가사 없음 → 캐시도 안 씀); 트랙 길이를 아는데 결과에 길이가 없으면 그 결과는 건너뜀; 아티스트도 길이도 없으면 검색 요청 자체를 하지 않음. 제목 비교는 넣지 않음. 정한 값은 내 제안이고 사용자 확인 전: ±5 s(lrclib `/api/get`은 ±2 s; 더 벌어지면 맞는 곡이어도 싱크가 어긋남). 검증 수단: `scratchpad/i71/test.sh`(무음 시험 트랙 셋 — 90 s 태그 없음, 150 s·272 s `Indila - S.O.S`+가짜 앨범; 예시 설정·빈 캐시로 실행). 이미 캐시에 들어간 잘못된 가사는 이 수정으로 지워지지 않음. **후속(작업 트리, 미커밋, 실행 확인 대기)**: 사용자 결정(2026-11-26)으로 캐시 파일에 `[length:mm:ss]` 태그 — `cache_lrclib_lyrics`가 트랙 길이를 적고, `try_load_from_cache`가 ±5 s(`LRCLIB_MAX_DURATION_DIFF_MS`, lrclib_provider.h로 옮김)로 비교해 다르면 캐시를 쓰지 않고 다시 찾음(파일은 두고, 맞는 가사를 찾으면 덮어씀). 태그 없는 옛 캐시는 마지막 줄 시각이 트랙 길이+5 s를 넘을 때만 거름(한쪽만 봄). `parse_lrc_metadata_tag`가 `[length:]`를 읽어 `metadata.length_ms`에 둠(파서 공용, fuzz_lrc 25 s·78만 회 무결). 검증 수단: `scratchpad/i71/test-cache.sh`. 검토가 짚은 기존 결함 둘(검색 응답의 객체 끝을 첫 `}`로 찾음 → 가사에 중괄호가 있으면 유령 결과; `duration` 키가 없으면 다음 결과의 값을 읽음)은 손대지 않음, LOW.
- 2026-11-27: #71 캐시 후속을 **8f90965로 로컬 커밋**(실행 확인: 옛 형식 캐시 거름, 272 s 트랙 캐시에 `[length:04:32]`, 재생 시 캐시 적중, 150 s 같은 태그는 거름). 검토로 `[length:3:]`이 3:00으로 읽히던 것을 `parse_length_tag`로 고침. 남긴 것: 태그에 lrclib 결과의 길이가 아니라 받아 온 트랙의 길이를 적으므로 허용 오차가 겹침(최대 10 s), 같은 키·다른 길이의 두 파일은 캐시를 번갈아 덮어씀(번역 캐시도 빗나감). **사용자 제안(2026-11-27)으로 JSON 읽기 fuzz 타깃 추가 — 작업 트리, 미커밋**: 검색 결과 고르기를 curl 없는 `src/provider/lrclib/lrclib_response.{c,h}`로 옮기고(`lrclib_find_best_match`), `fuzz/fuzz_lrclib.c`가 그것과 `json_utils.c`를 링크. 씨앗 6개(`fuzz/corpus/lrclib/*.txt`), `build:fuzz`의 반복 목록에 추가. 90 s·352만 회 + 30 s 무충돌. 결과 길이가 86400 s를 넘으면 비교하지 않음(`* 1000` 넘침 방지). fuzz는 메모리 오류만 잡으므로 앞의 LOW 둘(첫 `}`, `duration` 키 없음)은 그대로 — 논리 결함이라 충돌이 나지 않음. `json_extract_string`의 닫는 따옴표 판정도 `\\"`(역슬래시 뒤 따옴표)를 잘못 읽음(문자열을 지나쳐 읽지만 버퍼 안).
- 2026-11-27: fuzz 타깃 **b9b120e로 로컬 커밋**(`Closes #71`, 미푸시; 제어문자·NUL 씨앗 추가 후 60 s 더, 합계 약 680만 입력 무충돌). 캐시 파일로의 태그 끼워 넣기 확인(`scratchpad/i71/inject.c`, 파서만 링크): 가사 줄 안의 CR은 줄을 나누지 않음, 줄의 본문으로 온 `[length:..]`·`[offset:..]`는 태그로 읽히지 않음(본문에서 빠질 뿐), `[length:]`가 둘이면 뒤의 것이 이김(캐시 작성부는 ar/ti/al 뒤에 적으므로 MPRIS 제목에 줄바꿈으로 끼운 태그를 덮음 — 단 트랙 길이를 몰라 태그를 안 적는 경우는 끼운 것이 남고, 영향은 그 항목 하나). lrclib이 가사 본문에 `[offset:]`·`[ti:]` 줄을 넣으면 받아 올 때 적용됨(이전부터 같음, 서버가 어차피 타임스탬프를 정함). 닫는 따옴표 판정(`\\"`)은 LOW로 남김.
- 2026-11-27: f053e5e·8f90965·b9b120e 푸시 — 파이프라인 915 성공(7개 잡 전부, COPR 포함), `build:fuzz`에서 새 `lrclib` 타깃 66만 회 무충돌, #71 닫힘, SonarCloud @b9b120e 0건·게이트 OK·hotspot 0.
- 2026-11-27: **#22**(lrclib 5xx·타임아웃 재시도) 작업, **작업 트리에만(미커밋), 실행 확인 대기, /simplify 미실시**. `lrclib_provider.c`의 요청부를 다시 씀: 요청 헬퍼 둘을 `lrclib_fetch` 하나로(핸들 하나를 get·search가 같이 씀), 전송 결과를 OK/NOT_FOUND(404)/TRANSIENT(5xx·429·타임아웃·연결 실패 등)/UNAVAILABLE로 분류, TRANSIENT만 최대 3회 시도(대기 1 s → 3 s, `Retry-After`가 더 길면 그것, 5 s를 넘게 요구하면 재시도 안 함). 대기는 `thread_cancel_sleep_ms`(번역기의 `translator_sleep_cancellable`을 `thread_cancel.c`로 옮겨 같이 씀)로 취소 가능. 정확 일치가 UNAVAILABLE로 끝나면 search로 넘어가지 않음(같은 서버). 404는 INF, 일시 오류는 WRN. User-Agent에 홈페이지(`wshowlyrics/X.Y.Z (https://github.com/wshowlyrics/wshowlyrics)`, lrclib 문서 요구·iTunes·번역 요청에도 같이 쓰임), CLAUDE.md 릴리스 sed를 버전만 바꾸도록 고침. 검증 수단: `scratchpad/i22/mock.py`(일부러 실패하는 모의 서버) + `test.sh`, 시험 빌드 `build-mock`(`-DLRCLIB_MOCK_PORT=8071` — 런타임 훅 없음, 일반 빌드에는 127.0.0.1 문자열 없음 확인). 정하지 않은 것: 재시도까지 실패한 트랙을 같은 재생 중 다시 시도(범위 밖), 분류 결과를 #10이 쓰도록 밖으로 내보내기. **1차 실행 확인(정리 전)**: 503·503·200이 0/1001/4006 ms, 429 뒤 2003 ms, Retry-After 60 → 한 번만, 503×3 → 포기·search 없음, 404 → search·재시도 없음, 대기 중 트랙 변경 → 세 번째 요청 없음·222 ms 뒤 조회 종료. (첫 실행의 두 실패는 시험 도구 탓: 모의 서버가 콜론 뒤 공백을 넣어 `json_extract_string`이 못 읽음 — 추출기가 공백 하나에 실패하는 것은 실제 약점, 다음 커밋 대상; stdout 버퍼링으로 종료 줄을 제때 못 봄 → `stdbuf -oL`.) **/simplify 반영(재실행 확인 대기)**: 404 아닌 4xx를 `LRCLIB_REJECTED`로 갈라 search로 계속(이전 동작 회귀 방지), 실패 횟수를 조회 단위로 공유(`struct lrclib_session`, 최악 약 58 s → 34 s), `lrclib_lookup`이 enum을 돌려줌(#10용), 공통 curl 옵션은 세션당 한 번, 로그 문구 중립화('request failed'), `wait_for_retry`는 bool, `str_is_set`을 string_utils.h로(중복 static 둘 제거), 번역기의 200/429/500 리터럴을 상수로, meson 옵션 `lrclib_mock_port` + 시작 시 WRN. 남긴 것: iTunes와 같은 curl 옵션 체인 중복(별도 커밋감), 포기 뒤 다음 트랙에서 바로 다시 요청(서버의 Retry-After를 프로세스 단위로는 안 지킴 — #10과 함께), TLS 실패 분류는 모의 빌드로 검증 안 됨.
- 2026-11-27: #22 재시도 수정을 **4352a2d로 로컬 커밋**(정리 후 재실행: 일곱 경우 전부 기대대로 — 400은 재시도 없이 search로, 취소 후 220 ms; 실제 lrclib 상대 `i71/test.sh`도 이전과 같음). **응답 읽기 수정 — 작업 트리, 미커밋, 실행 확인 대기, 검토 미실시**: `json_utils`를 다시 씀 — 닫는 따옴표를 역슬래시 쌍을 세어 찾음(`\\"`가 문자열을 닫음), 키 뒤 콜론 앞뒤의 공백 허용, 값이 키처럼 생긴 문자열은 건너뜀, `\/` 해제, 범위 제한 조회 `json_extract_string_in`/`json_extract_int_in`와 `json_object_end`(문자열 안의 중괄호·중첩 객체를 건너뜀) 추가, `json_extract_int_from` 제거. `lrclib_find_best_match`는 결과 객체를 제 중괄호 안에서만 읽음(`duration`이 없는 결과가 다음 결과의 값을 빌리지 않음), 고른 결과의 메타데이터도 그 범위에서. 확인: 검사 프로그램 22항목 전부 ok(`scratchpad/i22/jsontest.c`, 커밋 안 함), fuzz_lrclib 90 s·390만 회 무충돌, 씨앗 `spaced.txt` 추가.
- 2026-11-27: 응답 읽기 수정을 **2cfa239로 로컬 커밋**(`Closes #22`, 미푸시). 실행 확인: 실제 lrclib·모의 서버 양쪽 통과. **실제 응답으로 결함 확인**: `stuck-a` 검색이 전에는 5건(201 s×2, 260 s×3)으로 보였는데 새 코드는 3건(260 s) — API를 직접 조회하니 201 s 두 건은 `syncedLyrics:null`이고, 옛 코드가 그 길이·아티스트·제목에 뒤 결과의 가사를 붙이고 있었음(#71에서 본 'WESTSIDE BOOGIE' 가사는 실제로는 뒤 결과의 것). 검토 반영: 길이 차이 계산을 순수 함수로(가사 복사 전에 길이부터 비교), iTunes `resultCount`를 숫자로 읽음(문자열로 읽어 'no tracks found'가 한 번도 안 찍히던 죽은 코드). 남긴 것: 응답 크기 상한 없음(`curl_utils.c`), `find_value`의 strstr가 범위 끝을 넘어 스캔(키가 전혀 없는 큰 응답에서 이차 비용 — 상한을 둘 곳은 응답 크기 쪽), `\uXXXX` 미해제(이전과 같음). 정리 뒤의 lrclib 실행 확인은 검사 프로그램·fuzz로만 함(사용자 재실행 전).
- 2026-11-27: 정리 후 빌드로 `i71/test.sh` 재확인(8 ms 채택·47줄, 로그는 가장 가까운 결과에만). **사용자 요청으로 응답 크기 상한 + curl 옵션 묶음 통합 — 작업 트리, 미커밋, 실행 확인 대기, 검토 미실시**(이슈 없이 이 문서로 추적): `curl_utils`에 `curl_setup_get`(TLS 1.2+, UA, 10 s/5 s, 리다이렉트, NOSIGNAL, 취소, 본문 상한)과 `curl_memory_buffer.max_size`(기본 `CURL_RESPONSE_MAX_BYTES` 2 MiB, 쓰기 콜백에서 강제 + `CURLOPT_MAXFILESIZE_LARGE`). GET 네 곳이 이것을 씀 — lrclib(자체 `setup_session` 제거), iTunes(`setup_itunes_request` 제거), 트레이 아트 다운로드(`artwork_write_capped` 제거, 8 MiB 유지), `--help`(5 s 유지). 번역기 콜백에도 같은 상한. 동작 변화: 아트 다운로드에 User-Agent가 붙음, `--help`에 TLS 1.2 하한·연결 5 s가 생김, lrclib·iTunes·help·번역 응답이 2 MiB를 넘으면 중단(lrclib에서는 재시도 없이 UNAVAILABLE). 검증: 모의 서버에 `huge`(길이 미고지 3 MB)·`hugelen`(Content-Length 3 MB) 추가. → ** 로컬 커밋(미푸시)**. 모의 서버 아홉 경우 통과(`huge`·`hugelen` 모두 요청 한 번, 'Maximum file size exceeded', 재시도·search 없음 — libcurl 8.23은 길이 미고지 본문도 스스로 끊어 콜백의 검사까지 오지 않음; 콜백은 검사 프로그램으로 직접 확인: 한도까지 허용, 1바이트 초과 거부). 주석·문서의 '옵션은 알린 길이만 본다'를 8.4 미만 한정으로 정정. **실제 서버 미확인**: iTunes, 아트 다운로드(UA 추가), `--help`. 남긴 것: 번역기의 `translator_curl_response`가 `curl_memory_buffer`의 복사본(상한 로직 두 곳), 리다이렉트 프로토콜 제한(7.81에는 `_STR` 옵션이 없어 버전 분기 필요), S4423이 `curl_utils.c`에 새 키로 열릴 수 있음(정책대로 falsepositive 재표시 필요 — 토큰 파일 없음). **실제 서버 확인 완료(2026-11-27)**: `--help` 전문, lrclib 정확 일치(실제 TLS), Spotify artUrl 다운로드(UA 포함), iTunes `found artwork`·`no tracks found`(무음 음원 둘, `scratchpad/i22/itunes-test.sh` — resultCount 숫자 읽기가 처음으로 실행됨). 번역 응답 상한만 미실행(요청이 취소됨).
- 2026-11-27: 4352a2d·2cfa239·fa52690 푸시 — #22 닫힘, Coverity 수동 실행(run 45972126185, fa52690, 46 단위) 제출 성공(결과는 대시보드). **SonarCloud @fa52690: 미해결 4건, 게이트 ERROR** — `c:S886` json_utils.c:87(for 본문에서 루프 변수 증가, 내가 2cfa239에서 넣음), `c:S4423` ×3 lrclib_provider.c:405·itunes_artwork.c:68·system_tray.c:106(TLS 옵션을 `curl_setup_get`으로 옮기자 `curl_easy_init` 호출 지점에서 같은 함수 안의 설정이 안 보여 다시 열림 — 검토가 `curl_utils.c`에 열릴 것으로 예상했으나 실제는 호출 지점). **수정안(작업 트리, 미커밋)**: 루프를 while로; `curl_handle_new()`가 init과 TLS 하한을 한 함수에서 하고 핸들 다섯(번역기 포함)이 모두 그것으로 생성 — 표시가 아니라 코드로 해소. 빌드 3종·검사 프로그램 통과. → **164c044로 로컬 커밋(미푸시)**: 실제 서버 확인 — iTunes(found artwork·no tracks found), `--help`, lrclib 정확 일치(같은 실행의 로그에서 43줄). 번역기 핸들만 미실행. 파이프라인 917은 7개 잡 전부 성공(COPR 포함). 재분석에서 4건이 사라지는지는 푸시 후 확인. **Coverity @fa52690: 사용자 확인(2026-11-27) 결함 0건.**
- 2026-11-27: 164c044 푸시 → **SonarCloud @164c044: 4건 → 1건, 게이트 ERROR 유지**. 남은 것은 `c:S4423` curl_utils.c:62(키 `AaIca5Tx5B_5k76XjBKg`) — `curl_handle_new` 안의 TLS 1.2 설정 줄 자체. 내 추정(핸들 생성 함수 안에서 설정이 보이면 풀린다)은 틀림: 이 규칙은 TLS 설정 지점마다 열리고, 과거 키들(main.c·translator·lrclib·itunes·tray·deepl 9개)은 정책대로 오탐 표시돼 있다가 이번에 코드가 옮겨지며 FIXED로 닫힘. 이제 표시할 곳이 하나로 줄었음 — 정책(표의 S4423 = falsepositive, 'Code explicitly enforces CURL_SSLVERSION_TLSv1_2')대로 표시 필요, 토큰 파일 없음. `S886`은 해소. 파이프라인 918 진행 중(확인 시점에 build:makepkg 성공, build:fuzz 실행 중).
- 2026-11-27: 파이프라인 918 성공(7개 잡). **0.12.0 사전 점검**: v0.11.2 이후 74커밋(fix 39·ci 11·docs 8·refactor 7·feat 7). 태그 때만 도는 deb/PPA 빌드 환경을 미리 재현 — `ubuntu:22.04` 컨테이너(gcc 11.4, meson 0.61.2, libcurl 7.81.0, glibc 2.35)에서 HEAD(164c044)가 경고 0으로 빌드됨. NUR이 치환하는 config.c 리터럴 셋 그대로. 릴리스 전 남은 것: S4423 오탐 표시 1건(게이트 ERROR), 번역 경로 실행 확인(핸들 생성·응답 상한 변경 후 미실행), 164c044 Coverity 미실행, 태그 파이프라인 동안 HM-iMac 로그인 유지(docker-modern), 태그에서 닫을 need-verify(#48·#30).
- 2026-11-27: **v0.12.0 릴리스**. 감사 문서 9ccca27, 버전 커밋 592d168(사용자), 모의 서버 b89d070(`tests/lrclib-mock/`)을 master로 푸시 → 파이프라인 919 성공(7개 잡) → 서명 태그 `v0.12.0`(592d168) 푸시 → **태그 파이프라인 920 성공(14개 잡)**. GitHub 릴리스 v0.12.0 생성(deb·rpm·AppImage+asc 등 자산 8개), COPR 빌드 11399417 succeeded. 이 태그를 기다리던 `need-verify` 넷을 조건 대조 후 **Done**으로 닫음: #48(copr-stable 통과; Alpine `sh`의 `.wait-github-tag`는 오류 없이 지나갔으나 미러에 태그가 이미 있어 기다리는 분기는 실행 안 됨), #30(소요 시간을 #28에 기록: ppa:tarball 612→57초, github-release 844→52초), #38, #37. #10은 #22가 닫혀 `blocked:prereq` 해제·`priority::low`(weight는 시작 때 Task로 나눠서; TSan 항목은 #60·#62로 전제가 바뀌어 다시 적어야 함). 모의 빌드용 임시 디렉터리 이름의 `lrclib`가 로그 필터에 걸리던 것은 커밋 전에 고침. Coverity @164c044 결과는 아직 전달받지 못함.
- 2026-11-25: 추적 방식 메모 — 11-23의 SEC-5부터 수정마다 이슈를 만들고 `Closes #N`으로 닫는 흐름이 되면서, 후속 건까지 이슈로 나가 이틀간 #49~#64가 생김(사용자 지적: "이슈가 스팸수준"). **이 문서가 감사 항목의 추적 원본**이고, 이슈는 커밋을 닫을 대상이 필요할 때와 감사와 무관하게 따로 일정이 잡힐 건에만 쓴다. 정리 단계에서 나온 관련 건은 진행 중인 이슈에 넣는다.

---

## 남은 작업 — 실제 코드 변경 필요

### 🔒 보안 — 2026-07-06 감사 발견 (신규)

| ID | 심각도 | 위치 | 문제 | 상태 |
|---|---|---|---|---|
| ~~SEC-1~~ | MEDIUM | mpris.c | 악성 MPRIS 메타데이터로 NULL deref DoS (GVariant 타입 미검증) | ✅ 완료 (38d1a85) — `dup_string_variant` 헬퍼 + PlaybackStatus 타입 가드 |
| ~~SEC-2~~ | MEDIUM | system_tray.c | artUrl 경유 SSRF + `file://` 임의 파일 디코드 | ✅ 완료 (89530c9) — 매직바이트 포맷 allowlist + realpath/S_ISREG + 크기상한. **IP 차단은 의도적 제외** (로컬 동일유저 위협모델에선 무의미 + 자체호스팅 파괴) |
| ~~SEC-3~~ | LOW | config.c | symlink 폴백이 resolved-path 검증 우회 + prefix 경계 미검사 | ✅ 완료 (f992c86) — `path_has_dir_prefix` 경계검사 + 폴백을 `is_secret_store_path`(resolved가 /run·$XDG_RUNTIME_DIR)로 제한 |
| ~~SEC-4~~ | LOW | lyrics_provider.c | `%00` 디코딩 시 embedded NUL 허용 (경로 절단) | ✅ 완료 (f992c86) — `val != 0` 조건으로 `%00` 리터럴 유지 |

### 🔎 2026-11-22 감사 발견 (신규, 전부 미수정)

확신 정도: **확인함** = 코드를 읽어 경로를 끝까지 따라감 (런타임 재현은 하지 않음) / **가능성** = 조건부이거나 외부 동작에 의존.

| ID | 심각도 | 위치 | 실패 시나리오 | 확신 |
|---|---|---|---|---|
| ~~R18~~ | MEDIUM | translator_common.c:423-433 `translator_rate_limit_delay`, :962-976 HTTP 오류 백오프, lyrics_manager.c:60-79 | 번역 중 트랙을 넘기면 `cancel_and_wait_translation`이 **메인 스레드에서** 최대 5초 대기. 번역 스레드는 요청 사이에 `nanosleep(rate_limit_ms)`(기본 6000ms, 최대 60000ms)로 자고 있어 취소 플래그를 못 봄. 남은 잠이 5초를 넘으면 `pthread_cancel`로 강제 종료 → 캐시 저장 없이 이번 실행분 번역 유실(과금분 포함), `args`/curl 핸들/요청 버퍼 누수, `translation_in_progress`가 true로 남음. HTTP 오류 백오프는 서버가 지정한 시간(상한 없음)만큼 잠. worker thread 도입(335f000)의 "메인 루프를 멈추지 않는다"가 이 경로에서 깨짐 | ✅ 완료 (8fa788e, 이슈 #53) — 50ms 단위 취소 가능 대기, 공용 핸들 타임아웃 60s/연결 5s + 취소 콜백, DeepL도 포함. `claude-haiku-5-5`로 요청 중·대기 중 취소 확인. DeepL 경로는 미실행 |
| ~~R19~~ | MEDIUM | translator_common.c:915-919, :1036 | 공용 번역 핸들(OpenAI/Gemini/Claude)에 `CURLOPT_TIMEOUT`·`CONNECTTIMEOUT`이 **없음** (6cc04d5는 NOSIGNAL만 추가). 연결이 멈춘 채 열려 있으면 `curl_easy_perform`이 끝나지 않고, 취소 콜백도 없어 R18의 5초 대기 → `pthread_cancel` 경로로 감. DeepL 핸들만 TIMEOUT 30s. lrclib/iTunes/아트 핸들은 TIMEOUT+CONNECTTIMEOUT+NOSIGNAL+취소 콜백 모두 적용됨, `--help` 핸들은 TIMEOUT 5s+NOSIGNAL | ✅ 완료 (8fa788e, 이슈 #53) — R18과 같은 커밋 |
| ~~R20~~ | MEDIUM | system_tray.c:1069-1093, :1309-1312, :1015-1030; main.c:418 | "FAST(네트워크 없음)" 경로에 블로킹이 남아 있음. (a) `run_ffmpeg_extract`가 메인 스레드에서 `g_spawn_sync`를 **타임아웃 없이** 호출 — 느린/끊긴 네트워크 마운트나 디코더를 붙잡는 파일이면 오버레이·트레이·D-Bus가 멈추고, SIGTERM도 `run=0`만 세울 뿐 빠져나오지 못함. `thumbnail` 필터는 100프레임을 디코드하므로 고해상도 영상은 정상 케이스도 체감 지연. (b) `mpris:artUrl`이 http(s)면(Spotify는 항상) `try_mpris_artwork`→`download_image`가 메인 스레드에서 최대 10초. (c) `handle_instrumental_break`의 재검색은 여전히 동기 `lyrics_find_for_track`(lrclib 포함) | ✅ (a)(b) 80669d4 (#61), (c) e2bccaf + 69a7eb1 (#64, 둘째 커밋은 가사 파일 저장·번역 설정 변경 재로드) — 미푸시. 남은 한계: 멈춘 마운트에서 worker join이 메인 스레드를 잡음 (#62) |
| ~~R21~~ | MEDIUM | 위 SonarCloud 표 | 신규 18건. ~~BLOCKER `S912` main.c~~ ✅ e4a990a, ~~HIGH `S134` ×2~~ ✅ 1c8afd8. **남은 15건**: MAJOR `S1121` ×5 (lyrics_manager.c:213/216), `S107` ×1 (system_tray.c `system_tray_update_icon_fast` 9 파라미터 — 자체 함수라 wontfix 대상 아님, 파라미터 구조체로), MINOR `S995` ×4, `S5350` ×4, `S886` ×1 | ✅ 완료 — BLOCKER/HIGH 3건 (e4a990a, 1c8afd8), 나머지 15건 (e9eef27). 해소 여부는 푸시 후 재분석으로 확인 |
| ~~CI-1~~ | MEDIUM | .github/workflows/coverity-scan.yml:26 | `libgdk-pixbuf2.0-dev` → Ubuntu 26.04에 없음. 주간 Coverity 스캔이 11-22 실패 | ✅ 완료 (424c2cf) — run 45116677248 성공 |
| ~~CI-2~~ | LOW | .gitlab/actions/publish.yml `publish:copr-nightly`/`copr-stable` | `uvx --from copr-cli`와 `alpine:latest`가 버전 고정 없이 최신을 받음 | ❌ 기각 (2026-11-23 결정) — 올려 줄 장치 없이 고정만 하면 조용히 구버전에 머문다. 전역 CLAUDE.md의 CI 버전 규칙(채널 태그, 또는 고정 + 올려 주는 장치)에서 현재 형태는 "채널 태그"에 해당하므로 그대로 둔다. 장치 없는 고정은 제안하지 않는다 |
| ~~R22~~ | LOW | lyrics_manager.c:82-107, :461-489 | 온라인 검색 중 플레이어가 종료되면 `handle_no_player_found`가 async fetch/art를 취소하지 않음 → 검색이 끝나면 `poll_load`가 죽은 트랙의 가사를 설치하고, 제목이 빈 알림("🎵 ")을 보내고, iTunes 조회와 **번역(API 과금)** 을 시작함 | ✅ 완료 (0383670, 이슈 #54) — `handle_no_player_found()`에서 두 fetch 취소. 검색 요청 직후 mpv를 종료해 확인: `No player found` 뒤 `lrclib search API failed (HTTP 0)`만 나오고 가사 설치·알림·번역 없음 |
| ~~R27~~ | LOW | lyrics_provider.c:90 `remove_extension` | 확장자 목록에 없는 파일(`.mka`, `.m4b`, `.wma`, `.ts` 등)은 URL 파일명에 확장자가 남아 `song.mka.lrc`를 찾음 → 같은 이름의 `song.lrc`를 못 찾음. 제목 기반 패턴은 정상. `lyrics_manager_clean_title()`에 별도의 다른 확장자 목록이 있음. 감사 범위 밖의 기존 동작이며 R22 테스트 로그에서 발견 → **이슈 #55로 등재, 수정은 미착수** | ✅ 완료 (1d831b0, #55) + 제목 쪽 후속 (5b852ec) — 미푸시. 목록은 세 벌이었음 |
| ~~R23~~ | LOW | system_tray.c:696, lyrics_provider.c:725, translator_common.c:192 외 메인 스레드 6곳 | `umask(0077)`…`umask(old)` 토글이 이제 4개 스레드(메인, fetch worker, art worker, 번역)에서 동시에 일어남. umask는 프로세스 전역이라 교차하면 (1) 캐시 파일이 0644로 생성되거나 (2) 프로세스 umask가 0077에 고정되어 "Edit Settings"로 띄운 터미널/에디터가 0600 파일을 만들게 됨. 캐시 디렉터리가 0700이라 노출 영향은 작음 | ✅ 완료 (0396394) — `fopen_private()`/`save_png_private()`로 교체, 소스에 `umask` 호출 없음. 실행 중인 프로세스에서 PNG 3종이 600으로 생성되고 umask는 0022 그대로임을 확인 |
| ~~SEC-5~~ | LOW→버그 | system_tray.c:801, :894-902 | `notify-send` argv에 `--`가 없고 본문이 앨범명으로 시작. 앨범 태그가 `-`로 시작하면(예: `-ERROR`) 옵션으로 해석되어 `Unknown option`, exit 1로 끝나고 **알림이 뜨지 않음**. spawn 자체는 성공하므로 로그에도 남지 않음. 11-23에 같은 인자로 직접 실행해 재현, `--` 추가 시 정상 동작 확인 | ✅ 완료 (af72173, 이슈 #49) — argv에 `"--"` 추가, `werror` 빌드 클린. `ALBUM=-ERROR` 파일 재생으로 알림 표시 확인 (11-23) |
| ~~SEC-6~~ | LOW | system_tray.c:1037-1061, :1141-1148, :1197-1205 | ffmpeg 입력 하드닝 여지. 경로는 `realpath` 결과라 항상 `/`로 시작 → `-` 시작 인자 주입은 **불가(문제 없음)**. 남는 것: (a) `stat` 후 ffmpeg가 다시 여는 TOCTOU — 동일 사용자만 가능, R20(a) 타임아웃이 있으면 무해. (b) `-protocol_whitelist` 미지정 — 음악 파일로 위장한 HLS/concat 플레이리스트가 ffmpeg로 다른 로컬 파일을 열게 할 수 있음(출력은 검증된 PNG뿐이라 유출 경로는 없음) | ✅ 완료 (b718934) — `-protocol_whitelist file`. 실제 영상·커버 든 FLAC에서 추출 정상 확인. TOCTOU는 그대로(동일 사용자 한정, R20에서 타임아웃이 생기면 무해) |
| ~~SEC-7~~ | LOW | system_tray.c:869-875, :801 | 알림 제목·본문을 이스케이프 없이 `notify-send`에 넘김. 알림 데몬 다수는 본문의 `<b>`·`<a href>` 같은 마크업을 해석하므로, 메타데이터에 마크업을 넣으면 알림에 링크를 심거나 `&`·`<`가 든 정상 제목이 깨져 보일 수 있음. 로컬 플레이어만 볼 때는 자기 파일의 태그였지만, `--allow-browsers`(3a6b2f4)에서는 **웹 페이지가 이 문자열을 정함**. 수정안: `g_markup_escape_text`로 본문 이스케이프 (제목은 표준상 마크업 미해석) | ✅ 완료 (b718934) — 알림 서버의 `body-markup` 여부를 한 번 묻고, 해석하는 서버면 앨범·아티스트·플레이어를 이스케이프(물을 수 없으면 이스케이프). SwayNotificationCenter에서 조회 동작 확인. `&`·`<`가 든 본문의 화면 표시는 미확인 |
| ~~R24~~ | LOW | system_tray.c:338-339 | `load_image_from_url`이 `file://` artUrl을 퍼센트 디코딩하지 않고 `realpath`에 넘김. 경로에 공백·비ASCII가 있으면(`My%20Album/cover.jpg`) 아트가 조용히 누락. `resolve_local_media_path`는 디코딩함 (불일치). SEC-2 때부터 있던 동작 | ✅ 완료 (b718934) — 디코딩한 경로를 먼저, 받은 그대로를 다음으로 시도. `file://` artUrl을 주는 플레이어가 없어 실행 확인은 못 함 |
| **R25** | LOW | claude_translator.c:71, :187-193 | `max_tokens: 1024` 고정. 사고(thinking)가 항상 켜지는 최신 모델은 사고 토큰도 이 한도에 포함되어 `stop_reason=max_tokens`로 버려질 수 있고, 줄마다 재시도 예산을 소모. 예시 기본값 `claude-sonnet-4-5`는 영향 없음. `output_config.format` 요청 형태, 텍스트 블록 결합, `refusal`/`max_tokens` 처리 자체는 맞음 | 가능성 (미실측) |
| **R26** (절반) | LOW | translator_common.c:560 / 각 translator의 `static CURL *curl_handle`; :673-685 | (a) `translator_init_curl_handle`로 만든 핸들은 요청에 쓰이지 않고 "초기화됨" 플래그로만 쓰임(요청은 :1036의 로컬 핸들) — 죽은 자원. (b) 반복 줄 재사용은 성공한 번역만 대상이라, "같은 언어"로 거부된 후렴은 등장 횟수만큼 다시 요청됨 | (a) ✅ 완료 (a19df73) — 핸들과 관련 함수 제거. (b) **하지 않음** — 같은 줄끼리 재시도 횟수를 공유해야 하는데, 일시적 네트워크 오류 한 번에 후렴 전체가 예산 소진으로 영구 제외될 수 있어 득보다 실이 큼 |

**클린 판정 (2026-11-22)**
- **worker ↔ 메인 공유 상태**: fetch/art 결과는 worker 전용 버퍼에 쓰고 `ready` atomic 이후 `pthread_join`한 뒤에만 읽음. 트랙 변경은 `begin_load`가 두 worker를 취소·join한 뒤 진행하므로 이전 트랙 결과가 새 트랙에 설치되는 경쟁 없음. 종료 시 `cleanup_resources`가 둘 다 join. 취소 플래그 전역 포인터는 `state` 내부(정적 수명)를 가리켜 dangling 없음. worker는 AppIndicator/GTK 위젯·Wayland를 호출하지 않음(gdk-pixbuf/cairo 메모리 연산만). `sanitize_path`는 `_Thread_local` 버퍼. config 핫리로드는 fetch 중 건너뜀(단, 그 가드가 R21의 S912). 번역 스레드가 리로드 중 읽는 `g_config` 값은 int/enum뿐이라 해제된 문자열 접근은 없음.
- **캐시 JSON 타입 검사** (199f582): `translations` 배열·`retry_counts` 객체 타입 확인 후 접근. 원소가 문자열이 아니어도 `json_object_get_string`이 안전하게 처리.
- **반복 줄 재사용** (cd9192a): 쓰기는 번역 스레드만, `strdup` 후 atomic store. 빈 문자열·공백 줄 경계 정상. 시간 추정이 "요청 수" 기준으로 바뀐 것도 일관됨.
- **가사 경로**: artist 폴더 `""`/`.`/`..`/`/` 포함 거부(f55258c). URL 파일명은 디코딩 **후** 마지막 `/`로 자르므로 `%2F`로 디렉터리를 탈출할 수 없음. 모든 검색 디렉터리에서 파일명을 찾도록 바뀐 것(9932b4b)은 읽기 전용 조회 범위만 넓힘.
- **wayland flush / 투명 버퍼 / shm 0크기 가드 / signal handler**: 이상 없음 (S134 중첩 스멜만 R21).
- **ffmpeg 임시 파일**: `$XDG_RUNTIME_DIR/wshowlyrics`(0700) 내 고정 이름, 추출 전후 `unlink`, 결과는 매직바이트 allowlist 디코드를 다시 거침. spawn 실패·비정상 종료·빈 출력 모두 처리됨.

### 요약 — 미완료 마이그레이션 / 정리 항목

| ID | 위치 | 패턴 | 우선순위 / 상태 |
|---|---|---|---|
| ~~R1~~ | main.h `lyrics_state` Wayland 필드 7개 | `wl_conn` 도입 후 옛 필드 미제거 (e0020f0) | ✅ 완료 (9007eec, 42→35 필드) |
| ~~R2~~ | main.h `lyrics_state` 35필드 | S1820 — 5개 sub-struct 분리 (style/surface/playback/config/runtime) | ✅ 완료 (6b55f10, 35→6 필드). B(시그니처 분해)는 후속 |
| ~~R3~~ | itunes_artwork.c, lrclib_provider.c | thin wrapper 정리 | ✅ 완료 (c15cdb9) |
| ~~R4~~ | config.c, lyrics_provider.c | `config_trim_whitespace` 정리 | ✅ 완료 (c15cdb9) |
| **R5** | config.c `[deepl]` section | Deprecated 섹션 — 제거 데드라인 결정 | ⏸ 보류 (정책) — 11-22 확인: config.c:211-216, 943/958, 변동 없음 |
| ~~R6~~ | mpris.c Seeked signal | 빈 콜백 정리 | ✅ 완료 (c15cdb9) |
| ~~R7~~ | lyrics_provider.c dispatcher | 4-branch if/else → provider 테이블 | ✅ 완료 (1d3800a) — 이슈 #2 vtable 설계. `struct translator_provider` 레지스트리로 dispatch + main.c init/cleanup 3곳 통합. Ollama = 새 모듈 + 배열 한 줄 |
| **R8** | parser_utils.c:480 `find_word_start` | 152줄 함수였음 → 11-22 재측정 **13줄** (이번 구간의 루비 파서 변경에서 `count_morae`·`find_kanji_boundary` 등 헬퍼로 분해됨) | ⏸ 보류 유지 — 원래 사유(길이)는 사실상 소멸. 대신 `find_kanji_boundary`(440-475)에 S134 중첩 스멜이 생김 → R21에서 처리 여부 결정 |
| **R9** | mpris.c:945, wayland_init.c:14 | 100+줄 함수 (11-22 재측정: `setup_player_subscription` 111줄, `wayland_init_surface` 115줄). `lyrics_manager_load_lyrics`는 async 분리(335f000)로 11줄이 되어 목록에서 제외 | ⏸ 보류 (강제 아님) |
| ~~R10~~ | config.c, deepl_translator.c | Magic number 버퍼 | ✅ 완료 (c15cdb9) |
| ~~R11~~ | config.c | `XDG_CONFIG_HOME` 빈 문자열 가드 + 잠재 NULL deref | ✅ 완료 (c15cdb9) |
| ~~R12-A~~ | 22 파일 | clang-tidy `misc-include-cleaner` 기준 unused #include 제거 (37 줄 순감) | ✅ 완료 (43a6366) |
| **R12-B** | main.h | main.h 슬림화 (~30 .c가 stdint.h 등 직접 include 필요) | ⏸ 보류 (B 옵션 — 가독성 트레이드오프) |
| ~~R13~~ | wayland_events.c:261, config.c:607/817/954, main.c:276 | SonarCloud 신규 5건 (S1121 ×4 체인 할당, S995 ×1 const 누락) | ✅ 완료 (6fcfc72) — 체인 할당 문장 분리 + const 파라미터 |
| ~~R14~~ | system_tray.c | 아이콘 폴백 3-tier에서 `set_indicator_icon_cached(); return true;` 3중 복붙 | ✅ 완료 (bb8b0cc) — 단락평가 `||` 하나로 통합 |
| ~~R15~~ | translator_common.c | `max_retries` 조회 + 기본값 `3` 중복 | ✅ 완료 (bb8b0cc) — `translator_get_max_retries()` 추출 |
| ~~R16~~ | main.c ×2, file_utils.c/.h | 캐시 경로 prefix 검사 중복 + 빈 cache_base 시 오판 | ✅ 완료 (bb8b0cc) — `path_is_in_cache_dir()` 추출. 참고: `get_cache_base_dir()`은 static char[]라 NULL이 아닌 빈 문자열 반환 → `strncmp(...,0)` 항상매치로 로컬 핫리로드 막히던 버그를 가드로 해소 |
| ~~R17~~ | main.c:309 | `char tr_path[768]` — R10에서 정리한 magic number 패턴 회귀 | ✅ 완료 (6fcfc72) — `PATH_BUFFER_SIZE` |

> **Ollama translator 구현**은 코드 품질 항목이 아니라 번역 기능 작업이므로 `.claude/TODO_TRANSLATION_FEATURES.md`로 이관됨 (R7 vtable이 통합 표면을 제공).

**TINY 묶음 (R3+R4+R6+R10+R11)**: ✅ 완료 (c15cdb9, 2026-05-02)
**TINY 묶음 2 (R13+R17)**: ✅ 완료 (6fcfc72, 2026-07-06) — SonarCloud 5건 전부 해소 + magic number

---

### 클린 판정 (2026-07-06 감사)

- **`wshowlyrics-offset`** (gdbus→busctl/dbus-send 폴백 +100줄): 인젝션 없음. 정수 정규식 검증, exact-match case, `busctl --`, 인용 처리 모두 적절.
- **`dbus_control.c`**: introspection XML로 타입 강제 → SEC-1 류 타입 혼동 불가. offset ±5000 클램프.
- **`translator_common.c` 캐시** (b891690 변경 포함): MAX_CACHE_FILE_SIZE 5MB 로드 전 검증, snprintf 절단 검사, umask 0077, json-c 인덱스 바운드 체크. 이상 없음.
- **`lock_file.c` / `runtime_dir.c`**: 0600/0700 권한, stale-lock fd 유지로 TOCTOU 회피. 클린.
- **`lrclib_provider.c`**: TLS 1.2 강제, json-c 파싱. `find_best_match_in_results`의 `strchr('{','}')` 오브젝트 분할은 가사 내 `}` 시 매칭 오류 가능 — 보안 아닌 정확성 이슈로 기록만.
- **`rendering_manager.c` HiDPI 스케일링** (d76a4c2): scale은 컴포지터 제공(1-3), 오버플로 비현실적. 클린.
- **파서들**: 이번 구간 diff 없음 + fuzz 커버리지 유지로 심층 재분석 생략.

---

### R2. 본 작업: S1820 — `lyrics_state` 구조체 분리

> ✅ **완료 A 단계 (2026-05-03, 6b55f10)**. lyrics_state 35→6 필드.
> 5개 sub-struct: style / surface_state / playback_state / config_state / runtime.
> 배치: playback → lyrics_types.h, config → config.h, 나머지는 main.h.
> ⏸ **B 단계 (시그니처 분해)는 후속 PR**. 자연스러운 함수만 점진적으로 sub-struct만 받도록 변경.
> 검증: meson build clean, Sway/Hyprland 런타임 (가사/D-Bus/pause-resume/instrumental break/TTY 전환).

**위치**: `src/main.h:53` `struct lyrics_state` 42필드 (R1 후 35필드) → 권장 20필드

**파급 범위 (실측 2026-05-02)**:
- `state->` 총 참조: **304건**
- 영향 파일: **8개**
- `lyrics_state *` 받는 함수 시그니처: **57개** (헤더 16, 구현 41)
- `&state->X` 포인터 전달: 27건

**필드 사용 빈도 상위**:
```
45  state->lyrics              29  state->current_line
22  state->surface             22  state->current_track
20  state->timing_offset_ms    14  state->overlay_enabled
13  state->width               12  state->layer_surface
12  state->height              10  state->prev_line / next_line
```

**자연스러운 분리안**:
```c
struct lyrics_state {
    struct wayland_connection *wl_conn;   // R1에서 통합 정리됨
    struct surface_state   surf;          // width/height/buffers/anchor/margin/layer  (10)
    struct appearance      look;          // foreground/background/font                (3)
    struct playback_state  pb;            // lyrics/current_track/lines/offset         (11)
    struct config_state    cfg;           // config_file_path/md5                      (2)
    struct runtime_flags   flags;         // run/reconnect/overlay_enabled/quirks      (5)
};
```
→ `lyrics_state` 자체 **6필드** (S1820 해소)

**작업량 추정**:
| 접근 | 시간 | diff |
|---|:---:|---|
| 단순 sed 치환만 (시그니처 `lyrics_state*` 유지) | 1–2h | +400 / −400 |
| 시그니처 분해 (작은 함수는 sub-struct만 받음) | 4–8h | +600 / −500 |

**검증 필수**:
- meson 빌드 (werror=true)
- fuzz 타겟 (lrc/lrcx/srt) 재실행
- 실제 음악 재생 + Wayland 재연결 (TTY 전환) 수동 테스트

**우선순위**: LOW — code smell이고 Quality Gate에 영향 없음. 단일 컨텍스트 구조체 패턴 자체는 유효하므로 Won't Fix도 합리적 선택지.

---

### R5. `[deepl]` deprecated config section 제거 데드라인 결정

> **결정 (2026-05-03)**: 보류. 별도 릴리스 정책 결정 사이클에서 다룸.

**위치**: `src/user_experience/config/config.c:211-216` (`parse_deprecated_deepl_section`), 943/958 (warning suppression) — 2026-11-22 기준

**상태**: 도입 `aedbfac` (2025-12-19, ~5개월 경과). 사용자에게 `⚠️ [deepl] section is deprecated` 경고 출력 후 호환 처리.

**작업**:
1. 다음 릴리스 노트에 "Removal scheduled in v0.X.0" 명시
2. 메이저 릴리스에서 코드 + warning suppression 제거
3. `[translation]` section만 인식하도록 단순화

**우선순위**: LOW — 사용자 영향. 한 메이저 릴리스 사이클 grace period 권장.

---

### R8. `find_word_start` 152줄 분리

> **결정 (2026-05-03)**: 보류. 강제 아님 (정적 분석 통과), 가독성 개선만 목적.
> 다음에 해당 함수를 손볼 일 생기면 그때 분리 검토.

**위치**: `src/parser/utils/parser_utils.c:480` (2026-11-22 재측정: 13줄 — 헬퍼로 분해되어 원래 사유 소멸)

**상태**: UTF-8 word boundary 탐색 로직. SonarCloud cognitive complexity 통과 (이미 Phase 9에서 정리된 패턴이라 추정). 기능은 정확하나 사람이 읽기 부담.

**작업안**:
- UTF-8 backwards iter 부분 분리 (이미 `move_back_one_utf8_char` 헬퍼 존재)
- space-boundary 분기 / kanji-boundary 분기를 별도 정적 함수로
- bounds check 부분은 그대로 유지 (NOSONAR 주석 보존)

**우선순위**: LOW — 동작 정확성 OK, 가독성 개선.

---

### R9. 100+줄 함수 분리

> **결정 (2026-05-03)**: 보류. R8과 같은 사유 (강제 아님, ROI 작음).

**위치 / 길이** (2026-11-22 재측정):
- `src/utils/mpris/mpris.c:945` `setup_player_subscription` — 111줄
- `src/utils/wayland/wayland_init.c:14` `wayland_init_surface` — 115줄
- ~~`lyrics_manager_load_lyrics`~~ — async 분리(335f000)로 11줄, 제외

**작업안**: 각 함수의 단계별 블록을 헬퍼로 추출. 외부 시그니처 유지.

**우선순위**: LOW — 리팩토링 ROI 작음. 현재 SonarCloud 룰 통과 중.

---

### R12. Unused includes 정리

> **A 단계 ✅ 완료 (2026-05-03, 43a6366)**: 22 파일에서 unused #include 37줄 순감.
> - 도구: `clang-tidy --checks="-*,misc-include-cleaner"` (compile_commands.json 활용)
> - False positive 5건 복구 (json-c/json.h ×4, shm.h forward dec 추가, stdio.h ×1)
> - 검증: meson build clean, Sway/Hyprland 런타임 (가사/D-Bus/pause-resume/instrumental/TTY)
>
> **B 단계 ⏸ 보류**: main.h 자체 슬림화. clang-tidy 분석 시 528건 missing include 발견되어 거대 변경 필요. 가독성 트레이드오프 (각 .c 파일이 stdint.h, string.h 등 다 직접 include) — 사용자 결정으로 보류.

---

## 권장 작업 순서

1. ~~SonarCloud 웹 마킹 작업~~ ✅ 완료 (2026-05-02)
2. ~~HTTP S5332 검토~~ ✅ 완료 (SAFE + acknowledgement)
3. ~~**TINY 묶음 PR** (R3 + R4 + R6 + R10 + R11, zero-risk)~~ ✅ 완료 (c15cdb9, 2026-05-02)
4. ~~**R1**: Wayland 필드 → `wl_conn` 통합~~ ✅ 완료 (9007eec, 2026-05-03)
5. ~~**R2-A**: S1820 `lyrics_state` 분리 (sub-struct)~~ ✅ 완료 (6b55f10, 2026-05-03)
6. ~~**R12-A**: misc-include-cleaner 기반 unused 제거~~ ✅ 완료 (43a6366, 2026-05-03)
7. ~~**SEC-1**: MPRIS GVariant 타입 가드~~ ✅ 완료 (38d1a85, 2026-07-06)
8. ~~**TINY 묶음 2** (R13 + R17): SonarCloud 5건 + magic number~~ ✅ 완료 (6fcfc72, 2026-07-06)
9. ~~**SEC-2**: artUrl 포맷검사 + `file://` 하드닝~~ ✅ 완료 (89530c9, 2026-07-06) — IP 차단은 의도적 제외
10. ~~**Small 정리** (R14 + R15 + R16): 중복 제거 + 가드~~ ✅ 완료 (bb8b0cc, 2026-07-06)
11. ~~**SEC-3 / SEC-4**: 하드닝 (경계 검사, `%00` 거부)~~ ✅ 완료 (f992c86, 2026-07-06)
12. ~~**R7**: Translator vtable 레지스트리~~ ✅ 완료 (1d3800a, 2026-07-06) — 이슈 #2. **Ollama 통합 인프라만 완료** (실제 `ollama_translator.c` 구현은 `TODO_TRANSLATION_FEATURES.md`로 이관)
13. ~~R2-B / R12-B~~ ⏸ 보류 (둘 다 가치 트레이드오프로 deferred)
14. ~~R5/R8/R9~~ ⏸ 보류

### 2026-11-22 감사분 권장 순서 (전부 승인 대기)

15. ~~**CI-1**: `coverity-scan.yml` 패키지명 → `libgdk-pixbuf-2.0-dev`~~ ✅ 완료 (424c2cf, 2026-11-23)
16. ~~**R21**~~ ✅ 완료 (e4a990a, 1c8afd8, e9eef27, 2026-11-24)
17. ~~**R18 + R19**~~ ✅ 완료 (8fa788e, 2026-11-24, 이슈 #53). 후속으로 `pthread_cancel` 폴백도 제거 (949a700, 이슈 #56).
18. ~~**R22**~~ ✅ 완료 (0383670, 2026-11-24, 이슈 #54)
19. ~~**R20**~~ ✅ (a)(b) 80669d4 (#61), (c) e2bccaf + 69a7eb1 (#64), 2026-11-25.
20. ~~**R24 / SEC-6 / SEC-7**~~ ✅ 완료 (b718934, 2026-11-24)
21. ~~**R23**~~ ✅ 완료 (0396394, 2026-11-24)
22. ~~**R27**~~ ✅ 완료 (1d831b0 + 5b852ec, 2026-11-25). **R25**만 남음 — 실제 응답의 사용 토큰을 재 본 뒤 한도를 정할 것. R26은 (a)만 완료 (a19df73).

수정안 자체 검토 (지난번처럼 수정이 새 이슈를 만들지 않도록):
- 16의 분리는 `&&` 우변 부작용을 없애는 형태여야 함(조건식 안에 `++` 금지). 현재 동작(fetch 중 카운터 정지)과 달라져도 무해.
- 17의 setopt는 기존 `||` 체인에 추가 — 이 패턴은 현재 Sonar에 걸리지 않음.
- 21은 `stat`→`fopen` 순서를 만들지 말 것(S5847). `open` 한 번으로 끝낼 것. world-writable 경로를 새로 허용하지 않음(S5443).
- 어떤 수정안도 config.c의 `"/etc/"`·`"/usr/share/"`·`"/etc/wshowlyrics/settings.ini"` 리터럴을 건드리지 않음 (NUR `--replace-fail`).
- SEC-2의 IP 차단은 재제안하지 않음. 보류 항목(R2-B, R5, R8, R9, R12-B)을 다시 열 근거는 발견되지 않음.

**2026-07-06 세션 완료**: SEC-1~4 + R13/R14/R15/R16/R17 + R7 전부 처리. origin/master에 push 완료. 당시 감사분은 모두 종료. 이후 유입분은 위 2026-11-22 목록 참조. (Ollama 등 번역 기능 작업은 `TODO_TRANSLATION_FEATURES.md` 참조.)

---

## 참고 자료

- **SonarCloud Dashboard**: https://sonarcloud.io/project/overview?id=wshowlyrics_wshowlyrics
- **SonarCloud Issues API**: https://sonarcloud.io/api/issues/search?componentKeys=wshowlyrics_wshowlyrics
- **SonarCloud Hotspots API**: https://sonarcloud.io/api/hotspots/search?projectKey=wshowlyrics_wshowlyrics
- **Coverity Dashboard**: https://scan.coverity.com/projects/wshowlyrics
- **마킹 정책**: `CLAUDE.md` → "Marking Issues / Hotspots (Write API)" 섹션

---

## 상태

- **최종 업데이트**: 2026-11-27 (v0.12.0 릴리스, 태그 파이프라인 920 성공)
- **현재 단계**: 2026-11-25~27에 열린 이슈 #41·#59·#60·#63·#62(Task #65~#69)·#71·#22를 닫음. 그 뒤 이슈 없이 한 일: 응답 크기 상한과 GET 설정 통합(fa52690), 핸들 생성 통합(164c044). SonarCloud @164c044는 `c:S4423` 1건(curl_utils.c, 키 `AaIca5Tx5B_5k76XjBKg`)을 사용자가 오탐 표시해 **0건·게이트 OK**(2026-11-27 재조회). Coverity는 fa52690에서 0건(사용자 확인), 164c044는 제출만 됨(run 45988754705) — 대시보드 확인 필요. 번역 경로는 164c044 빌드로 실행 확인(`[1/56] Translated`). 시험 잔여물 정리함(sshfs 재현 마운트 해제, scratchpad 시험 파일 삭제, `build-mock` 삭제; 실제 캐시는 이미 비워져 있었음). `scratchpad/i22`에 모의 서버(`mock.py`)·시험 스크립트 넷만 남김 — tmpfs라 재부팅 시 사라짐, 저장소에 둘지는 미정.
- **v0.12.0 릴리스됨(2026-11-27)**: 태그 `v0.12.0` = 592d168, 파이프라인 920 성공. 남은 확인은 Coverity @164c044 결과뿐(릴리스 커밋과는 버전 문자열 한 줄 차이).
- **완료**: Phase 1-14 + 2026-05-02 SonarCloud 마킹 + TINY 묶음 (c15cdb9) + R1 (9007eec) + R2-A (6b55f10) + R12-A (43a6366)
  - **2026-07-06 세션**: SEC-1 (38d1a85) · R13+R17 (6fcfc72) · SEC-2 (89530c9) · R14~R16 (bb8b0cc) · SEC-3/4 (f992c86) · R7 (1d3800a)
  - **이후**: SonarCloud S912/S5443/S5847 (b7f50a8) · NUR 리터럴 복원 (afa4a6a) · fuzz url_decode (8fdf3a3) · Coverity CID 1697552 (1fc4295) · CI-1 Coverity 워크플로 복구 (424c2cf) · SEC-5 notify-send `--` (af72173, #49) · R21 BLOCKER/HIGH (e4a990a, 1c8afd8) · R18+R19 번역 취소·타임아웃 (8fa788e, #53) · R22 플레이어 소멸 시 fetch 취소 (0383670, #54) · pthread_cancel 폴백 제거 (949a700, #56) · R21 15건 (e9eef27) · SEC-7·R24·SEC-6 (b718934) · R23 (0396394) · R26(a) (a19df73) · 썸네일 20% 지점 (4f2c558, #57) · R20 (a)(b) 아트 조회 worker화 (80669d4, #61) · R27 확장자 (1d831b0, #55 / 5b852ec) · R20 (c) 재검색·재로드 worker화 (e2bccaf, 69a7eb1, #64)
- **남은 이슈**:
  - 활성: LOW — R25. 2026-11-25~27 작업에서 알고 남긴 것(각 날짜 항목 참고): 번역 스레드가 설정 스칼라 셋을 직접 읽음, 조회가 막혀 있는 동안 settings.ini 저장 미반영, SIGKILL 뒤의 블로킹 `waitpid`, 실제 CIFS 미측정, 캐시 `[length:]`의 오차 겹침(최대 10 s), 재시도 포기 뒤 다음 트랙에서 바로 재요청(#10), 번역기의 응답 버퍼·콜백이 공용 것의 복사본, 리다이렉트 프로토콜 제한 없음
  - ⏸ 보류 유지: R2-B (시그니처 분해), R12-B (main.h 슬림화), R5 (릴리스 정책), R8/R9 (강제 아님)
  - 번역 기능 작업(Ollama translator 등)은 `TODO_TRANSLATION_FEATURES.md`
- **목표**: A등급 유지, Quality Gate GREEN 유지, BLOCKER/CRITICAL 0개, SonarCloud 이슈 0건 (164c044 재분석 + 오탐 표시 1건으로 확인, 2026-11-27)
- **참고**: Coverity CID 643610 (b3a410a), CID 1697552 (1fc4295) 수정 완료. SEC-A/SEC-B 검증 완료 — 모두 안전. SEC-2의 IP 차단은 의도적 제외(재제안 금지). config.c의 `"/etc/"`·`"/usr/share/"` 리터럴은 NUR 빌드가 의존
