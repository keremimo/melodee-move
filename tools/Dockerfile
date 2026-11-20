# SPDX-License-Identifier: AGPL-3.0-only
# The build image: Ubuntu 22.04 for linux/arm64. Its glibc, 2.35, is the
# newest Ableton's Move firmware has (AbletonOS 4.1), so the one binary
# runs there and on poundhard-armbian (trixie, newer) alike; libstdc++ is
# linked in statically (Makefile).
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ make git ca-certificates pkg-config python3 \
    libjack-jackd2-dev libfreetype-dev libfontconfig-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
