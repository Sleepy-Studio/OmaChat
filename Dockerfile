# omachat-server only: no Qt Quick, PipeWire, Opus, FFmpeg or Wayland are
# needed to run the server, so the image stays small and has no GUI/audio
# stack at all. Arch-based on both stages so the same package versions this
# project is tested against (see .github/workflows/ci.yml) are what runs in
# production.
FROM archlinux:base-devel AS build

RUN pacman -Syu --noconfirm --needed \
        cmake ninja qt6-base qt6-imageformats protobuf abseil-cpp libsodium tomlplusplus openssl \
    && pacman -Scc --noconfirm

WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DOMACHAT_BUILD_CLIENT=OFF \
        -DOMACHAT_BUILD_DAEMON=OFF \
        -DOMACHAT_BUILD_CLI=OFF \
        -DOMACHAT_BUILD_TESTS=OFF \
    && cmake --build build --target omachat-server -j"$(nproc)"

FROM archlinux:base AS runtime

RUN pacman -Syu --noconfirm --needed qt6-base qt6-imageformats protobuf abseil-cpp libsodium tomlplusplus openssl \
    && pacman -Scc --noconfirm \
    && useradd --system --uid 954 --home-dir /var/lib/omachat --shell /usr/bin/nologin omachat \
    && install -d -o omachat -g omachat -m 700 /var/lib/omachat /etc/omachat

COPY --from=build /src/build/server/omachat-server /usr/bin/omachat-server
COPY packaging/server/server.toml.example /usr/share/doc/omachat/server.toml.example
COPY packaging/docker/entrypoint.sh /usr/local/bin/omachat-entrypoint
RUN chmod +x /usr/local/bin/omachat-entrypoint

USER omachat
WORKDIR /var/lib/omachat
VOLUME ["/var/lib/omachat"]
EXPOSE 6473/tcp 6474/udp

# See packaging/docker/entrypoint.sh: on first run it writes a config from
# OMACHAT_* environment variables and generates a self-signed certificate
# if none is mounted, so `docker run` works with zero setup.
ENTRYPOINT ["/usr/local/bin/omachat-entrypoint"]
