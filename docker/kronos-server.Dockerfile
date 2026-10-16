# Kronos game server in a container: docker build -f docker/kronos-server.Dockerfile -t kronos-server .
FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git ca-certificates pkg-config libvulkan-dev libsdl2-dev zlib1g-dev \
    libcurl4-openssl-dev libsecret-1-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev glslc \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S engine -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENGINE_BUILD_STUDIO=OFF \
    && cmake --build build --parallel --target kronos_server

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends libcurl4 libsecret-1-0 zlib1g ca-certificates \
    && rm -rf /var/lib/apt/lists/* && useradd --create-home kronos
COPY --from=build /src/build/src/kronos_server /usr/local/bin/kronos_server
USER kronos
WORKDIR /home/kronos
ENV KRONOS_GAMES_DIR=/games
VOLUME /games
EXPOSE 7777/udp
ENTRYPOINT ["kronos_server"]
CMD ["--port", "7777"]
