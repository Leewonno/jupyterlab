## Copyright (c) Jupyter Development Team.
## Distributed under the terms of the Modified BSD License.
#
##FROM --platform=linux/amd64 mambaorg/micromamba:1.5.5-jammy as base
#FROM mambaorg/micromamba:1.5.5-jammy AS base
#
## Create new user
#ARG NEW_MAMBA_USER_ID=57440
#ARG NEW_MAMBA_USER_GID=57440
#
#USER root
#
#RUN if grep -q '^ID=alpine$' /etc/os-release; then \
#  # alpine does not have usermod/groupmod
#  apk add --no-cache --virtual temp-packages shadow=4.13-r0; \
#  fi
#RUN if [ "$(id ${MAMBA_USER} -u)" != "$NEW_MAMBA_USER_ID" ]; then \
#  usermod "-u ${NEW_MAMBA_USER_ID}" "${MAMBA_USER}"; \
#  fi
#RUN if [ "$(id ${MAMBA_USER} -g)" != "$NEW_MAMBA_USER_GID" ]; then \
#  groupmod -o -g ${NEW_MAMBA_USER_GID} ${MAMBA_USER} && \
#  usermod -g ${NEW_MAMBA_USER_GID} ${MAMBA_USER}; \
#  fi
#RUN if grep -q '^ID=alpine$' /etc/os-release; then \
#  # remove the packages that were only needed for usermod/groupmod
#  apk del temp-packages; \
#  fi
#
#USER $MAMBA_USER
#
## Add the pip user installation path
#ENV PATH="/home/${MAMBA_USER}/.local/bin:${PATH}"
#
## Install base dev tools with conda
#RUN --mount=type=cache,target=/jupyterlab/conda-cache,uid=${NEW_MAMBA_USER_ID},gid=${NEW_MAMBA_USER_GID} \
#    CONDA_PKGS_DIRS="/jupyterlab/conda-cache" micromamba install -c nodefaults -c conda-forge -yn base nodejs=22 yarn=3 git python=3 less vim nano
#
#ENV EDITOR=nano
#
#WORKDIR /home/$MAMBA_USER/jupyterlab_cache
#
#COPY --chown=$MAMBA_USER:$MAMBA_USER pyproject.toml LICENSE README.md ./
#COPY --chown=$MAMBA_USER:$MAMBA_USER jupyterlab/_version.py ./jupyterlab/_version.py
#
## Install all python dependencies only with pip to maximize using its cache
#RUN --mount=type=cache,target=/jupyterlab/pip-cache,uid=${NEW_MAMBA_USER_ID},gid=${NEW_MAMBA_USER_GID} \
#    PIP_CACHE_DIR="/jupyterlab/pip-cache" SKIP_JUPYTER_BUILDER=1 micromamba run python -m pip install -e  ".[dev,docs,test]" && \
#    cd / && \
#    rm -rf /home/$MAMBA_USER/jupyterlab_cache
#
#EXPOSE 8888
#
#FROM base as dev
#
#COPY --chown=$MAMBA_USER:$MAMBA_USER . .
#
## Build the cache in /home/$MAMBA_USER/jupyterlab_cache
#RUN micromamba run yarn install
#
#WORKDIR /home/$MAMBA_USER/jupyterlab
#
#RUN micromamba install -n base -c conda-forge rsync -y && \
#    micromamba run rsync -ar /home/$MAMBA_USER/jupyterlab_cache/. /home/$MAMBA_USER/jupyterlab && \
#    micromamba run python -m pip install -e  "."


# Copyright (c) Jupyter Development Team.
# Distributed under the terms of the Modified BSD License.

# ====================== Ver 2

