#!/bin/bash
# 스크립트 실행 중 단 하나의 명령어라도 실패(에러 코드 반환)하면 즉시 스크립트를 중단하도록 설정
set -e


# RAM 디스크(/dev/shm)로 복사된 가상 환경의 경로를 변수로 선언
PROVIDER_DIR="/dev/shm/provider_venv"
VENV_DIR="/dev/shm/provider_venv"

# 환경 변수 경로 설정
export PATH="/workspace/.local/bin:$VENV_DIR/bin:$PATH"

# PIP 인스톨 패키지를 system 디렉토리가 아닌 PVC에 설치하도록 설정
export PIP_PREFIX="/workspace/.local"

# 라이브러리 이미지의 python version과 상이 대비
if command -v python3 >/dev/null 2>&1; then
    PY_VER=$(python3 -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')
    # python 커널 재시작 시 최적화로 인한 문제 수정
    SITE_PKG="/workspace/.local/lib/python${PY_VER}/site-packages"
    mkdir -p "$SITE_PKG"
    export PYTHONPATH="$SITE_PKG:/opt/venv/lib/python${PY_VER}/site-packages${PYTHONPATH:+:$PYTHONPATH}"
fi

mkdir -p /opt/gpuguard
#  파일이 존재하지 않는다면, 처음 실행되는 상태이므로 아래의 Shim 주입 로직을 탑니다.(이미 존재한다면 주입 로직을 건너뜀)
if [ ! -f "/opt/gpuguard/libcudart_real.so" ]; then
    echo "Locating libcudart.so.12..."
    # 라이브러리 이미지(PyTorch 등) 어딘가에 숨어있는 진짜 libcudart.so.12 파일의 위치를 찾아 CUDART_PATH 변수에 저장
    CUDART_PATH=$(find /usr /opt /workspace -name "libcudart.so.12" | grep -v "real" | head -1)
    # 진짜 라이브러리의 경로를 찾았다면, 그 위치에 있던 진짜 파일을 /opt/gpuguard/libcudart_real.so 로 복사해서 백업
    if [ -n "$CUDART_PATH" ]; then
        echo "Found libcudart at $CUDART_PATH. Setting up shim..."
        cp "$CUDART_PATH" /opt/gpuguard/libcudart_real.so
        cp "$PROVIDER_DIR/libcudart_shim.so" "$CUDART_PATH"
        # /dev/shm/으로 배달해온 쿠다 라이브러리 shim(libcudart_shim.so)을 진짜 파일이 있던 원본 경로에 그대로 덮어씁니다
        # LD_PRELOAD: 어떤 프로그램을 실행하든, 여기에 등록된 라이브러리(.so)를 시스템의 다른 모든 라이브러리보다 무조건 가장 먼저 메모리에 로드(Pre-load)시킴
        export LD_PRELOAD="$PROVIDER_DIR/libcudart_shim.so"
    else
        # matplotlib 처럼 GPU가 필요 없는 이미지인 경우
        echo "Warning: libcudart.so.12 not found. Shim setup skipped."
    fi
else
    export LD_PRELOAD="$PROVIDER_DIR/libcudart_shim.so"
fi

echo "Starting IDE..."
# 이 쉘 스크립트가 실행될 때 외부에서 넘겨받은 모든 Arguments를 의미
# IDE 런타임 인계: Java의 PodSpecBuilder에서 설정하고 넘겨준 명령어 전체를 포함 덮어씌워서 프로세스를 교체, 실행하는 명령
exec "$@"
