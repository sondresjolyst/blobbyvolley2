# Container image for the headless load test client. Build it from the root of the repository:
#   docker build -f tools/loadbot/Dockerfile -t blobby-loadbot .
FROM debian:trixie-slim@sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2 AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        libsdl2-dev \
        libphysfs-dev \
        libboost-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt version.txt ./
COPY cmake ./cmake
COPY deps ./deps
COPY data ./data
COPY src ./src
COPY linux ./linux
COPY macos ./macos
COPY launcher ./launcher
COPY tools/loadbot/*.cpp tools/loadbot/*.h ./tools/loadbot/

RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DBLOBBY_SITE_SYNC=OFF -DBLOBBY_LAUNCHER=OFF -DBUILD_LOADBOT=ON \
    && cmake --build /build --parallel --target blobby-loadbot

FROM debian:trixie-slim@sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2

ARG BLOBBY_VERSION=dev
LABEL org.opencontainers.image.title="blobby-loadbot" \
      org.opencontainers.image.description="Headless load test client for a Blobby Volley 2 tournament server" \
      org.opencontainers.image.version="${BLOBBY_VERSION}" \
      org.opencontainers.image.source="https://github.com/sondresjolyst/blobbyvolley2-tournament" \
      org.opencontainers.image.licenses="GPL-2.0-or-later"

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libsdl2-2.0-0 \
        libphysfs1 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 999 --user-group --no-create-home --shell /usr/sbin/nologin blobby

COPY --from=build /build/src/blobby-loadbot /usr/local/bin/blobby-loadbot
# the bot scripts for --script and the lua files they run on, in the data folder the build looks in
COPY data/api.lua data/bot_api.lua /usr/local/share/blobby/
COPY data/scripts/*.lua /usr/local/share/blobby/scripts/

USER blobby
ENTRYPOINT ["blobby-loadbot"]