#FROM mambaorg/micromamba:1.5.5-jammy AS base
#
#ARG NEW_MAMBA_USER_ID=57440
#ARG NEW_MAMBA_USER_GID=57440
#
#USER root
#
#RUN if grep -q '^ID=alpine$' /etc/os-release; then \
#  apk add --no-cache --virtual temp-packages shadow=4.13-r0; \
#  fi
#RUN if [ "$(id ${MAMBA_USER} -u)" != "$NEW_MAMBA_USER_ID" ]; then \
#  usermod "-u ${NEW_MAMBA_USER_ID}" "${MAMBA_USER}"; \
#  fi
#RUN if [ "$(id ${MAMBA_USER} -g)" != "$NEW_MAMBA_USER_GID" ]; then \
#  groupmod -o -g ${NEW_MAMBA_USER_GID} ${MAMBA_USER} && \
#  usermod -g ${NEW_MAMBA_USER_GID} ${MAMBA_USER}; \
#  fi
#RUN if grep -q '^ID=alpine$' /etc/os-release; then \
#  apk del temp-packages; \
#  fi
#
#USER $MAMBA_USER
#
#ENV PATH="/home/${MAMBA_USER}/.local/bin:${PATH}"
#
#RUN --mount=type=cache,target=/jupyterlab/conda-cache,uid=${NEW_MAMBA_USER_ID},gid=${NEW_MAMBA_USER_GID} \
#    CONDA_PKGS_DIRS="/jupyterlab/conda-cache" micromamba install -c nodefaults -c conda-forge -yn base nodejs=22 yarn=3 git python=3 less vim nano
#
#ENV EDITOR=nano
#
#WORKDIR /home/$MAMBA_USER/jupyterlab_cache
#
#COPY --chown=$MAMBA_USER:$MAMBA_USER pyproject.toml LICENSE README.md ./
#COPY --chown=$MAMBA_USER:$MAMBA_USER jupyterlab/_version.py ./jupyterlab/_version.py
#
#RUN --mount=type=cache,target=/jupyterlab/pip-cache,uid=${NEW_MAMBA_USER_ID},gid=${NEW_MAMBA_USER_GID} \
#    PIP_CACHE_DIR="/jupyterlab/pip-cache" SKIP_JUPYTER_BUILDER=1 micromamba run python -m pip install -e  ".[dev,docs,test]" && \
#    cd / && \
#    rm -rf /home/$MAMBA_USER/jupyterlab_cache
#
#EXPOSE 8888
#
#FROM base AS dev
#
## (구) docker run의 -e 플래그들 → 파일 와처 폴링 설정
#ENV CHOKIDAR_USEPOLLING=true \
#    WATCHPACK_POLLING=true \
#    TSC_WATCHFILE=PriorityPollingInterval
#
#COPY --chown=$MAMBA_USER:$MAMBA_USER . .
#
#RUN micromamba run yarn install
#
#WORKDIR /home/$MAMBA_USER/jupyterlab
#
#RUN micromamba install -n base -c conda-forge rsync -y && \
#    micromamba run rsync -ar /home/$MAMBA_USER/jupyterlab_cache/. /home/$MAMBA_USER/jupyterlab && \
#    micromamba run python -m pip install -e  "."
#
## (구) docker run 명령의 mkdir 부분
#RUN mkdir -p /home/$MAMBA_USER/jlab_root
#
## (구) docker run의 bash -lc "..." → 기본 기동 명령
## build/watch/jupyter는 마운트된 소스 기준으로 동작해야 하므로 RUN이 아니라 CMD에 둠
#CMD ["bash", "-lc", "jlpm build && (jlpm watch &) && jupyter lab --dev-mode --watch --ip=0.0.0.0 --port=8888 --no-browser --allow-root --ServerApp.root_dir=/home/mambauser/jlab_root"]


# ====================== Ver 3

# Copyright (c) Jupyter Development Team.
# Distributed under the terms of the Modified BSD License.

# FROM --platform=linux/amd64 mambaorg/micromamba:1.5.5-jammy as base
# --platform=linux/amd64 제거 (아키텍처 amd64(x86_64)로 강제 지정 방지)
FROM mambaorg/micromamba:1.5.5-jammy AS base

# Create new user
ARG NEW_MAMBA_USER_ID=57440
ARG NEW_MAMBA_USER_GID=57440

USER root

