#
# spec file for OpenFirePower: the game, and the dedicated server on its own
# (openSUSE Build Service; also builds on Fedora). License: MIT
#
Name:           openfirepower
Version:        0.1
Release:        0
Summary:        An open remake of the 1987 Amiga tank classic Fire Power
License:        MIT
Group:          Amusements/Games/Action/Other
URL:            https://openfp.retrocs.org/
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  cmake >= 3.16
BuildRequires:  gcc
BuildRequires:  pkgconfig(sdl2)
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  pkgconfig(libcurl)
BuildRequires:  systemd-rpm-macros
BuildRequires:  sysuser-tools
Requires:       %{name}-data = %{version}

%description
Drive into the enemy's fortress, take their flag and bring it home, while they try to do the
same to you. Five worlds, day, dusk and night, split screen for up to four players, a map
editor, replays, and online play with ratings, ladders and a map gallery. No files from the
original game are needed.

%package data
Summary:        Graphics, sounds and maps for OpenFirePower
BuildArch:      noarch

%description data
The art, sounds and maps OpenFirePower draws itself (all MIT).

%package server
Summary:        OpenFirePower dedicated server
Group:          Amusements/Games/Action/Other
%sysusers_requires

%description server
The dedicated server for OpenFirePower's online play: lobby, chat, matches with computer
players, map rotation, ratings and statistics (SQLite), the public server list, webhooks.
Runs as a systemd service; its settings are in /etc/ofp-server/server.cfg.

%prep
%autosetup

%build
%cmake -DOFP_DATA_DIR=%{_datadir}/%{name} -DOFP_INSTALL_SYSTEMD=ON
%cmake_build
%sysusers_generate_pre packaging/systemd/ofp-server.sysusers ofp-server ofp-server.conf

%install
%cmake_install

%check
%ctest

%pre server -f ofp-server.pre
%service_add_pre ofp-server.service

%post server
%service_add_post ofp-server.service

%preun server
%service_del_preun ofp-server.service

%postun server
%service_del_postun ofp-server.service

%files
%license LICENSE
%{_bindir}/openfirepower
%{_datadir}/applications/org.openfirepower.OpenFirePower.desktop
%{_datadir}/metainfo/org.openfirepower.OpenFirePower.metainfo.xml
%{_datadir}/icons/hicolor/256x256/apps/org.openfirepower.OpenFirePower.png

%files data
%{_datadir}/%{name}

%files server
%license LICENSE
%{_docdir}/%{name}
%{_bindir}/ofp_server
%{_bindir}/ofp_match
%{_unitdir}/ofp-server.service
%{_sysusersdir}/ofp-server.conf
%dir %{_sysconfdir}/ofp-server
%config(noreplace) %{_sysconfdir}/ofp-server/server.cfg

%changelog
