Name:           landrop
Version:        1.0.0
Release:        1%{?dist}
Summary:        Cross-Platform P2P and Hybrid Cloud File Transfer

License:        MIT
URL:            https://github.com/landrop/lan-drop
Source0:        landrop-%{version}.tar.gz

BuildRequires:  cmake >= 3.20
BuildRequires:  gcc-c++ >= 11
BuildRequires:  make
BuildRequires:  qt6-qtbase-devel
BuildRequires:  openssl-devel
BuildRequires:  desktop-file-utils

Requires:       qt6-qtbase
Requires:       qt6-qtbase-gui
Requires:       openssl-libs
Requires:       hicolor-icon-theme

%description
LAN-Drop is an ultra-fast, secure, cross-platform local network file transfer
tool. It provides peer-to-peer Wi-Fi discovery, chunked streaming, SHA-256
integrity verification, and optional TLS transport.

%prep
%autosetup -n landrop-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/lan-drop.desktop
export QT_QPA_PLATFORM=offscreen
%ctest

%files
%license README.md
%{_bindir}/lan_drop
%{_datadir}/applications/lan-drop.desktop
%{_datadir}/icons/hicolor/scalable/apps/lan-drop.svg
%dir %{_datadir}/landrop
%{_datadir}/landrop/styles.qss
%{_datadir}/landrop/lan-drop.svg

%changelog
* Fri Oct 23 2026 LAN-Drop Team <vladis@localhost> - 1.0.0-1
- Initial release of LAN-Drop Qt 6 for Fedora Linux
