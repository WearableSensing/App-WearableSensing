#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPS_DIR="${SCRIPT_DIR}/deps"

DSI_VERSION="1.21.3"
LSL_VERSION="1.17.7"
DSI_DIR="${ROOT_DIR}/vendor/dsi-api/${DSI_VERSION}"
LSL_DIR="${DEPS_DIR}/liblsl-${LSL_VERSION}"

case "$(uname -m)" in
  arm64)
    DSI_PLATFORM="Darwin-arm64"
    DSI_LIBRARY="libDSI-Darwin-arm64.dylib"
    ;;
  x86_64)
    DSI_PLATFORM="Darwin-x86_64"
    DSI_LIBRARY="libDSI-Darwin-x86_64.dylib"
    ;;
  *)
    echo "Unsupported Mac architecture: $(uname -m)" >&2
    exit 1
    ;;
esac

mkdir -p "${DEPS_DIR}"

if [[ ! -f "${DSI_DIR}/DSI.h" || ! -f "${DSI_DIR}/${DSI_LIBRARY}" ]]; then
  echo "Missing vendored DSI API v${DSI_VERSION} files in ${DSI_DIR}" >&2
  exit 1
fi

LSL_FRAMEWORK="${LSL_DIR}/lsl.xcframework/macos-arm64_x86_64/lsl.framework"
if [[ ! -f "${LSL_FRAMEWORK}/Versions/A/lsl" ]]; then
  echo "Downloading liblsl v${LSL_VERSION} universal macOS framework..."
  LSL_ZIP="${DEPS_DIR}/lsl.xcframework.${LSL_VERSION%.*}.zip"
  curl -fL \
    "https://github.com/sccn/liblsl/releases/download/v${LSL_VERSION}/lsl.xcframework.${LSL_VERSION%.*}.zip" \
    -o "${LSL_ZIP}"
  rm -rf "${LSL_DIR}"
  mkdir -p "${LSL_DIR}"
  ditto -x -k "${LSL_ZIP}" "${LSL_DIR}"
fi

DIST_DIR="${SCRIPT_DIR}/dist-$(uname -m)"
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

echo "Building adaptive-backfill dsi2lsl for $(uname -m)..."
clang \
  -std=c11 \
  -O2 \
  -Wall \
  -Wextra \
  -DDSI_PLATFORM=-${DSI_PLATFORM} \
  -I"${DSI_DIR}" \
  -I"${LSL_FRAMEWORK}/Versions/A/include" \
  "${ROOT_DIR}/CLI/dsi2lsl.c" \
  "${DSI_DIR}/DSI_API_Loader.c" \
  -F"$(dirname "${LSL_FRAMEWORK}")" \
  -framework lsl \
  -lpthread \
  -Wl,-rpath,@executable_path \
  -o "${DIST_DIR}/dsi2lsl"

cp "${DSI_DIR}/${DSI_LIBRARY}" "${DIST_DIR}/"
ditto "${LSL_FRAMEWORK}" "${DIST_DIR}/lsl.framework"

cat > "${DIST_DIR}/run-dsi2lsl.sh" <<'EOF'
#!/bin/bash
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "${HERE}"
exec ./dsi2lsl "$@"
EOF
chmod +x "${DIST_DIR}/dsi2lsl" "${DIST_DIR}/run-dsi2lsl.sh"

echo
echo "Build complete: ${DIST_DIR}"
echo "Run with:"
echo "  cd \"${DIST_DIR}\""
echo "  ./run-dsi2lsl.sh --port=/dev/cu.YOUR_DSI_PORT --lsl-stream-name=WS-default"
