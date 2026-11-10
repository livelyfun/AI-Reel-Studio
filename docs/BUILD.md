# Building AI Reel Studio on Fedora Linux

## 1. System Requirements & Dependencies

AI Reel Studio is a native Linux desktop application written in **C17** using **GTK4**.
The primary target distribution is **Fedora Linux** (Fedora 40, 41, 42, 44+).

### Required Development Packages

On Fedora, install the development dependencies using `dnf`:

```bash
sudo dnf install -y \
    gcc \
    meson \
    ninja-build \
    pkg-config \
    gtk4-devel \
    glib2-devel \
    json-glib-devel \
    sqlite-devel \
    libsecret-devel \
    libcurl-devel \
    gstreamer1-devel \
    gstreamer1-plugins-base-devel \
    ffmpeg \
    ffmpeg-free-devel
```

### Runtime Multimedia Dependencies

In addition to the core libraries, ensure multimedia tools and GStreamer plugins are installed for video playback and rendering:

```bash
sudo dnf install -y \
    ffmpeg \
    gstreamer1-plugins-good \
    gstreamer1-plugins-bad-free \
    gstreamer1-plugin-gtk4
```

---

## 2. Building from Source

### Standard Meson Build

1. **Configure the build directory:**

```bash
meson setup build --buildtype=debugoptimized
```

2. **Compile the application:**

```bash
ninja -C build
```

3. **Run automated test suite:**

```bash
meson test -C build --verbose
```

4. **Install system-wide (optional):**

```bash
sudo ninja -C build install
```

---

## 3. Running AI Reel Studio

To launch the native desktop application directly from the build directory:

```bash
./build/ai-reel
```

Or once installed:

```bash
ai-reel
```

---

## 4. Building with Containerized Environment (Rootless Podman)

If building in an isolated container on Fedora:

```bash
# Start container with host workspace mounted:
podman run -d --name aireel-build \
    -v $(pwd):/workspace:z \
    -w /workspace \
    registry.fedoraproject.org/fedora:44 \
    sleep infinity

# Install build dependencies inside container:
podman exec aireel-build dnf install -y \
    gcc meson ninja-build pkg-config \
    gtk4-devel glib2-devel json-glib-devel sqlite-devel \
    libsecret-devel libcurl-devel gstreamer1-devel gstreamer1-plugins-base-devel

# Build inside container:
podman exec aireel-build meson setup build
podman exec aireel-build ninja -C build
```

---

## 5. Development & Debugging Options

Meson build options:

- **AddressSanitizer (ASan):**
  ```bash
  meson setup build-asan -Db_sanitize=address,undefined
  ninja -C build-asan
  ```

- **Release Build:**
  ```bash
  meson setup build-release --buildtype=release --strip
  ninja -C build-release
  ```

- **Verbose Logging:**
  Run with GLib and AI Reel Studio debug output:
  ```bash
  G_MESSAGES_DEBUG=all ./build/ai-reel
  ```

---

## 6. Packaging & Distribution

### 6.1 Building the Fedora RPM Package

To build the native `.rpm` package for Fedora Linux:

1. **Install RPM build tools:**
   ```bash
   sudo dnf install -y rpm-build rpmlint desktop-file-utils libappstream-glib
   ```

2. **Generate distribution archive:**
   ```bash
   meson setup build_dist
   meson dist -C build_dist --formats=gztar
   ```

3. **Stage archive and spec file:**
   ```bash
   mkdir -p ~/rpmbuild/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
   cp build_dist/meson-dist/ai-reel-1.0.0.tar.gz ~/rpmbuild/SOURCES/
   cp packaging/rpm/ai-reel-studio.spec ~/rpmbuild/SPECS/
   ```

4. **Execute rpmbuild:**
   ```bash
   rpmbuild -ba ~/rpmbuild/SPECS/ai-reel-studio.spec
   ```

5. **Lint and verify the package:**
   ```bash
   rpmlint ~/rpmbuild/SPECS/ai-reel-studio.spec
   rpmlint ~/rpmbuild/RPMS/x86_64/ai-reel-studio-1.0.0-*.rpm
   ```

6. **Install and test the generated RPM:**
   ```bash
   sudo dnf install ~/rpmbuild/RPMS/x86_64/ai-reel-studio-1.0.0-1.fc44.x86_64.rpm
   ai-reel --help
   ```

---

### 6.2 Building the Flatpak Application Bundle

The Flatpak build uses GNOME Platform 47 runtime and SDK with sandboxed FFmpeg and codec support.

1. **Install Flatpak and Flatpak Builder:**
   ```bash
   sudo dnf install -y flatpak flatpak-builder
   ```

2. **Add Flathub remote (if not already added):**
   ```bash
   flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
   ```

3. **Install the GNOME 47 SDK and Platform:**
   ```bash
   flatpak install -y flathub org.gnome.Platform//47 org.gnome.Sdk//47
   ```

4. **Build and install locally:**
   ```bash
   flatpak-builder --user --install --force-clean build-flatpak packaging/flatpak/org.aireelstudio.AiReelStudio.json
   ```

5. **Create a standalone `.flatpak` single-file bundle:**
   ```bash
   flatpak-builder --repo=repo --force-clean build-flatpak packaging/flatpak/org.aireelstudio.AiReelStudio.json
   flatpak build-bundle repo org.aireelstudio.AiReelStudio.flatpak org.aireelstudio.AiReelStudio
   ```

6. **Run the Flatpak application:**
   ```bash
   flatpak run org.aireelstudio.AiReelStudio
   ```

---

### 6.3 Desktop and AppStream Metadata

- **Desktop Entry:** `data/ai-reel.desktop` (validated via `desktop-file-validate`).
- **AppStream Metainfo:** `data/org.aireelstudio.AiReelStudio.metainfo.xml` (validated via `appstream-util validate-relax --nonet` and `appstreamcli validate --no-net`).
- **Application Icon:** `data/icons/ai-reel.svg` (installed to `hicolor` scalable theme).
- **License File:** `LICENSE` (MIT license installed to `%license` directory).

