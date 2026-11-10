# Builds the WebAssembly version in a container and serves it with nginx.
#
# This sidesteps the host toolchain entirely -- no emsdk to install, no PATH to
# activate, no Emscripten cache on a filesystem that might refuse to cooperate.
#
#   docker build -t bobby-carrot .
#   docker run --rm -p 8080:80 bobby-carrot      # → http://localhost:8080
#
# The image embeds the original game assets, which belong to FDG Entertainment.
# Keep it local: do not push it to a public registry. See README.md.

# The emsdk image is pinned through a build argument rather than hard-coded, so
# a known-good Emscripten can be selected without editing this file:
#   docker build --build-arg EMSDK_VERSION=4.0.16 -t bobby-carrot .
ARG EMSDK_VERSION=latest
ARG NGINX_VERSION=alpine

FROM emscripten/emsdk:${EMSDK_VERSION} AS build

WORKDIR /src

COPY docker/sdl-release-2.32.10.zip /tmp/sdl2.zip
RUN unzip /tmp/sdl2.zip -d /tmp && \
    mv /tmp/SDL-release-2.32.10 /tmp/SDL2

# Use the correct environment variable name
ENV EMCC_LOCAL_PORTS="sdl2=/tmp/SDL2"

# Build the SDL2 port on its own layer, before any source is copied: it is the
# slowest step and the one least likely to change, so it stays cached across
# edits to the game.
#
# The mkdir works around an Emscripten bug -- the SDL2 port writes
# sysroot/lib/pkgconfig/sdl2.pc without creating the directory first, which
# breaks on a cache that has not been populated yet.
RUN mkdir -p "$(em-config CACHE)/sysroot/lib/pkgconfig" \
 && embuilder build sdl2 \
 && test -f "$(em-config CACHE)/sysroot/lib/wasm32-emscripten/libSDL2.a"

COPY . .

# Use the assets from the build context when they are there, otherwise fetch
# them. Keeping both paths means a working copy that already has assets does
# not hit the network, and a fresh clone still builds unattended.
RUN if [ -n "$(ls -A assets/v1/level 2>/dev/null)" ]; then \
        echo "assets: using the copy from the build context"; \
    else \
        echo "assets: not in the build context, fetching"; \
        tools/fetch_assets.sh; \
    fi \
 && test "$(ls assets/v1/level/*.lvl | wc -l)" -eq 50

# cmake is invoked directly rather than through `make web`: sourcing the emsdk
# environment puts a directory literally named "cmake" on PATH, which trips up
# GNU make's own PATH walk (see README.md).
RUN emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build-web -j"$(nproc)" \
 && test -f build-web/index.wasm \
 && test -f build-web/index.data \
 && ls -la build-web/index.*


FROM nginx:${NGINX_VERSION} AS runtime

LABEL org.opencontainers.image.title="Bobby Carrot (WebAssembly)" \
      org.opencontainers.image.description="C++/Emscripten port. Contains third-party game assets; do not redistribute." \
      com.bobbycarrot.redistributable="false"

COPY docker/nginx.conf /etc/nginx/conf.d/default.conf
COPY --from=build \
    /src/build-web/index.html \
    /src/build-web/index.js \
    /src/build-web/index.wasm \
    /src/build-web/index.data \
    /usr/share/nginx/html/

EXPOSE 80

HEALTHCHECK --interval=30s --timeout=3s --start-period=2s \
    CMD wget -q -O /dev/null http://127.0.0.1/index.wasm || exit 1
