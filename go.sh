#!/usr/bin/env bash
# vim :set ts=4 sw=4 sts=4 et:
die() { printf $'Error: %s\n' "$*" >&2; exit 1; }
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)
self=${BASH_SOURCE[0]:?}
project=${root##*/}
pexec() { >&2 printf exec; >&2 printf ' %q' "$@"; >&2 printf '\n'; exec "$@"; }
#---


go---docker() {
    pexec "${self:?}" docker \
    exec "${self:?}" "$@"
}

go---ospray() {
    pexec "${self:?}" ospray \
    exec "${self:?}" "$@"
}

go-osprayAsAService() {
    pexec "${cmake_binary_dir:?}/osprayAsAService" \
        "$@" \
        ##
}

go-studioAsAService() {
    pexec 
}


#--- Docker

docker_tag=${project,,}:latest
docker_name=${project,,}
docker_start=(
    --mount="type=bind,src=${root:?},dst=${root:?},readonly=false"
    --mount="type=bind,src=${HOME:?},dst=${HOME:?},readonly=false"
    --mount="type=bind,src=/etc/passwd,dst=/etc/passwd,readonly=true"
    --mount="type=bind,src=/etc/group,dst=/etc/group,readonly=true"
    --mount="type=bind,src=/mnt/seenas2/data,dst=/mnt/seenas2/data,readonly=true"
)
docker_exec=(
)

go-docker() {
    "${FUNCNAME[0]:?}-$@"
}

go-docker-build() {
    pexec docker build \
        --tag "${docker_tag:?}" \
        - <<'EOF'
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y \
        cmake \
        gcc \
        g++ \
        libx11-dev \
        libglu1-mesa-dev \
        xorg-dev \
        libglfw3-dev \
        curl \
        gdb \
        git \
    && rm -rf /var/lib/apt/lists/*

RUN apt-get update && \
    apt-get install -y \
        software-properties-common \
    && \
    add-apt-repository \
        ppa:deadsnakes/ppa \
    && \
    apt-get update && \
    apt-get install -y \
        python3.9 \
        python3-dev \
        python3-pip \
        python3-virtualenv \
    && rm -rf /var/lib/apt/lists/*

ARG STUDIO_VERSION=0.12.1
WORKDIR /opt/studio-${STUDIO_VERSION:?}
RUN --mount=type=cache,target=/tmp \
    curl \
        --continue-at - \
        --location \
        https://github.com/ospray/ospray_studio/releases/download/v${STUDIO_VERSION:?}/ospray_studio-${STUDIO_VERSION:?}.x86_64.linux.tar.gz \
        --output /tmp/studio-${STUDIO_VERSION:?}.x86_64.linux.tar.gz \
    && \
    tar \
        --extract \
        --file=/tmp/studio-${STUDIO_VERSION:?}.x86_64.linux.tar.gz \
        --strip-components=1 \
        --directory=/opt/studio-${STUDIO_VERSION:?} \
    && \
    true
ENV PATH="/opt/studio-${STUDIO_VERSION:?}/bin${PATH:+:${PATH}}" \
    CPATH="/opt/studio-${STUDIO_VERSION:?}/include${CPATH:+:${CPATH}}" \
    LIBRARY_PATH="/opt/studio-${STUDIO_VERSION:?}/lib64${LIBRARY_PATH:+:${LIBRARY_PATH}}" \
    LD_LIBRARY_PATH="/opt/studio-${STUDIO_VERSION:?}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
    PYTHONPATH="/opt/studio-${STUDIO_VERSION:?}/lib64${PYTHONPATH:+:${PYTHONPATH}}"

EOF
}

go-docker-start() {
    pexec docker run \
        --rm \
        --init \
        --detach \
        --name "${docker_name:?}" \
        "${docker_start[@]}" \
        "${docker_tag:?}" \
        sleep infinity \
        ##
}

go-docker-stop() {
    pexec docker stop \
        --time 0 \
        "${docker_name:?}" \
        ##
}

go-docker-exec() {
    local tty
    if [ -t 0 ]; then
        tty=
    fi

    pexec docker exec \
        ${tty+--tty} \
        --interactive \
        --detach-keys="ctrl-q,ctrl-q" \
        --user "$(id -u):$(id -g)" \
        --workdir "${PWD:?}" \
        --env USER \
        --env HOSTNAME \
        "${docker_name:?}" \
        "$@"
}


#---

ospray_source_dir=${root:?}/external/ospray/scripts/superbuild
ospray_binary_dir=${ospray_source_dir:?}/build
ospray_prefix_dir=${ospray_binary_dir:?}/stage
ospray_configure=(
    -DCMAKE_BUILD_TYPE:STRING=Debug
    -DINSTALL_IN_SEPARATE_DIRECTORIES:BOOL=OFF
)
ospray_build=(
)
ospray_install=(
)

go-ospray() {
    "${FUNCNAME[0]:?}-$@"
}

go-ospray-clean() {
    pexec rm -rfv -- \
        "${ospray_binary_dir:?}" \
        ##
}

go-ospray-configure() {
    pexec cmake \
        -H"${ospray_source_dir:?}" \
        -B"${ospray_binary_dir:?}" \
        -DCMAKE_INSTALL_PREFIX:PATH="${ospray_prefix_dir:?}" \
        "${ospray_configure[@]}" \
        "$@" \
        ##
}

go-ospray-build() {
    pexec cmake \
        --build "${ospray_binary_dir:?}" \
        "${ospray_build[@]}" \
        "$@" \
        ##
}

go-ospray-install() {
    pexec cmake \
        --install "${ospray_binary_dir:?}" \
        "${ospray_install[@]}" \
        "$@" \
        ##
}

go-ospray-exec() {
    PATH=${ospray_prefix_dir:?}/bin${PATH:+:${PATH:?}} \
    LD_LIBRARY_PATH=${ospray_prefix_dir:?}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH:?}} \
    pexec "$@"
}




#---

cmake_source_dir=${root:?}
cmake_binary_dir=${cmake_source_dir:?}/build
cmake_prefix_dir=${cmake_binary_dir:?}/stage
cmake_configure=(
    -DCMAKE_BUILD_TYPE:STRING=Debug
)
cmake_build=(
)
cmake_install=(
)

go-cmake() {
    "${FUNCNAME[0]:?}-$@"
}

go-cmake-clean() {
    pexec rm -rfv -- \
        "${cmake_binary_dir:?}" \
        ##
}

go-cmake-configure() {
    pexec cmake \
        -H"${cmake_source_dir:?}" \
        -B"${cmake_binary_dir:?}" \
        -DCMAKE_INSTALL_PREFIX:PATH="${cmake_prefix_dir:?}" \
        "${cmake_configure[@]}" \
        "$@" \
        ##
}

go-cmake-build() {
    pexec cmake \
        --build "${cmake_binary_dir:?}" \
        "${cmake_build[@]}" \
        "$@" \
        ##
}

go-cmake-install() {
    pexec cmake \
        --install "${cmake_binary_dir:?}" \
        "${cmake_install[@]}" \
        "$@" \
        ##
}


#---
test -f "${root:?}/env.sh" && source "${_:?}"
"go-$@"
