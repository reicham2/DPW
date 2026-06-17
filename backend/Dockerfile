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
COPY backend/CMakeLists.txt .
COPY backend/deps/ deps/

# Stub sources so cmake configure caches without real src/
RUN mkdir -p src/core/models src/infra src/db src/utils src/handlers \
  && echo "// stub" > src/main.cpp \
  && for f in http request_helpers; do echo "// stub" > src/core/$f.cpp; done \
  && for f in auth cache app_config ws_manager graph wp_client web_push midata_client weather_client; do \
       echo "// stub" > src/infra/$f.cpp; done \
  && for f in db_connection db_activities db_users db_notifications db_forms db_mail db_admin db_camps; do \
       echo "// stub" > src/db/$f.cpp; done \
  && for f in permissions strings templates activity_helpers; do \
       echo "// stub" > src/utils/$f.cpp; done \
  && for f in activities forms mail users notifications admin share_ideenkiste setup_maintenance camps; do \
       echo "// stub" > src/handlers/$f.cpp; done \
  && touch src/core/http.hpp src/core/request_helpers.hpp \
  && touch src/core/models/all_models.hpp src/core/models/activity.hpp \
     src/core/models/admin.hpp src/core/models/attachment.hpp src/core/models/form.hpp \
     src/core/models/ideenkiste.hpp src/core/models/location.hpp src/core/models/mail.hpp \
     src/core/models/notification.hpp src/core/models/serializers.hpp \
     src/core/models/share.hpp src/core/models/user.hpp src/core/models/camp.hpp \
  && touch src/infra/auth.hpp src/infra/cache.hpp src/infra/app_config.hpp \
     src/infra/ws_manager.hpp src/infra/graph.hpp src/infra/wp_client.hpp \
     src/infra/web_push.hpp src/infra/midata_client.hpp src/infra/weather_client.hpp \
  && touch src/db/database.hpp \
  && touch src/utils/permissions.hpp src/utils/strings.hpp src/utils/templates.hpp \
     src/utils/activity_helpers.hpp \
  && touch src/handlers/handlers.hpp src/handlers/handler_deps.hpp

ARG BACKEND_BUILD_TYPE=Release
ARG ENABLE_DEBUG_AUTH=0

RUN cmake -B build -DCMAKE_BUILD_TYPE=${BACKEND_BUILD_TYPE} \
  -DENABLE_DEBUG_AUTH=${ENABLE_DEBUG_AUTH} \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

# Now copy real sources and build – only this layer reruns on code changes
COPY backend/src/ src/

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
  docker-cli \
  postgresql16-client \
  libcurl \
  curl \
  ca-certificates \
  libstdc++ \
  libgcc \
  tzdata

ENV TZ=Europe/Zurich

COPY --from=build /src/build/server /usr/local/bin/server
COPY db/init.sql /etc/dpw/init.sql

ENV DPW_INIT_SQL_PATH=/etc/dpw/init.sql

EXPOSE 8080
CMD ["server"]
