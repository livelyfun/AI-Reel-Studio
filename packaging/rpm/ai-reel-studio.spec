Name:           ai-reel-studio
Version:        1.0.0
Release:        1%{?dist}
Summary:        Professional Native AI-Powered Short-Form Video Production Studio

License:        MIT
URL:            https://github.com/livelyfun/AI-Reel-Studio
Source0:        %{url}/archive/v%{version}/ai-reel-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  meson
BuildRequires:  ninja-build
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib
BuildRequires:  pkgconfig(gtk4)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(gio-2.0)
BuildRequires:  pkgconfig(json-glib-1.0)
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  pkgconfig(libsecret-1)
BuildRequires:  pkgconfig(libcurl)
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-video-1.0)

Requires:       /usr/bin/ffmpeg
Requires:       gstreamer1-plugins-good
Requires:       gstreamer1-plugin-gtk4
Requires:       hicolor-icon-theme

%description
AI Reel Studio is a native C17 Linux desktop studio for creating, editing,
previewing and exporting short-form social videos (Instagram Reels, YouTube
Shorts, TikTok) with multi-provider AI planning, storyboard generation,
procedural background music, Ken Burns cinematography and verified H.264/AAC
MP4 exports.

%prep
%autosetup -n ai-reel-%{version}

%build
%meson
%meson_build

%install
%meson_install

%check
%meson_test
desktop-file-validate %{buildroot}%{_datadir}/applications/ai-reel.desktop
appstream-util validate-relax --nonet %{buildroot}%{_datadir}/metainfo/org.aireelstudio.AiReelStudio.metainfo.xml

%files
%license LICENSE
%{_bindir}/ai-reel
%{_datadir}/applications/ai-reel.desktop
%{_datadir}/metainfo/org.aireelstudio.AiReelStudio.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/ai-reel.svg
%{_datadir}/ai-reel-studio/

%changelog
* Tue Sep 01 2026 Mithlesh Kumar Das <kirakeshav158@gmail.com> - 1.0.0-1
- Initial native C17/GTK4 release for Fedora Linux.

