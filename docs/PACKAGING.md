# LAN-Drop Packaging & Distribution Guide

LAN-Drop supports native production packaging across all major desktop and server operating systems.

---

## 1. Supported Packaging Targets

| Distribution / OS | Package Format | Toolchain / Generator | Output Location |
| :--- | :--- | :--- | :--- |
| **Debian / Ubuntu / Mint** | `.deb` | CPack (`CPackDeb`) | `build/lan_drop_<version>_<arch>.deb` |
| **Fedora / RHEL / CentOS** | `.rpm` | `rpmbuild` + `landrop.spec` | `dist/rpm/*.rpm` |
| **Generic Linux (Portable)** | `.tar.gz` | CMake / CPack | `dist/landrop-<version>-linux-x86_64.tar.gz` |
| **Universal Linux** | `.AppImage` | AppDir + `appimagetool` | `dist/LAN-Drop-<version>-x86_64.AppImage` |
| **Windows 10/11 x64** | `.exe` (Installer) | Inno Setup 6 + PowerShell | `dist/windows/LAN-Drop-Setup-<version>.exe` |

---

## 2. Debian & Ubuntu Packaging (`.deb`)

LAN-Drop utilizes CMake's built-in CPack Debian generator with automatic shared library dependency resolution (`CPACK_DEBIAN_PACKAGE_SHLIBDEPS`).

### Prerequisites
```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build qt6-base-dev qt6-qpa-plugins libssl-dev libxkbcommon-dev libgl1-mesa-dev
```

### Build & Package Command
```bash
# 1. Configure in Release mode
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DLANDROP_ENABLE_WEBRTC=ON

# 2. Build the binaries and run verification
cmake --build build
export QT_QPA_PLATFORM=offscreen
ctest --test-dir build --output-on-failure

# 3. Generate the Debian package
cd build
cpack -G DEB
```

### Installation
```bash
sudo dpkg -i lan_drop_*.deb
sudo apt-get install -f # resolve any missing dependencies if needed
```

---

## 3. Fedora & Enterprise Linux Packaging (`.rpm`)

RPM packaging is governed by `packaging/fedora/landrop.spec` and automated via `scripts/package-fedora.sh`.

### Prerequisites
```bash
sudo dnf install -y cmake gcc-c++ make qt6-qtbase-devel openssl-devel \
    desktop-file-utils rpm-build rpmdevtools git tar
```

### Build & Package Command
```bash
chmod +x scripts/package-fedora.sh
./scripts/package-fedora.sh
```

The script performs:
1. Source tarball preparation (`landrop-<version>.tar.gz`).
2. Release build and full automated test suite execution under offscreen Qt.
3. RPM build via `rpmbuild -ba packaging/fedora/landrop.spec`.
4. Generation of a portable self-contained archive (`landrop-<version>-linux-x86_64.tar.gz`).

---

## 4. Universal Linux AppImage

The AppImage bundle contains the native Qt 6 application, standard desktop metadata, high-resolution icons, and an unprivileged launcher script.

### Build Command
```bash
chmod +x scripts/build-appimage.sh
./scripts/build-appimage.sh
```

### Output:
- Staged AppDir: `dist/LAN-Drop.AppDir`
- AppImage bundle: `dist/LAN-Drop-1.0.1-x86_64.AppImage`

If `appimagetool` is not installed on your system, the script stages the complete `LAN-Drop.AppDir` directory and downloads or invokes `appimagetool` automatically.

---

## 5. FreeDesktop AppStream Metadata

LAN-Drop provides FreeDesktop AppStream 1.0 metadata conforming to modern software centers (GNOME Software, KDE Discover, Flathub):
- **Metadata File**: `resources/io.landrop.LAN-Drop.metainfo.xml`
- **Installation Path**: `${CMAKE_INSTALL_DATADIR}/metainfo/io.landrop.LAN-Drop.metainfo.xml`
- **Specification Features**:
  - Desktop Component ID: `io.landrop.LAN-Drop`
  - Launchable desktop entry: `lan-drop.desktop`
  - Content Rating: OARS 1.1 (all zero - privacy preserving, zero telemetry)
  - Semantic release changelogs

### Validating Metadata:
```bash
appstreamcli validate resources/io.landrop.LAN-Drop.metainfo.xml
```

---

## 6. Windows 10/11 Packaging

Windows builds produce a signed, single-file Inno Setup installer.

### Prerequisites (Windows)
- Visual Studio 2022 / Build Tools (C++20 MSVC)
- Qt 6.7+ via `install-qt-action` or official Qt online installer
- OpenSSL via `vcpkg install openssl:x64-windows`
- Inno Setup 6 (`choco install innosetup -y`)

### Build Command (PowerShell)
```powershell
pwsh scripts/package-windows.ps1 -RunTests
```
The resulting installer will be located at `dist\windows\LAN-Drop-Setup-1.0.1.exe`.

---

## 7. Cryptographic Release Verification

All CI/CD release artifacts are validated and accompanied by `SHA256SUMS.txt`.
Users can verify their package authenticity via:
```bash
sha256sum -c SHA256SUMS.txt --ignore-missing
```
