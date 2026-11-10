# AI Reel Studio

A professional native Linux desktop application written in **C17** and **GTK4** for creating, editing, previewing, and exporting AI-powered short-form videos.

Designed for creators producing high-retention content for:
- **Instagram Reels** (9:16)
- **YouTube Shorts** (9:16)
- **TikTok** (9:16)
- **YouTube Standard Videos** (16:9)
- **Square Posts** (1:1)

---

## Key Highlights

- **100% Native Linux Architecture**: Engineered in C17 with GTK4 and GLib/GIO. Zero Node.js, zero React, zero Electron, zero browser dependencies.
- **Extensible AI Provider Ecosystem**: Add, edit, test, and switch API providers on the fly without rebuilding. Out-of-the-box support for:
  - **Google Gemini** (Native HTTP integration, text planning, structured JSON, TTS voiceover)
  - **OpenRouter** (Model catalogue, multi-model routing)
  - **Generic OpenAI-Compatible Endpoints** (Local Ollama, vLLM, LM Studio)
- **Independent AI Capability Routing**: Configure separate providers for Content Planning, Storyboard Generation, Narration (TTS), and Visual Assets.
- **Enterprise-Grade Credential Security**: API keys are securely managed via the Linux Secret Service (`libsecret`). Plaintext keys are never stored in SQLite database, project JSON, logs, or Git repositories.
- **Production Multimedia Engine**:
  - Native video preview powered by **GStreamer**.
  - High-precision Ken Burns cinematography (pan, zoom, tilts).
  - High-fidelity typography with 5 subtitle styles (TikTok Viral, Karaoke Pop, Cinematic Glow, Minimal Clean, Neon Punch).
  - Procedural background mood music synthesis with audio ducking during voiceover.
  - Final rendering to verified **H.264/AAC MP4** using **FFmpeg** and validated with **ffprobe**.
- **Persistent Local Projects**: Structured storage using SQLite (`studio.db`) with automatic schema version migrations and WAL mode.

---

## Installation & Packages

Pre-built binaries are provided on the [Releases page](https://github.com/livelyfun/AI-Reel-Studio/releases) for Fedora RPM and standalone Flatpak bundles.

### 1. Fedora Linux RPM (`.rpm`)

Download the latest `.rpm` package and install it via DNF:

```bash
sudo dnf install ./ai-reel-studio-1.0.0-1.fc44.x86_64.rpm
```

Launch the application:
```bash
ai-reel
# Or search for "AI Reel Studio" in your desktop application menu
```

### 2. Universal Flatpak Bundle (`.flatpak`)

Download the `.flatpak` bundle and install it:

```bash
flatpak install --user ./org.aireelstudio.AiReelStudio.flatpak
```

Launch the Flatpak app:
```bash
flatpak run org.aireelstudio.AiReelStudio
```

### 3. Verify Download Integrity

Verify the cryptographic SHA-256 hash of your downloaded packages:

```bash
sha256sum -c SHA256SUMS
```

---

## Building from Source

### Prerequisites (Fedora Linux)

```bash
sudo dnf install -y \
    gcc meson ninja-build pkg-config \
    gtk4-devel glib2-devel json-glib-devel sqlite-devel \
    libsecret-devel libcurl-devel gstreamer1-devel gstreamer1-plugins-base-devel \
    ffmpeg ffmpeg-free-devel
```

### Build and Run

```bash
meson setup build --buildtype=debugoptimized
ninja -C build
./build/ai-reel
```

### Run Tests

```bash
meson test -C build --verbose
```

---

## Architecture & Documentation

- [Architecture Overview](docs/ARCHITECTURE.md)
- [API Provider System](docs/API_PROVIDER_SYSTEM.md)
- [Media Pipeline & FFmpeg Integration](docs/MEDIA_PIPELINE.md)
- [Security & Credential Management](docs/SECURITY.md)
- [Database Schema & Migrations](docs/DATABASE_SCHEMA.md)
- [Migration Strategy from Web App](docs/MIGRATION.md)
- [Detailed Build & Packaging Guide](docs/BUILD.md)

---

## License

MIT License. See [LICENSE](LICENSE) for details.

