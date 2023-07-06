FROM ubuntu:22.04 AS dev
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
        python3 \
        python3-pip \
        python3-virtualenv \
    && rm -rf /var/lib/apt/lists/*


ARG OSPRAY_VERSION=2.12.0
WORKDIR /opt/ospray-${OSPRAY_VERSION:?}
RUN --mount=type=cache,target=/tmp \
    curl \
        --continue-at - \
        --location \
        https://github.com/ospray/ospray/releases/download/v${OSPRAY_VERSION:?}/ospray-${OSPRAY_VERSION:?}.x86_64.linux.tar.gz \
        --output /tmp/ospray-${OSPRAY_VERSION:?}.x86_64.linux.tar.gz \
    && \
    tar \
        --extract \
        --file=/tmp/ospray-${OSPRAY_VERSION:?}.x86_64.linux.tar.gz \
        --strip-components=1 \
        --directory=/opt/ospray-${OSPRAY_VERSION:?} \
    && \
    true
ENV PATH="/opt/ospray-${OSPRAY_VERSION:?}/bin${PATH:+:${PATH}}" \
    CPATH="/opt/ospray-${OSPRAY_VERSION:?}/include${CPATH:+:${CPATH}}" \
    LIBRARY_PATH="/opt/ospray-${OSPRAY_VERSION:?}/lib${LIBRARY_PATH:+:${LIBRARY_PATH}}" \
    LD_LIBRARY_PATH="/opt/ospray-${OSPRAY_VERSION:?}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"


FROM dev AS prod
WORKDIR /opt/src/tapestry
COPY . .
RUN <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

source_dir=/opt/src/tapestry
binary_dir=${source_dir:?}/build
prefix_dir=/opt/tapestry

cmake \
    -H"${source_dir:?}" \
    -B"${binary_dir:?}" \
    -DCMAKE_INSTALL_PREFIX:PATH="${prefix_dir:?}" \
    -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo \
    ##

cmake \
    --build "${binary_dir:?}" \
    ##

cmake \
    --install "${binary_dir:?}" \
    ##

EOF

ENV PATH="/opt/tapestry/bin${PATH:+:${PATH}}"

CMD ["tapestryServer"]
