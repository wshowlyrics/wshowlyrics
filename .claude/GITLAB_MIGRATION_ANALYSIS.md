# GitLab을 메인 리포지토리로 사용할 때 장단점 분석

> 분석일: 2026-03-01
> 현재 구조: GitHub (주) → GitLab (미러)
> 검토 대상: GitLab (주) → GitHub (미러) 전환

## 장점

### CI/CD
- `.gitlab-ci.yml` 단일 파일로 모든 파이프라인 관리 (현재 GitHub Actions 10개+ 워크플로우 분산)
- GitLab → GitHub 미러링이 GitLab 내장 기능 (워크플로우 불필요, 설정만으로 가능)

### 빌트인 기능
- Container Registry, Package Registry 기본 내장
- GitLab Pages 기본 제공
- 빌트인 SAST/DAST 스캐너 (별도 서비스 연동 없이 보안 분석 가능)

### 운영
- self-hosted GitLab으로 이전이 용이 (향후 인프라 자율성 확보)
- 프로젝트 단위 세밀한 권한 관리

## 단점

### CI/CD 무료 한도
- GitLab 무료: 400분/월 vs GitHub 무료: 2,000분/월
- 빌드 빈도가 높으면 GitLab 무료 한도가 부족할 수 있음

### 마이그레이션 비용 (이 프로젝트 기준)

현재 GitHub 생태계에 깊이 묶인 워크플로우 전면 재작성 필요:

| 워크플로우 | 역할 |
|---|---|
| `ci.yml` | 빌드/테스트 |
| `build-packages.yml` | deb/rpm/AppImage 빌드 |
| `coverity-scan.yml` | 주간 Coverity 스캔 |
| `aur-publish.yml` | AUR unstable 배포 |
| `aur-release.yml` | AUR stable 배포 |
| `nur-publish.yml` | NUR nightly 배포 |
| `nur-release.yml` | NUR release 배포 |
| `copr-publish.yml` | COPR nightly 배포 |
| `copr-release.yml` | COPR release 배포 |
| `ppa-release.yml` | PPA release 배포 |
| `gitlab-mirror.yml` | GitLab 미러 (이건 불필요해짐) |

### 외부 서비스 재설정

| 서비스 | 영향 |
|---|---|
| **SonarCloud** | GitHub 네이티브 연동 → GitLab 연동 재설정, PR decoration 재구성 |
| **Coverity Scan** | GitHub Actions 기반 → GitLab CI로 재작성 |
| **GitHub Security tab** | Semgrep SARIF 업로드 불가 → GitLab 자체 SAST로 대체 |
| **GitHub Releases** | 빌드 아티팩트 자동 배포 → GitLab Release로 재구성 또는 GitHub API 호출 |
| **GitHub Pages** | gh-pages 브랜치 운영 중 → GitLab Pages로 이전 |
| **Gitleaks** | GitHub Actions 후처리 → GitLab CI로 재작성 |

### 코드베이스 내 참조 변경

약 30곳의 GitHub URL 참조 업데이트 필요:
- `README.md`, `docs/README.ko.md`: 배지, 릴리즈 링크
- `src/main.c`: help URL, 문서 URL
- `wshowlyrics.service`: Documentation URL
- `CLAUDE.md`: SonarCloud/Coverity 대시보드 링크
- 각 워크플로우 내 Homepage, Vcs-Git, Source URL

### 커뮤니티/가시성
- GitHub이 오픈소스 프로젝트 발견에 압도적으로 유리
- GitLab은 상대적으로 검색 노출이 낮음
- GitHub Issues 이력 마이그레이션 필요
- GitHub Stars/Watch 등 소셜 지표 초기화

## 결론

이 프로젝트는 CI/CD 파이프라인(10개+ 워크플로우), 외부 서비스(SonarCloud, Coverity, Semgrep), 패키지 배포(AUR, NUR, COPR, PPA)가 모두 GitHub 생태계에 깊이 통합되어 있음. **전환 비용 대비 실질적 기술 이점이 크지 않아** 현재 구조(GitHub 주 + GitLab 미러) 유지가 합리적.

### 전환이 유의미해지는 시점
- self-hosted GitLab 운영 시 (완전한 인프라 제어 필요 시)
- GitHub 정책 변경 등으로 무료 플랜 제약이 생길 때
- 팀 규모 확대로 GitLab의 프로젝트 관리 기능이 필요할 때
