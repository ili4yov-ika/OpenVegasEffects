%define name openvegas
%define version 0.1.0
%define release 1

Summary: Open-source nonlinear video editor (VEGAS Pro–like)
Name: %{name}
Version: %{version}
Release: %{release}%{?dist}
License: GPL-3.0+
Group: Applications/Multimedia
Source0: %{name}-%{version}.tar.gz
URL: https://github.com/ili4yov-ika/OpenVegas
BuildRequires: gcc-c++
BuildRequires: make
BuildRequires: cmake >= 3.16
BuildRequires: qt6-qtbase-devel
BuildRequires: qt6-qtsvg-devel
Requires: qt6-qtbase >= 6.8.0
Requires: qt6-qtsvg >= 6.8.0
BuildArch: x86_64

%description
OpenVegas — открытый кроссплатформенный видеоредактор (NLE),
ориентированный на workspace VEGAS Pro 22. Лицензия GNU GPL.

Основные возможности (MVP):
- Оболочка UI: media bin, preview, timeline, Master Bus
- Открытие проектов .veg (VegReader v0)
- Диалоги Welcome, Properties, Preferences, Trimmer

%prep
%setup -q

%build
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=%{_prefix} \
      ..
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
cd build
make install DESTDIR=%{buildroot}

%files
%defattr(-,root,root,-)
%{_bindir}/OpenVegas
%{_datadir}/applications/openvegas.desktop
# CMakeLists ставит и scalable/openvegas.svg, и 256x256/openvegas.png — глоб
# обязан покрывать оба, иначе rpmbuild падает на "installed (but unpackaged) file".
%{_datadir}/icons/hicolor/*/apps/openvegas.*
%doc README.md
%license LICENSE

%changelog
* Wed Jul 29 2026 OpenVegas contributors <maintainer@example.com> - 0.1.0-1
- Первый релиз OpenVegas (адаптация упаковки)
- MVP оболочка и VegReader v0
