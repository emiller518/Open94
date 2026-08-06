#!/bin/bash
# Build the headless harness inside the nhl94-dev docker image.
set -e
cd "$(dirname "$0")/.."
docker run --rm -v "$PWD":/work -w /work/native/harness nhl94-dev \
    bash -ec "make -j\$(nproc) $* && chown -R $(id -u):$(id -g) build harness 2>/dev/null || true"
