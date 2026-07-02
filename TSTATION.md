# Tstation (JupyterLab 개발 가이드)

이 문서는 `JupyterLab`을 `Tstation`에서 사용하기 위해 작업한 내용을 공유하기 위해 작성하였습니다.

<br />


## 1. 커스텀 패키지 개요

커스터마이징은 core를 직접 수정하는 대신, 별도 확장(extension) 패키지로 분리해 작업했습니다. (꼭 core를 수정해야하는 경우, core를 수정한 경우도 있습니다. core 수정 된 내용과 수정 시 반영 방법은 #5, #6을 참고해주세요.)

| 패키지 | 위치 | 역할 |
| --- | --- | --- |
| `@jupyterlab/custom-ui-extension` | `packages/custom-ui-extension` | UI/스타일 커스터마이징을 한곳에 모은 확장 |
| `@jupyterlab/custom-api-extension` | `packages/custom-api-extension` | 외부 서버와의 통신(제출/조회 등)을 담당하는 중앙 API 클라이언트 |

> 의존 방향: `custom-ui-extension` → `custom-api-extension` (UI에서 API 클라이언트 토큰 `ICustomApi`를 사용).

<br />

## 2. custom-api-extension

**목적**: 외부 서버와의 모든 통신을 관리. 각 UI 코드가 개별적으로 `fetch`를 호출하지 않고, 이 확장이 제공하는 중앙 클라이언트(`ICustomApi` 토큰)를 주입받아 사용.

**제공 플러그인**
| 플러그인 id | autoStart | 설명 |
| --- | --- | --- |
| `:api` | ✅ | 중앙 API 클라이언트(`ICustomApi`)를 provide. 다른 확장은 이 토큰을 `requires`로 주입받아 사용. |
| `:file-open-reporter` | ❌ | 파일이 열릴 때 파일명을 외부 서버로 보고(현재 기본 비활성). |

> `ICustomApi`는 앱 전역에서 단 하나만 존재해야 하므로 **singleton 패키지**로 등록되어 있음.

<br />

## 3. custom-ui-extension

**목적**: Tstation 용도에 맞춘 UI/스타일/브랜딩 커스터마이징을 하나의 확장으로 모으기 위해 생성한 패키지. core나 개별 extension을 직접 고치는 대신 여기서 override.

**주요 구성 (`src/index.ts`에서 등록하는 플러그인)**
| 플러그인 id | 설명 |
| --- | --- |
| `:style` | 중앙 커스텀 스타일 적용 |
| `:splash` | 로딩 UI 변경 |
| `:sidebar` | 좌측 사이드바 패널 커스터마이징 (Extension 비활성화 / 사이드 바 목록 추가) |
| `:toolbar` | 제출 API로 전송하는 툴바 버튼 커스터마이징 |
| `:icons` | 내장 아이콘 SVG를 커스텀 아이콘으로 교체 (Lucid 아이콘 사용 중) |
| `:launcher` | 런처(Launcher) 커스터마이징 |
| `:menu` | 상단 메뉴 커스터마이징 |
| `:placeholder` | 빈 노트북 코드 셀에 안내 placeholder 표시 |
| `:about` | 상단 메뉴 `About Tstation`의 모달을 커스터마이징 |

**스타일 구성**
- `style/components/*.css` — 컴포넌트별 커스텀 CSS(`sidebar`, `panel`, `launcher`, `loading`, `toolbar` 등)를 `src/index.ts` 상단에서 import
- `style/icons/*.svg` — 교체용 아이콘 SVG

<br />

## 4. 새 커스텀 패키지를 추가할 때 (체크리스트)

- 이 저장소는 Lerna + Yarn workspaces 모노레포
- `dev_mode/package.json`, `jupyterlab/staging/package.json`은 대부분 **integrity 스크립트가 생성/동기화**
- 단, 아래 파일들은 신규 커스텀 패키지를 추가할 때 확인/수정 대상입니다.

### 4-1. 패키지 자체 생성
1. `packages/<이름>-extension/` 디렉터리 생성.
2. `package.json` — 기존 커스텀 패키지를 복사해 다음을 맞춤:
   - `"name": "@jupyterlab/<이름>-extension"`, `version`은 다른 패키지와 동일하게 맞춰 사용했음 / 별도 버전 지정해서 맞추기만 해도 사용가능할 걸로 보임
   - `"jupyterlab": { "extension": true }` (필수 — 이게 있어야 labextension으로 인식)
   - 토큰을 provide 한다면 `styleModule`/`sideEffects` 등도 기존 패키지 참고.
3. `tsconfig.json` — 기존 패키지 것을 복사하고 `references`에 의존 패키지 추가.
4. `src/index.ts` — `export default [ ...plugins ]` 형태로 플러그인 배열 export.

### 4-2. 빌드에 편입시키기 위해 수정할 파일
| 파일 | 수정 내용 |
| --- | --- |
| `packages/metapackage/package.json` | `dependencies`에 새 패키지 추가 (**소스 오브 트루스**) |
| `packages/metapackage/tsconfig.json` | `references`에 `{ "path": "../<이름>-extension" }` 추가 |
| `packages/metapackage/tsconfig.test.json` | 위와 동일하게 `references` 추가 |
| `dev_mode/package.json` | `resolutions`, `dependencies`, `jupyterlab.extensions`, `jupyterlab.linkedPackages`, (토큰 provide 시) `jupyterlab.singletonPackages`에 추가 |
| `jupyterlab/staging/package.json` | `dev_mode`와 동일한 항목 반영 |

> `dev_mode/package.json`과 `staging/package.json`은 직접 손대기보다 metapackage 및 각 패키지 `package.json`을 먼저 맞춘 뒤 아래 명령으로 재생성하는 것을 권장합니다.

### 4-3. 반영 명령
```bash
jlpm install          # 워크스페이스/심볼릭 링크 갱신
jlpm run integrity    # dev_mode·staging package.json 동기화 + 무결성 검사
jlpm run build        # 전체 빌드
```

<br />

## 5. 기존(core/upstream) 패키지를 수정하고 배포에 반영하기

새 패키지가 아니라 **이미 존재하는 패키지**(`packages/<pkg>/src/...`)를 직접 고쳤을 때의 흐름입니다. 소스만 고친다고 배포 앱에 반영되지 않으며, 아래 단계를 거쳐야 합니다.

### 5-1. 두 가지 빌드 산출물 구분
| 산출물 | 위치 | 용도 | git |
| --- | --- | --- | --- |
| dev_mode | `dev_mode/` | **개발용**. `jlpm watch`로 실시간 반영해 확인. | 산출물은 미커밋 |
| static | `jupyterlab/static/` | **배포용**. Python `jupyterlab` 패키지가 실제로 서빙하는 번들. | **gitignore(빌드 산출물)** |

> `jupyterlab/static`은 커밋 대상이 아니라 배포 시점에 빌드로 생성/갱신합니다. 반대로 `jupyterlab/staging/`의 `package.json`, `webpack.config.js`, `yarn.lock` 등은 커밋됩니다.

### 5-2. staging에서 확인/수정할 파일
대부분 `jlpm run integrity`가 `packages/metapackage/package.json`을 기준으로 `jupyterlab/staging/package.json`을 동기화하므로 직접 편집은 지양합니다. 다만 아래는 인지하고 있어야 합니다.

| 파일 | 내용 |
| --- | --- |
| `jupyterlab/staging/package.json` | 배포 앱에 포함되는 패키지 목록/버전, `jupyterlab.singletonPackages` 등. **integrity가 metapackage 기준으로 동기화** (직접 수정하지 말고 metapackage를 고칠 것). |
| `jupyterlab/staging/yarn.lock` | `build:core` 실행 시 갱신될 수 있음. 변경분은 커밋 필요. |

> core 패키지를 직접 수정한 경우, jupyterlab/staging/package.json, jupyterlab/staging/yarn.lock 쪽에서 로컬 경로를 지정해주지 않으면 배포 환경에서는 수정한 패키지가 아닌, npm으로 지정된 버전 패키지를 가져옴 (패키지 당 최초 1회 등록해야함 / 최초 1회 등록하면 다음부턴 배포시에도 반영됨)

<br />

## 6. Core(JupyterLab 원본 코드) 직접 수정 내역

확장 패키지로 처리할 수 없어 **core를 직접 고친** 부분입니다. upstream 버전을 올릴 때 아래 파일들이 충돌/원복 대상이라 확인이 필요합니다.

| 파일 | 변경 내용 | 이유 |
| --- | --- | --- |
| `jupyterlab/labapp.py` | `LabApp.app_name` `"JupyterLab"` → `"TStation"` | 앱/페이지 타이틀 브랜딩 |
| `packages/terminal/src/widget.ts` | 터미널 배경색을 `--launcher-background-color`(없으면 `--jp-layout-color0`) 우선 사용 | 다크모드 입력영역 배경색 통일 |
| `packages/launcher/src/widget.tsx` | 런처 카드에 `data-command`, `data-launcher-args` 속성 추가 | custom-ui-extension의 launcher 커스터마이징에서 카드 식별/재구성에 사용 |
| `packages/application-extension/schema/move-widget.json` | 탭 컨텍스트 메뉴의 "Move Widget To" 서브메뉴 제거 | 불필요 메뉴 숨김 |
| `packages/apputils-extension/schema/notification.json` | `checkForUpdates` 기본값 `true` → `false`, `fetchNews` 기본값 `"none"` → `"false"` | 업데이트 확인/뉴스 알림(하단 Jupyter 알람) 비활성 |

> **주의**: 위 파일들은 `packages/custom-*`와 달리 upstream(원본 경로)과 같은 경로를 공유하므로, JupyterLab 버전 업 시 merge 충돌이 나거나 변경이 조용히 덮일 수 있습니다. upstream 병합 후 반드시 이 표의 항목들이 유지됐는지 확인하세요.

<br />


## 7. 참고
- 확장 플러그인 패턴: `packages/*-extension/src/index.ts`
- Lumino 패턴(토큰/시그널/disposable): `docs/source/developer/patterns.md`
- 모노레포 구조: `docs/source/developer/repo.md`

## 8. 실행 코드

### 8-1. Docker로 실행 시
- Jupyter 권장 및 기본 Dockerfile은 docker/Dockerfile
- Jupyter 기본 경량 버전은 docker/Dockerfile.runtime
- Tstation 전용 dockerfile은 workspace-provider.Dockerfile
- Tstation 에서 사용할 거기 때문에, workspace-provider.Dockerfile 만 빌드 성공해서 띄울 수 있으면 됨
- 실제 서버에서 사용하는 명령어는 아니고, 프론트에서 테스트용 명령어
```bash
# 빌드 명령어
docker build -f workspace-provider.Dockerfile -t workspace-provider:latest .

# 빌드 후 컨테이너 실행 명령어
docker run --rm -it -p 8888:8888 workspace-provider:latest /provider_venv/bin/python /provider_venv/bin/jupyter-lab --ip=0.0.0.0 --port=8888 --no-browser --allow-root --ServerApp.root_dir=/tmp --IdentityProvider.token=
```

### 8-2. 로컬 개발 서버 실행 시
```bash
# 가상환경 세팅
python3 -m venv .venv
source .venv/bin/activate

pip install -e ".[dev,test]"

jlpm
jlpm build

# 터미널1 (안켜도됨)
jlpm watch
또는
python scripts/watch_dev.py

# 터미널2

# 개발모드로 여는 경우 (--watch 는 코드 감지용 (수정사항 바로 안봐도 되면 빼고 실행))
jupyter lab --dev-mode --watch
# 개발모드 끄고 여는 경우
jupyter lab
# 개발모드 끄고 열 때, 수정사항들이 반영되지 않으면 아래 빌드 명령어 이용
jupyter lab build --splice-source
```
