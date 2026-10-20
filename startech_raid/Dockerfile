FROM ghcr.io/home-assistant/base:3.24 AS backend-build
RUN apk add --no-cache build-base linux-headers
WORKDIR /build
COPY src ./src
RUN cc -std=c11 -Os -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror \
    -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
    -Wl,-z,noexecstack,-z,relro,-z,now -s src/startech_status.c -o /build/startech-raid-status

FROM ghcr.io/home-assistant/base:3.24
RUN apk add --no-cache python3
COPY --from=backend-build /build/startech-raid-status /usr/local/bin/startech-raid-status
COPY server.py seal_backend.py /app/
COPY web /app/web
COPY DOCS.md THIRD_PARTY.md LICENSE /app/
RUN python3 /app/seal_backend.py && chmod 0755 /usr/local/bin/startech-raid-status
CMD ["python3", "-u", "/app/server.py"]