RUN if grep -q '^ID=alpine$' /etc/os-release; then \
  apk add --no-cache --virtual temp-packages shadow=4.13-r0; \
  fi
RUN if [ "$(id ${MAMBA_USER} -u)" != "$NEW_MAMBA_USER_ID" ]; then \
  usermod "-u ${NEW_MAMBA_USER_ID}" "${MAMBA_USER}"; \
  fi
RUN if [ "$(id ${MAMBA_USER} -g)" != "$NEW_MAMBA_USER_GID" ]; then \
  groupmod -o -g ${NEW_MAMBA_USER_GID} ${MAMBA_USER} && \
  usermod -g ${NEW_MAMBA_USER_GID} ${MAMBA_USER}; \
  fi
RUN if grep -q '^ID=alpine$' /etc/os-release; then \
  apk del temp-packages; \
  fi

USER $MAMBA_USER

ENV PATH="/home/${MAMBA_USER}/.local/bin:${PATH}"

RUN --mount=type=cache,target=/jupyterlab/conda-cache,uid=${NEW_MAMBA_USER_ID},gid=${NEW_MAMBA_USER_GID} \
    CONDA_PKGS_DIRS="/jupyterlab/conda-cache" micromamba install -c nodefaults -c conda-forge -yn base nodejs=22 yarn=3 git python=3 less vim nano

ENV EDITOR=nano

WORKDIR /home/$MAMBA_USER/jupyterlab_cache

COPY --chown=$MAMBA_USER:$MAMBA_USER pyproject.toml LICENSE README.md ./
COPY --chown=$MAMBA_USER:$MAMBA_USER jupyterlab/_version.py ./jupyterlab/_version.py

RUN --mount=type=cache,target=/jupyterlab/pip-cache,uid=${NEW_MAMBA_USER_ID},gid=${NEW_MAMBA_USER_GID} \
    PIP_CACHE_DIR="/jupyterlab/pip-cache" SKIP_JUPYTER_BUILDER=1 micromamba run python -m pip install -e  ".[dev,docs,test]" && \
    cd / && \
    rm -rf /home/$MAMBA_USER/jupyterlab_cache

EXPOSE 8888

FROM base AS dev

COPY --chown=$MAMBA_USER:$MAMBA_USER . .

RUN micromamba run yarn install

WORKDIR /home/$MAMBA_USER/jupyterlab

RUN micromamba install -n base -c conda-forge rsync -y && \
    micromamba run rsync -ar /home/$MAMBA_USER/jupyterlab_cache/. /home/$MAMBA_USER/jupyterlab && \
    micromamba run python -m pip install -e  "."

# ==============================================
# 커스텀
# ==============================================

RUN cd packages/application-extension && micromamba run jlpm run build && cd ../..
RUN micromamba run jlpm build:core

# favicon 변경
USER root
RUN cp /home/$MAMBA_USER/jupyterlab/docker/favicon.ico \
    "$(micromamba run python -c 'import jupyter_server, os; print(os.path.dirname(jupyter_server.__file__))')/static/favicons/favicon.ico"

# 일반 사용자로 전환
USER $MAMBA_USER

# JupyterLab이 사용할 루트 작업 디렉터리 미리 생성
RUN mkdir -p /home/$MAMBA_USER/jlab_root

# 나중에 지워야할 코드
# --IdentityProvider.token='' : 토큰 없이 이용할 목적 -> 나중에 권한 관리 어떻게 하는지에 따라 삭제해야할수도 있음
# --dev-mode : 배포에서는 정식 설치 버전을 써야 하므로 제거
CMD ["bash", "-lc", "jupyter lab --app-dir=/home/mambauser/jupyterlab/jupyterlab --ip=0.0.0.0 --port=8888 --no-browser --ServerApp.root_dir=/home/mambauser/jlab_root --IdentityProvider.token=''"]

# 배포용 CMD
#CMD ["jupyter", "lab", "--ip=0.0.0.0", "--port=8888", "--no-browser", "--ServerApp.root_dir=/home/mambauser/jlab_root"]
