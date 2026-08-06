#!/bin/bash
# Fetch vendored dependencies at pinned revisions.
set -e
cd "$(dirname "$0")"
GPGX_COMMIT=8ae4ef7f71341c2246d36781b46480c500743f38
if [ ! -d vendor/Genesis-Plus-GX ]; then
    git clone https://github.com/ekeeke/Genesis-Plus-GX.git vendor/Genesis-Plus-GX
fi
git -C vendor/Genesis-Plus-GX fetch -q origin $GPGX_COMMIT 2>/dev/null || true
git -C vendor/Genesis-Plus-GX checkout -q $GPGX_COMMIT
echo "Genesis-Plus-GX @ $GPGX_COMMIT"
git -C vendor/Genesis-Plus-GX apply --check ../../vendor-patches/gpgx-rc-native.patch 2>/dev/null \
    && git -C vendor/Genesis-Plus-GX apply ../../vendor-patches/gpgx-rc-native.patch \
    && echo "applied gpgx-rc-native.patch" || echo "patch already applied or failed"
