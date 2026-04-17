# syntax=docker/dockerfile:1

# ---- Stage 1: Build --------------------------------------------------------
FROM node:22-alpine AS build

WORKDIR /app

ARG ENABLE_DEBUG_AUTH=0
ENV VITE_ENABLE_DEBUG_AUTH=${ENABLE_DEBUG_AUTH}

COPY package.json package-lock.json ./
RUN --mount=type=cache,target=/root/.npm npm ci

COPY . .
RUN npm run build

# ---- Stage 2: Serve --------------------------------------------------------
FROM nginx:1.27-alpine AS runtime

COPY --from=build /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf
COPY docker-entrypoint.sh /docker-entrypoint.sh

RUN sed -i 's/\r$//' /docker-entrypoint.sh && chmod +x /docker-entrypoint.sh

EXPOSE 80
ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["nginx", "-g", "daemon off;"]
