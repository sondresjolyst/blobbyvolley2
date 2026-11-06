# Container image for the Blobby Volley 2 dedicated server.
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
COPY CMakeLists.txt ./
COPY deps ./deps
COPY data ./data
COPY src ./src
COPY linux ./linux
COPY macos ./macos

RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /build --parallel --target blobby-server rules_zip

FROM debian:trixie-slim@sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        libsdl2-2.0-0 \
        libphysfs1 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home --shell /usr/sbin/nologin blobby

COPY --from=build /build/src/blobby-server /usr/local/bin/blobby-server
COPY --from=build /build/data/rules.zip /usr/local/share/blobby/rules.zip
COPY --from=build /src/data/server.xml /src/data/rules_api.lua /src/data/api.lua /usr/local/share/blobby/

USER blobby
EXPOSE 1234/udp
# Run in the foreground and print log messages to stderr, which Docker collects.
ENTRYPOINT ["blobby-server", "--no-daemon", "--print-msgs"]
