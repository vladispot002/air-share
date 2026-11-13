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

### Installation & Dependency Considerations
```bash
sudo dpkg -i dist/release-v1.0.2/landrop_1.0.2_amd64.deb
sudo apt-get install -f
```

> [!NOTE]
> **Dynamic Library Dependencies on Ubuntu**:
> When compiled with WebRTC enabled (`-DLANDROP_ENABLE_WEBRTC=ON`), the resulting binary links dynamically against `libdatachannel.so.0.22`. Because `libdatachannel` is not currently distributed in the standard Ubuntu 24.04 `main` or `universe` repositories, Ubuntu and Debian users are strongly recommended to run the **Universal AppImage**, which self-contains all required Qt 6, OpenSSL, and libdatachannel shared libraries.

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
1. Source tarball preparation (`landrop-1.0.2.tar.gz`).
2. Release build and full automated test suite execution under offscreen Qt.
3. RPM build via `rpmbuild -ba packaging/fedora/landrop.spec`.
4. Generation of a portable self-contained archive (`landrop-1.0.2-linux-x86_64.tar.gz`).

### Verified Installation & Verification (Fedora 40+)
```bash
# Install local RPM
sudo dnf install -y ./dist/rpm/landrop-1.0.2-1.fc44.x86_64.rpm

# Verify installation integrity
rpm -V landrop

# Run built-in self-test diagnostics
/usr/bin/lan_drop doctor --json

# Uninstall cleanly
sudo dnf remove -y landrop
```

---

## 4. Universal Linux AppImage

The AppImage bundle contains the native Qt 6 application, standard desktop metadata, high-resolution icons, bundled `libdatachannel.so.0.22`, and an unprivileged launcher script.

### Build Command
```bash
chmod +x scripts/build-appimage.sh
./scripts/build-appimage.sh
```

### Output:
- Staged AppDir: `dist/LAN-Drop.AppDir`
- AppImage bundle: `dist/LAN-Drop-1.0.2-x86_64.AppImage`

If `appimagetool` is not installed on your system, the script stages the complete `LAN-Drop.AppDir` directory and downloads or invokes `appimagetool` automatically.

### Running the AppImage (FUSE 2 Note)
Modern distributions (Fedora 40+, Ubuntu 24.04+) ship with FUSE 3 (`libfuse.so.3`) by default, whereas legacy AppImage runtimes depend on FUSE 2 (`libfuse.so.2`).

To run the AppImage without installing extra packages:
```bash
./dist/LAN-Drop-1.0.2-x86_64.AppImage --appimage-extract-and-run
```

Or install legacy FUSE 2 support:
```bash
# Fedora / RHEL
sudo dnf install -y fuse-libs

# Ubuntu / Debian
sudo apt-get install -y libfuse2
```

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
desktop-file-validate packaging/fedora/lan-drop.desktop
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
The resulting installer will be located at `dist\windows\LAN-Drop-Setup-1.0.2.exe`.

---

## 7. Cryptographic Release Verification

All CI/CD release artifacts are validated and accompanied by `SHA256SUMS.txt`.
Users can verify their package authenticity via:
```bash
sha256sum -c SHA256SUMS.txt --ignore-missing
```

