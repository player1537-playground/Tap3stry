#!/usr/bin/env bash
# vim :set ts=4 sw=4 sts=4 et:
die() { printf $'Error: %s\n' "$*" >&2; exit 1; }
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)
self=$(realpath "${BASH_SOURCE[0]:?}")
project=${root##*/}
pexec() { >&2 printf exec; >&2 printf ' %q' "$@"; >&2 printf '\n'; exec "$@"; }
#---


go---docker() {
    pexec "${self:?}" docker \
    exec "${self:?}" "$@"
}

go---virtualenv() {
    pexec "${self:?}" virtualenv \
    exec "${self:?}" "$@"
}

go-gdb-engine() {
    pexec gdb \
        -ex="r > /dev/null < ${root:?}/tmp/engine.stdin.txt" \
        --args \
            "${cmake_binary_dir:?}/engine" \
            "$@" \
            ##
}

go-engine() {
    pexec "${cmake_binary_dir:?}/engine" \
        "$@" \
        ##
}

go-server() {
    # OSPRAY_LOG_LEVEL=debug \
    # OSPRAY_LOG_OUTPUT=cerr \
    # OSPRAY_ERROR_OUTPUT=cerr \
    pexec python3 "${cmake_source_dir:?}/src/server/main.py" \
        --engine-executable "${cmake_binary_dir:?}/engine" \
        ##
}

go-uwsgi() {
    pexec uwsgi \
        --enable-threads \
        --http :8080 \
        --http-keepalive=1 \
        --pymodule-alias wsgi="${cmake_source_dir:?}/src/server/main.py" \
        --module wsgi:app \
        --env ENGINE_EXECUTABLE="${cmake_binary_dir:?}/engine" \
        ##
}

go-github.io() {
    pexec python3 -m http.server \
        --bind 0.0.0.0 \
        --directory "${cmake_source_dir:?}/external/github.io" \
        8081 \
        ##
}


#--- Docker

docker_source_dir=${root:?}
docker_tag=${project,,}:latest
docker_name=${project,,}
docker_build=(
    --progress=plain
)
docker_start=(
    --mount="type=bind,src=${root:?},dst=${root:?},readonly=false"
    --mount="type=bind,src=${HOME:?},dst=${HOME:?},readonly=false"
    --mount="type=bind,src=/etc/passwd,dst=/etc/passwd,readonly=true"
    --mount="type=bind,src=/etc/group,dst=/etc/group,readonly=true"
)
docker_exec=(
)
docker_service_name=${project,,}
docker_service_create=(
    --publish="8080:8080"
)

go-docker() {
    "${FUNCNAME[0]:?}-$@"
}

go-docker---dev() {
    docker_build+=(
        --target=dev
    )
    "${FUNCNAME[0]%%--*}-$@"
}

go-docker---prod() {
    docker_build+=(
        --target=prod
    )
    "${FUNCNAME[0]%%--*}-$@"
}

go-docker-build() {
    pexec docker build \
        --tag "${docker_tag:?}" \
        "${docker_source_dir:?}" \
        "${docker_build[@]}" \
        ##
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

go-docker-service() {
    "${FUNCNAME[0]:?}-$@"
}

go-docker-service-create() {
    pexec docker service create \
        --name="${docker_service_name:?}" \
        ${1:+--replicas="${1:?}"} \
        "${docker_service_create[@]}" \
        "${docker_tag:?}" \
        ##
}

go-docker-service-scale() {
    pexec docker service scale \
        "${docker_service_name:?}=${1:?need replicas}" \
        ##
}

go-docker-service-stop() {
    pexec docker service rm \
        "${docker_service_name:?}" \
        ##
}


#--- Python

virtualenv_dir=${root:?}/venv

go-virtualenv() {
    "${FUNCNAME[0]:?}-${@-create}"
}

go-virtualenv-create() {
    pexec python3 -m virtualenv \
        "${virtualenv_dir:?}" \
        ##
}

go-virtualenv-pip() {
    "${FUNCNAME[0]:?}-${@-install}"
}

go-virtualenv-pip-install() {
    pexec "${virtualenv_dir:?}/bin/pip" install \
        -r "${root:?}/requirements.txt" \
        ##
}

go-virtualenv-python() {
    pexec "${virtualenv_dir:?}/bin/python" \
        "$@" \
        ##
}

go-virtualenv-exec() {
    source "${virtualenv_dir:?}/bin/activate" \
    || die "Failed to source: $_"

    pexec "$@"
}


#---

cmake_source_dir=${root:?}
cmake_binary_dir=${cmake_source_dir:?}/build
cmake_prefix_dir=${cmake_binary_dir:?}/stage
cmake_configure=(
    -DCMAKE_BUILD_TYPE:STRING=Debug
    -DCMAKE_CXX_FLAGS:STRING="-Wall -Werror -Wextra"
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
