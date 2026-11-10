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

## 6. Packaging

- **RPM Package:** See `packaging/rpm/ai-reel-studio.spec`.
- **Flatpak:** See `packaging/flatpak/org.aireelstudio.AiReelStudio.json`.
- **Desktop Entry:** `data/ai-reel.desktop` installs to `/usr/share/applications/`.
- **Application Icon:** `data/icons/ai-reel.svg` installs to `/usr/share/icons/hicolor/scalable/apps/`.

