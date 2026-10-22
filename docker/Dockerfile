# Build from the repository root, with submodules initialized:
#   docker build -f docker/Dockerfile -t prmurmur .

FROM ubuntu:24.04 AS base

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install --no-install-recommends -y \
    ca-certificates \
    libcap2 \
    libfmt9 \
    '^libprotobuf[0-9]+t64$' \
    libqt6core6t64 \
    libqt6network6t64 \
    libqt6sql6t64 \
    libqt6sql6-sqlite \
    libqt6xml6t64 \
    libspdlog1.12 \
    libsqlite3-0 \
    libssl3t64 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*


FROM base AS build

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install --no-install-recommends -y \
    build-essential cmake ninja-build pkgconf git ca-certificates python3 \
    qt6-base-dev qt6-tools-dev qt6-tools-dev-tools \
    libssl-dev libboost-dev libprotobuf-dev protobuf-compiler libprotoc-dev libcap-dev \
    libcli11-dev libspdlog-dev libfmt-dev nlohmann-json3-dev libutfcpp-dev libmsgsl-dev \
    libsqlite3-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN for submodule in 3rdparty/cmake-compiler-flags 3rdparty/tracy; do \
        if [ -z "$(ls -A "$submodule")" ]; then \
            echo "$submodule is empty: run 'git submodule update --init --recursive' first" >&2; \
            exit 1; \
        fi; \
    done

# The dependency provider fetches SOCI >= 4.1.4 (Ubuntu 24.04 only ships 4.0.x).
# Boost_NO_BOOST_CMAKE stops FindBoost from re-entering the provider while SOCI is configured.
RUN --mount=type=cache,target=/build \
    cmake -G Ninja -S . -B /build \
        -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=cmake/mumble_dependency_provider.cmake \
        -DBoost_NO_BOOST_CMAKE=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_UNITY_BUILD=ON \
        -Dclient=OFF \
        -Dserver=ON \
        -Dice=OFF \
        -Dzeroconf=OFF \
        -Denable-mysql=OFF \
        -Denable-postgresql=OFF \
        -Dtests=OFF \
        -Dwarnings-as-errors=OFF \
    && cmake --build /build \
    && cp /build/prmurmurd /usr/local/bin/prmurmurd


FROM base AS prmurmur

COPY --from=build /usr/local/bin/prmurmurd /usr/local/bin/prmurmurd
COPY auxiliary_files/mumble-server.ini /etc/prmurmur/bare_config.ini
COPY --chmod=755 docker/entrypoint.sh /entrypoint.sh

RUN mkdir -p /data

EXPOSE 64740/tcp 64740/udp

VOLUME ["/data"]
ENTRYPOINT ["/entrypoint.sh"]
