#!/bin/bash
# Fetch the vendored dependencies and apply this project's patches.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
mkdir -p vendor
[ -d vendor/palm-os-sdk ] || git clone --depth 1 https://github.com/jichu4n/palm-os-sdk.git vendor/palm-os-sdk
if [ ! -d vendor/PumpkinOS ]; then
  git clone --depth 1 https://github.com/migueletto/PumpkinOS.git vendor/PumpkinOS
  git -C vendor/PumpkinOS apply "$ROOT/patches/pumpkinos.patch"
fi
[ -d .venv ] || { python3 -m venv .venv && .venv/bin/pip install --quiet capstone; }
# Browser harness for the WebAssembly build; it drives the Chrome already on
# the machine rather than downloading one.
if command -v npm >/dev/null && [ ! -d tools/node_modules ]; then
  PUPPETEER_SKIP_DOWNLOAD=1 npm --prefix tools install --silent || \
    echo "note: tools/webtest.js needs 'npm --prefix tools install'"
fi
echo "vendor ready. Now:"
echo "  .venv/bin/python tools/recomp68k.py && .venv/bin/python tools/recomparm.py"
echo "  make -C src -j6"
echo "  make -C vendor/PumpkinOS/src JOBS=10"
echo "  tools/make_app.sh"
echo "and for the browser build:"
echo "  tools/make_web.sh && tools/webserver.py 8080 build/web"
