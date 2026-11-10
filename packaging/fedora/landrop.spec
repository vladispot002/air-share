Name:           landrop
Version:        1.0.2
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
%{_datadir}/metainfo/io.landrop.LAN-Drop.metainfo.xml
%dir %{_datadir}/landrop
%{_datadir}/landrop/styles.qss
%{_datadir}/landrop/lan-drop.svg

%post
/bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :
update-desktop-database &> /dev/null || :

%postun
update-desktop-database &> /dev/null || :
if [ $1 -eq 0 ] ; then
    /bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null
    gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
fi

%changelog
* Wed Nov 11 2026 LAN-Drop Team <vladis@localhost> - 1.0.2-1
- WireProtocol v2 framing, real WebRTC DataChannel integration, and production security hardening

* Fri Oct 23 2026 LAN-Drop Team <vladis@localhost> - 1.0.1-1
- Fix desktop application launcher, path permissions, and release packaging

* Fri Oct 23 2026 LAN-Drop Team <vladis@localhost> - 1.0.0-1
- Initial release of LAN-Drop Qt 6 for Fedora Linux
