# SPDX-License-Identifier: AGPL-3.0-only
# The build image: Ubuntu 22.04 for linux/arm64. Its glibc, 2.35, is the
# newest Ableton's Move firmware has (AbletonOS 4.1), so the one binary
# runs there and on poundhard-armbian (trixie, newer) alike; libstdc++ is
# linked in statically (Makefile).
#
# The compiler is 22.04's GCC 12 (gcc, g++, cc and c++ are it), not its
# default GCC 11: GCC 11 spends minutes at -O2 wherever Melodee's Project is
# made or its PatternBank reset (melodee-sample's model and shell files),
# GCC 12 seconds. A clean build takes 53 s instead of 226 s.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-12 g++-12 make ccache git ca-certificates pkg-config python3 \
    libjack-jackd2-dev libfreetype-dev libfontconfig-dev \
    && ln -sf gcc-12 /usr/bin/gcc && ln -sf g++-12 /usr/bin/g++ \
    && ln -sf gcc-12 /usr/bin/cc && ln -sf g++-12 /usr/bin/c++ \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
