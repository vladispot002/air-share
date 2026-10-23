#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "==> [LAN-Drop] Building Release configuration..."

command -v cmake >/dev/null 2>&1 || { echo "Error: cmake is required but not installed." >&2; exit 1; }
command -v c++ >/dev/null 2>&1 || { echo "Error: C++ compiler is required but not installed." >&2; exit 1; }

BUILD_DIR="${ROOT_DIR}/build-release"

mkdir -p "${BUILD_DIR}"
cd "${ROOT_DIR}"

cmake -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON

cmake --build "${BUILD_DIR}" --parallel "$(nproc || echo 4)"

echo "==> [LAN-Drop] Running test suite via CTest..."
export QT_QPA_PLATFORM=offscreen
ctest --test-dir "${BUILD_DIR}" --output-on-failure

echo "==> [LAN-Drop] Release build and tests completed successfully!"
echo "    Executable: ${BUILD_DIR}/lan_drop"
