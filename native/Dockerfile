FROM ubuntu:24.04
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential zlib1g-dev python3 libsdl2-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
