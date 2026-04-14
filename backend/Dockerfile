# syntax=docker/dockerfile:1

# ---- Stage 1: Build --------------------------------------------------------
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
  build-essential \
  ccache \
  cmake \
  libssl-dev \
  zlib1g-dev \
  libuv1-dev \
  libpq-dev \
  libcurl4-openssl-dev \
  ca-certificates \
  && rm -rf /var/lib/apt/lists/*

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
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
  libssl3 \
  zlib1g \
  libuv1t64 \
  libpq5 \
  libcurl4 \
  curl \
  ca-certificates \
  && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/server /usr/local/bin/server

EXPOSE 8080
CMD ["server"]
