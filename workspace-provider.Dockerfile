# 1. Stage: Shim Builder
FROM gcc:12 AS shim-builder
WORKDIR /build

# 컨테이너 내부에 /build 작업 폴더를 만들고, 호스트 서버에 있는 cudart_shim.c 소스 코드를 이 폴더 안으로 복사해 옵니다.
COPY shim/cudart_shim.c cudart_shim.c

# cudart_shim.c을 컴파일하여 libcudart_shim.so 생성
RUN gcc -shared -fPIC -O2 \
    -DREAL_LIBCUDART=\"/opt/gpuguard/libcudart_real.so\" \
    -o libcudart_shim.so \
    cudart_shim.c \
    -ldl -lpthread

# 2. Stage: Frontend Builder
# node.js 및 python 설치
FROM node:22 AS frontend-builder
RUN apt-get update && apt-get install -y python3 python3-pip python3-venv

WORKDIR /build
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"
RUN pip install --upgrade pip build "hatchling>=1.21.1" "jupyter-builder>=1.0.0b1" "hatch-jupyter-builder>=0.3.2"

COPY pyproject.toml LICENSE README.md ./
COPY jupyterlab/ ./jupyterlab/
COPY . .

# 호스트 서버에 있는 커스텀 JupyterLab 전체 소스 코드를 컨테이너 안으로 복사
# Node.js의 패키지 매니저인 yarn을 이용해 프론트엔드 의존성(자바스크립트 패키지들)을 전부 다운로드
# 설치 가능한 1개의 배포용 파일(.whl 파일)로 압축하여 /export 폴더에 저장
RUN corepack enable && \
    yarn install && \
    python3 -m build --wheel --no-isolation --outdir /export

# 3. Final Stage: Provider Image
# 위에서 만들었던 결과물(shim.so, wheel 파일)만 가져와서, 가상 환경 1개로 포장하는 최종 단계입니다.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

# Stage1 & 2 에서 생성한 .so 파일과 .whl 파일, 그리고 호스트 서버에서 provider_entrypoint.sh 스크립트도 가져옵니다.
# GCC나 Node.js 같은 무거운 툴은 최종 이미지에 포함하지 않습니다
RUN apt-get update && apt-get install -y python3 python3-pip python3-venv curl && rm -rf /var/lib/apt/lists/*
WORKDIR /provider
COPY --from=shim-builder /build/libcudart_shim.so ./
COPY --from=frontend-builder /export/*.whl ./
COPY provider_entrypoint.sh ./

# 최종 포장용 폴더인 `/provider_venv`에 파이썬 가상 환경을 만듭니다.
# 가져온 커스텀 JupyterLab .whl 패키지를 설치, Shim 라이브러리와 엔트리포인트 스크립트를 전부 이 가상 환경 폴더(/provider_venv/) 안으로 집어넣습니다.
# 나중에 이 폴더를 통째로 `/dev/shm/`로 던져주기 위함입니다.
RUN python3 -m venv /provider_venv && \
    /provider_venv/bin/python -m pip install --upgrade pip && \
    /provider_venv/bin/python -m pip install ./*.whl && \
    cp libcudart_shim.so provider_entrypoint.sh /provider_venv/ && \
    find /provider_venv/bin -type f -exec sed -i 's|^#!/provider_venv/bin/python.*|#!/usr/bin/env python3|g' {} + && \
    mkdir -p /provider_venv/vscode && \
    curl -fL https://github.com/coder/code-server/releases/download/v4.93.1/code-server-4.93.1-linux-amd64.tar.gz | tar -xz -C /provider_venv/vscode --strip-components=1

CMD ["echo", "This is a passive provider image. Use it as an initContainer to copy files."]
