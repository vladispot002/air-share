# ==============================================================================
# Stage 1: Build Web Client Assets with Node.js
# ==============================================================================
FROM node:22-alpine AS web-builder
WORKDIR /build/web

# Install dependencies
COPY web/package.json web/package-lock.json ./
RUN npm ci --no-audit --no-fund

# Copy source and build static distribution
COPY web/ ./
RUN npm run build

# ==============================================================================
# Stage 2: Compile Static Go Signaling Server
# ==============================================================================
FROM golang:1.23-alpine AS go-builder
WORKDIR /build/backend/signaling

RUN apk add --no-cache ca-certificates git

# Cache Go modules
COPY backend/signaling/go.mod backend/signaling/go.sum ./
RUN go mod download

# Copy source code
COPY backend/signaling/ ./

# Compile statically linked binary with symbol stripping (-s -w)
ARG TARGETARCH
RUN CGO_ENABLED=0 GOOS=linux GOARCH=${TARGETARCH} go build \
    -ldflags="-s -w -extldflags '-static'" \
    -trimpath \
    -o /bin/signaling-server ./cmd/server

# ==============================================================================
# Stage 3: Minimal Hardened Runtime Container (< 20MB)
# ==============================================================================
FROM alpine:3.20 AS runtime

# Security: Install root certificates and create dedicated non-root user
RUN apk --no-cache add ca-certificates tzdata wget \
    && addgroup -g 10001 -S landrop \
    && adduser -u 10001 -S landrop -G landrop -s /sbin/nologin

WORKDIR /app

LABEL org.opencontainers.image.title="LAN-Drop Signaling Service" \
      org.opencontainers.image.description="Signaling and WebRTC relay coordinator for LAN-Drop" \
      org.opencontainers.image.version="1.0.2" \
      org.opencontainers.image.vendor="LAN-Drop Team" \
      org.opencontainers.image.licenses="MIT"

# Copy compiled artifacts from builder stages
COPY --from=web-builder --chown=landrop:landrop /build/web/dist /app/web/dist
COPY --from=go-builder --chown=landrop:landrop /bin/signaling-server /app/signaling-server

# Default configuration environment
ENV PORT=8080 \
    WEB_DIR=/app/web/dist

# Expose HTTP/WebSocket signaling port
EXPOSE 8080

# Switch to unprivileged non-root user
USER 10001:10001

# Healthcheck validating HTTP endpoint
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD wget -q --spider http://127.0.0.1:8080/healthz || exit 1

STOPSIGNAL SIGTERM

ENTRYPOINT ["/app/signaling-server"]
