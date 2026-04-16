# syntax=docker/dockerfile:1

# ---- Stage 1: Build --------------------------------------------------------
FROM alpine:3.21 AS build

RUN apk add --no-cache \
  build-base \
  cmake \
  ccache \
  openssl-dev \
  zlib-dev \
  libuv-dev \
  libpq-dev \
  curl-dev \
  linux-headers

WORKDIR /src
COPY CMakeLists.txt .
COPY deps/ deps/

# Stub sources so cmake configure caches without real src/
RUN mkdir -p src && for f in main auth db handlers ws_manager graph; do \
  echo "// stub" > src/$f.cpp; \
  done && touch src/auth.hpp src/db.hpp src/handlers.hpp src/models.hpp \
  src/ws_manager.hpp src/graph.hpp

ARG BACKEND_BUILD_TYPE=Release

RUN cmake -B build -DCMAKE_BUILD_TYPE=${BACKEND_BUILD_TYPE} \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

# Now copy real sources and build – only this layer reruns on code changes
COPY src/ src/

RUN --mount=type=cache,target=/root/.cache/ccache \
  CCACHE_DIR=/root/.cache/ccache \
  cmake --build build --parallel "$(nproc)"

# ---- Stage 2: Runtime ------------------------------------------------------
FROM alpine:3.21 AS runtime

RUN apk add --no-cache \
  libssl3 \
  zlib \
  libuv \
  libpq \
  libcurl \
  curl \
  ca-certificates \
  libstdc++ \
  libgcc

COPY --from=build /src/build/server /usr/local/bin/server

EXPOSE 8080
CMD ["server"]
