Name:           ai-reel-studio
Version:        1.0.0
Release:        1%{?dist}
Summary:        Professional Native AI-Powered Short-Form Video Production Studio

License:        MIT
URL:            https://github.com/livelyfun/AI-Reel-Studio
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  meson
BuildRequires:  ninja-build
BuildRequires:  pkgconfig(gtk4)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(gio-2.0)
BuildRequires:  pkgconfig(json-glib-1.0)
BuildRequires:  pkgconfig(sqlite3)
BuildRequires:  pkgconfig(libsecret-1)
BuildRequires:  pkgconfig(libcurl)
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-video-1.0)

Requires:       gtk4
Requires:       glib2
Requires:       json-glib
Requires:       sqlite-libs
Requires:       libsecret
Requires:       libcurl
Requires:       ffmpeg
Requires:       gstreamer1
Requires:       gstreamer1-plugins-good
Requires:       gstreamer1-plugin-gtk4

%description
AI Reel Studio is a native C17 Linux desktop studio for creating, editing,
previewing and exporting short-form social videos (Instagram Reels, YouTube
Shorts, TikTok) with multi-provider AI planning, storyboard generation,
procedural background music, Ken Burns cinematography and verified H.264/AAC
MP4 exports.

%prep
%autosetup

%build
%meson
%meson_build

%install
%meson_install

%check
%meson_test

%files
%{_bindir}/ai-reel
%{_datadir}/applications/ai-reel.desktop
%{_datadir}/icons/hicolor/scalable/apps/ai-reel.svg
%{_datadir}/ai-reel-studio/

%changelog
* Tue Sep 01 2026 Mithlesh Kumar Das <kirakeshav158@gmail.com> - 1.0.0-1
- Initial native C17/GTK4 release for Fedora Linux.

