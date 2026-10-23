#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
RPMBUILD_DIR="${DIST_DIR}/rpmbuild"
VERSION="1.0.0"

echo "=================================================="
echo "  [LAN-Drop] Packaging for Fedora Linux x86_64    "
echo "=================================================="

mkdir -p "${DIST_DIR}" "${RPMBUILD_DIR}/SOURCES" "${RPMBUILD_DIR}/SPECS" "${RPMBUILD_DIR}/RPMS" "${RPMBUILD_DIR}/SRPMS" "${RPMBUILD_DIR}/BUILD" "${RPMBUILD_DIR}/BUILDROOT"

echo "==> [1/4] Preparing source archive landrop-${VERSION}.tar.gz..."
tar --exclude-vcs \
    --exclude='./build*' \
    --exclude='./dist*' \
    --exclude='./obj*' \
    --exclude='./bin*' \
    --exclude='./lib*' \
    --exclude='./node_modules*' \
    --exclude='./Design*' \
    --exclude='./downloads*' \
    --exclude='./shared_drop*' \
    --transform "s,^\.,landrop-${VERSION}," \
    -czf "${DIST_DIR}/landrop-${VERSION}.tar.gz" -C "${ROOT_DIR}" .

cp -f "${DIST_DIR}/landrop-${VERSION}.tar.gz" "${RPMBUILD_DIR}/SOURCES/"
cp -f "${ROOT_DIR}/packaging/fedora/landrop.spec" "${RPMBUILD_DIR}/SPECS/"

echo "==> [2/4] Building portable Linux binary archive..."
BUILD_DIR="${ROOT_DIR}/build-package"
cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "${BUILD_DIR}" --target lan_drop --parallel "$(nproc || echo 4)"

PORTABLE_STAGING="${DIST_DIR}/landrop-${VERSION}-linux-x86_64"
rm -rf "${PORTABLE_STAGING}"
mkdir -p "${PORTABLE_STAGING}/bin" "${PORTABLE_STAGING}/share/applications" "${PORTABLE_STAGING}/share/icons/hicolor/scalable/apps" "${PORTABLE_STAGING}/share/landrop"

cp -f "${BUILD_DIR}/lan_drop" "${PORTABLE_STAGING}/bin/"
cp -f "${ROOT_DIR}/resources/lan-drop.desktop" "${PORTABLE_STAGING}/share/applications/"
cp -f "${ROOT_DIR}/resources/lan-drop.svg" "${PORTABLE_STAGING}/share/icons/hicolor/scalable/apps/"
cp -f "${ROOT_DIR}/resources/styles.qss" "${PORTABLE_STAGING}/share/landrop/"
cp -f "${ROOT_DIR}/resources/lan-drop.svg" "${PORTABLE_STAGING}/share/landrop/"
cp -f "${ROOT_DIR}/README.md" "${PORTABLE_STAGING}/"

cat > "${PORTABLE_STAGING}/run-landrop.sh" << 'EOF'
#!/usr/bin/env bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="${SCRIPT_DIR}/bin:${PATH}"
exec "${SCRIPT_DIR}/bin/lan_drop" "$@"
EOF
chmod +x "${PORTABLE_STAGING}/run-landrop.sh" "${PORTABLE_STAGING}/bin/lan_drop"

tar -czf "${DIST_DIR}/landrop-${VERSION}-linux-x86_64.tar.gz" -C "${DIST_DIR}" "landrop-${VERSION}-linux-x86_64"
echo "    Created: ${DIST_DIR}/landrop-${VERSION}-linux-x86_64.tar.gz"

echo "==> [3/4] Building Fedora RPM package..."
if command -v rpmbuild >/dev/null 2>&1; then
    echo "    Using local rpmbuild..."
    rpmbuild --define "_topdir ${RPMBUILD_DIR}" -ba "${RPMBUILD_DIR}/SPECS/landrop.spec"
elif command -v podman >/dev/null 2>&1; then
    echo "    Local rpmbuild not found. Using rootless Podman (registry.fedoraproject.org/fedora:44)..."
    podman run --rm \
        -v "${DIST_DIR}:/dist:z" \
        -v "${ROOT_DIR}/packaging/fedora/landrop.spec:/landrop.spec:z" \
        registry.fedoraproject.org/fedora:44 bash -c '
            set -euo pipefail
            echo "--> Installing build tools in container..."
            dnf install -y --setopt=install_weak_deps=False \
                cmake gcc-c++ make qt6-qtbase-devel openssl-devel desktop-file-utils rpm-build rpmdevtools
            echo "--> Running rpmbuild..."
            mkdir -p /root/rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
            cp /dist/landrop-1.0.0.tar.gz /root/rpmbuild/SOURCES/
            rpmbuild -ba /landrop.spec
            cp -rf /root/rpmbuild/RPMS/* /dist/rpmbuild/RPMS/
            cp -rf /root/rpmbuild/SRPMS/* /dist/rpmbuild/SRPMS/
        '
elif command -v docker >/dev/null 2>&1; then
    echo "    Local rpmbuild not found. Using Docker..."
    docker run --rm \
        -v "${DIST_DIR}:/dist" \
        -v "${ROOT_DIR}/packaging/fedora/landrop.spec:/landrop.spec" \
        registry.fedoraproject.org/fedora:44 bash -c '
            set -euo pipefail
            dnf install -y --setopt=install_weak_deps=False \
                cmake gcc-c++ make qt6-qtbase-devel openssl-devel desktop-file-utils rpm-build rpmdevtools
            mkdir -p /root/rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
            cp /dist/landrop-1.0.0.tar.gz /root/rpmbuild/SOURCES/
            rpmbuild -ba /landrop.spec
            cp -rf /root/rpmbuild/RPMS/* /dist/rpmbuild/RPMS/
            cp -rf /root/rpmbuild/SRPMS/* /dist/rpmbuild/SRPMS/
        '
else
    echo "Warning: Neither rpmbuild, podman, nor docker is available to build RPM locally." >&2
    echo "To build RPM directly on Fedora, run: sudo dnf install -y rpm-build rpmdevtools" >&2
fi

echo "==> [4/4] Collecting packaged artifacts..."
mkdir -p "${DIST_DIR}/rpm"
if [ -d "${RPMBUILD_DIR}/RPMS" ]; then
    find "${RPMBUILD_DIR}/RPMS" -name "*.rpm" -exec cp -f {} "${DIST_DIR}/rpm/" \;
fi

echo "=================================================="
echo "  [LAN-Drop] Packaging Summary:"
echo "=================================================="
ls -lh "${DIST_DIR}"/*.tar.gz 2>/dev/null || true
ls -lh "${DIST_DIR}/rpm"/*.rpm 2>/dev/null || true
echo "Done!"
