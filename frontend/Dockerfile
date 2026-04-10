# ---- Stage 1: Build --------------------------------------------------------
FROM node:22-alpine AS build

# Build-Args von docker-compose (aus Root-.env)
ARG VITE_MSAL_CLIENT_ID
ARG VITE_MSAL_TENANT_ID
ARG VITE_DEBUG=false

WORKDIR /app/frontend

COPY frontend/package.json frontend/package-lock.json* ./
RUN npm install

COPY frontend/ .

# .env im Parent-Dir (/app) erstellen, damit envDir: '..' in vite.config greift
RUN mkdir -p /app && \
    printf "VITE_MSAL_CLIENT_ID=%s\nVITE_MSAL_TENANT_ID=%s\nVITE_DEBUG=%s\n" \
    "$VITE_MSAL_CLIENT_ID" "$VITE_MSAL_TENANT_ID" "$VITE_DEBUG" > /app/.env

RUN npm run build

# ---- Stage 2: Serve --------------------------------------------------------
FROM nginx:1.27-alpine AS runtime

COPY --from=build /app/frontend/dist /usr/share/nginx/html
COPY frontend/nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80
