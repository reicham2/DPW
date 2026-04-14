# syntax=docker/dockerfile:1.6

# ---- Stage 1: Build --------------------------------------------------------
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
  ccache \
    cmake \
    git \
    libssl-dev \
    zlib1g-dev \
    libuv1-dev \
    libpq-dev \
    libcurl4-openssl-dev \
    ca-certificates \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt .
COPY src/ src/
COPY deps/ deps/

ARG BACKEND_BUILD_TYPE=Release
ENV CCACHE_DIR=/root/.cache/ccache

RUN --mount=type=cache,target=/root/.cache/ccache \
  cmake -B build -DCMAKE_BUILD_TYPE=${BACKEND_BUILD_TYPE} \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  && cmake --build build --parallel "$(nproc)"

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
