%define name openvegaseffects
%define version 0.1.0
%define release 1

Summary: Open reimplementation of a VEGAS Effects style compositing application
Name: %{name}
Version: %{version}
Release: %{release}%{?dist}
License: GPL-3.0-or-later
Group: Applications/Multimedia
Source0: %{name}-%{version}.tar.gz
URL: https://github.com/ili4yov-ika/OpenVegasEffects
BuildRequires: gcc-c++
BuildRequires: cmake >= 3.21
BuildRequires: ninja-build
BuildRequires: qt6-qtbase-devel >= 6.8.0
BuildRequires: qt6-qtsvg-devel >= 6.8.0
BuildRequires: qt6-qttools-devel >= 6.8.0
Requires: qt6-qtbase >= 6.8.0
Requires: qt6-qtsvg >= 6.8.0
# Playback and the voiceover recorder load the system libVLC.
Recommends: vlc-libs
BuildArch: x86_64

%description
OpenVegas Effects is an open reimplementation of a VEGAS Effects style
compositing application: composite shots with 2D and 3D layers, text, masks,
tracking, native .hfpl effects, transitions and Behaviors, and export.

%prep
%setup -q

%build
%cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

%files
%{_bindir}/OpenVegasEffects
%{_datadir}/applications/openvegaseffects.desktop
%{_datadir}/icons/hicolor/scalable/apps/openvegaseffects.svg
%doc README.md
%license LICENSE

%changelog
* Fri Oct 23 2026 OpenVegas Effects contributors <maintainer@example.com> - 0.1.0-1
- First package of OpenVegas Effects
