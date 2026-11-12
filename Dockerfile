# Build WebAssembly and serve it with nginx.
# The image includes FDG Entertainment assets; do not redistribute it.
# Override EMSDK_VERSION to select a specific toolchain.
ARG EMSDK_VERSION=latest
ARG NGINX_VERSION=alpine

FROM emscripten/emsdk:${EMSDK_VERSION} AS build

WORKDIR /src

COPY docker/sdl-release-2.32.10.zip /tmp/sdl2.zip
RUN unzip /tmp/sdl2.zip -d /tmp && \
    mv /tmp/SDL-release-2.32.10 /tmp/SDL2

ENV EMCC_LOCAL_PORTS="sdl2=/tmp/SDL2"

# Cache SDL separately. Older Emscripten ports need pkgconfig created before the build.
RUN mkdir -p "$(em-config CACHE)/sysroot/lib/pkgconfig" \
 && embuilder build sdl2 \
 && test -f "$(em-config CACHE)/sysroot/lib/wasm32-emscripten/libSDL2.a"

COPY . .

# Reuse local assets when available; fetch them for a fresh checkout.
RUN if [ -n "$(ls -A assets/v1/level 2>/dev/null)" ]; then \
        echo "assets: using the copy from the build context"; \
    else \
        echo "assets: not in the build context, fetching"; \
        tools/fetch_assets.sh; \
    fi \
 && test "$(ls assets/v1/level/*.lvl | wc -l)" -eq 50

# Invoke CMake directly to avoid GNU make PATH issues with the emsdk environment.
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
