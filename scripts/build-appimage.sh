#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
APPDIR="${DIST_DIR}/LAN-Drop.AppDir"
BUILD_DIR="${ROOT_DIR}/build-appimage"
VERSION="1.0.2"

echo "=================================================="
echo "  [LAN-Drop] Building Linux AppImage & AppDir     "
echo "=================================================="

# 1. Compile LAN-Drop release binary
echo "==> [1/4] Building LAN-Drop in Release mode with WebRTC..."
cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DLANDROP_ENABLE_WEBRTC=ON \
    -DCMAKE_PREFIX_PATH="${HOME}/.local" \
    -DBUILD_TESTING=ON

cmake --build "${BUILD_DIR}" --parallel "$(nproc || echo 4)"

# Run quick verification
echo "==> Running automated test suite before packaging..."
export QT_QPA_PLATFORM=offscreen
export LD_LIBRARY_PATH="${HOME}/.local/lib64:${HOME}/.local/lib:${LD_LIBRARY_PATH:-}"
ctest --test-dir "${BUILD_DIR}" --output-on-failure

# 2. Stage AppDir structure
echo "==> [2/4] Assembling AppDir structure at ${APPDIR}..."
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" \
         "${APPDIR}/usr/share/applications" \
         "${APPDIR}/usr/share/icons/hicolor/scalable/apps" \
         "${APPDIR}/usr/share/metainfo" \
         "${APPDIR}/usr/share/landrop"

cp -f "${BUILD_DIR}/lan_drop" "${APPDIR}/usr/bin/"
cp -f "${ROOT_DIR}/resources/lan-drop.desktop" "${APPDIR}/usr/share/applications/"
cp -f "${ROOT_DIR}/resources/lan-drop.svg" "${APPDIR}/usr/share/icons/hicolor/scalable/apps/"
cp -f "${ROOT_DIR}/resources/io.landrop.LAN-Drop.metainfo.xml" "${APPDIR}/usr/share/metainfo/"
cp -f "${ROOT_DIR}/resources/styles.qss" "${APPDIR}/usr/share/landrop/"
cp -f "${ROOT_DIR}/resources/lan-drop.svg" "${APPDIR}/usr/share/landrop/"

# Root level AppDir requirements
cp -f "${ROOT_DIR}/resources/lan-drop.desktop" "${APPDIR}/lan-drop.desktop"
cp -f "${ROOT_DIR}/resources/lan-drop.svg" "${APPDIR}/lan-drop.svg"
cp -f "${ROOT_DIR}/resources/lan-drop.svg" "${APPDIR}/.DirIcon"

# Bundle Qt platform plugins and essential shared libraries
echo "==> Staging Qt 6 platform plugins and runtime libraries..."
mkdir -p "${APPDIR}/usr/lib" "${APPDIR}/usr/plugins"
QT_PLUGINS_DIR="$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || pkg-config --variable=plugindir Qt6Core 2>/dev/null || echo "")"
if [ -n "${QT_PLUGINS_DIR}" ] && [ -d "${QT_PLUGINS_DIR}/platforms" ]; then
    cp -rf "${QT_PLUGINS_DIR}/platforms" "${APPDIR}/usr/plugins/"
fi

for lib in $(ldd "${BUILD_DIR}/lan_drop" | grep "=> /" | awk '{print $3}'); do
    case "$(basename "${lib}")" in
        libQt6*|libcrypto*|libssl*|libxkbcommon*|libxcb*|libdatachannel*|libusrsctp*)
            cp -n "${lib}" "${APPDIR}/usr/lib/" 2>/dev/null || true
            ;;
    esac
done

# Ensure 64-bit WebRTC & SCTP libraries are bundled
cp -P "${HOME}/.local/lib64"/libusrsctp* "${APPDIR}/usr/lib/" 2>/dev/null || true
cp -P "${HOME}/.local/lib64"/libdatachannel* "${APPDIR}/usr/lib/" 2>/dev/null || true

# 3. Create hardened AppRun script
echo "==> [3/4] Generating AppRun launcher..."
cat > "${APPDIR}/AppRun" << 'EOF'
#!/usr/bin/env bash
set -e

HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="${HERE}/usr/bin:${HERE}/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${HERE}/usr/lib/x86_64-linux-gnu:${HERE}/lib:${LD_LIBRARY_PATH:-}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins:${QT_PLUGIN_PATH:-}"
export QML2_IMPORT_PATH="${HERE}/usr/qml:${QML2_IMPORT_PATH:-}"

EXEC="${HERE}/usr/bin/lan_drop"
if [ ! -f "${EXEC}" ]; then
    echo "Error: Binary lan_drop not found in AppImage bundle." >&2
    exit 1
fi

exec "${EXEC}" "$@"
EOF
chmod +x "${APPDIR}/AppRun"
chmod +x "${APPDIR}/usr/bin/lan_drop"

# 4. Generate .AppImage if appimagetool is available or can be obtained
echo "==> [4/4] Checking for appimagetool..."
mkdir -p "${DIST_DIR}"
OUTPUT_APPIMAGE="${DIST_DIR}/LAN-Drop-${VERSION}-x86_64.AppImage"

APPIMAGETOOL_BIN="${APPIMAGETOOL:-$(command -v appimagetool || true)}"

if [ -z "${APPIMAGETOOL_BIN}" ]; then
    TOOL_CACHE="${DIST_DIR}/tools/appimagetool-x86_64.AppImage"
    if [ -x "${TOOL_CACHE}" ]; then
        APPIMAGETOOL_BIN="${TOOL_CACHE}"
    elif command -v curl >/dev/null 2>&1; then
        echo "--> appimagetool not found on PATH. Attempting to fetch standalone appimagetool..."
        mkdir -p "${DIST_DIR}/tools"
        if curl -fsSL -o "${TOOL_CACHE}" "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" 2>/dev/null; then
            chmod +x "${TOOL_CACHE}"
            APPIMAGETOOL_BIN="${TOOL_CACHE}"
        fi
    fi
fi

if [ -n "${APPIMAGETOOL_BIN}" ] && [ -x "${APPIMAGETOOL_BIN}" ]; then
    echo "--> Packaging AppImage with ${APPIMAGETOOL_BIN}..."
    if command -v appstreamcli >/dev/null 2>&1; then
        echo "--> Verifying AppStream metadata offline..."
        appstreamcli validate --no-net "${APPDIR}/usr/share/metainfo/io.landrop.LAN-Drop.metainfo.xml"
    fi
    ARCH=x86_64 "${APPIMAGETOOL_BIN}" --appimage-extract-and-run -n "${APPDIR}" "${OUTPUT_APPIMAGE}" || \
    ARCH=x86_64 "${APPIMAGETOOL_BIN}" -n "${APPDIR}" "${OUTPUT_APPIMAGE}"
    echo "==> AppImage created successfully: ${OUTPUT_APPIMAGE}"
else
    echo "--> Note: 'appimagetool' not available. AppDir is fully staged at:"
    echo "    ${APPDIR}"
    echo "    You can convert it to .AppImage at any time using: appimagetool \"${APPDIR}\" \"${OUTPUT_APPIMAGE}\""
fi

echo "=================================================="
echo "  LAN-Drop AppImage / AppDir stage ready!"
echo "=================================================="
