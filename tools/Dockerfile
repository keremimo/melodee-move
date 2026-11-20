# SPDX-License-Identifier: AGPL-3.0-only
# The build image: Debian trixie for linux/arm64, the same release as the
# Move's Armbian, so the binary needs no newer glibc than the device has.
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ make git ca-certificates pkg-config python3 \
    libjack-jackd2-dev libfreetype-dev libfontconfig-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
