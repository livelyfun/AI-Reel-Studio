/**
 * Audio Engine for AI Reel Studio
 * Handles real-time Web Audio procedural background music generation,
 * Gemini TTS narration playback, audio ducking, and master mix routing.
 */

class AudioEngine {
  private ctx: AudioContext | null = null;
  private musicGain: GainNode | null = null;
  private voiceGain: GainNode | null = null;
  private masterGain: GainNode | null = null;
  private musicInterval: any = null;
  private activeVoiceSource: AudioBufferSourceNode | null = null;
  private audioBuffersCache: Map<string, AudioBuffer> = new Map();
  private isMuted: boolean = false;
  private currentVolume: number = 0.8;
  private activeGenre: string = "Cinematic";

  public init() {
    if (this.ctx) return;
    try {
      const AudioCtxClass = window.AudioContext || (window as any).webkitAudioContext;
      this.ctx = new AudioCtxClass();

      this.masterGain = this.ctx.createGain();
      this.masterGain.gain.value = this.currentVolume;
      this.masterGain.connect(this.ctx.destination);

      this.musicGain = this.ctx.createGain();
      this.musicGain.gain.value = 0.25; // ambient level
      this.musicGain.connect(this.masterGain);

      this.voiceGain = this.ctx.createGain();
      this.voiceGain.gain.value = 1.0; // full clarity for voice
      this.voiceGain.connect(this.masterGain);
    } catch (e) {
      console.warn("Web AudioContext initialization failed:", e);
    }
  }

  public getAudioContext(): AudioContext | null {
    this.init();
    return this.ctx;
  }

  public getDestination(): MediaStreamAudioDestinationNode | null {
    this.init();
    if (!this.ctx) return null;
    const dest = this.ctx.createMediaStreamDestination();
    this.masterGain?.connect(dest);
    return dest;
  }

  public setVolume(val: number) {
    this.currentVolume = Math.max(0, Math.min(1, val));
    if (this.masterGain && this.ctx) {
      this.masterGain.gain.setValueAtTime(this.isMuted ? 0 : this.currentVolume, this.ctx.currentTime);
    }
  }

  public setMuted(muted: boolean) {
    this.isMuted = muted;
    if (this.masterGain && this.ctx) {
      this.masterGain.gain.setValueAtTime(this.isMuted ? 0 : this.currentVolume, this.ctx.currentTime);
    }
  }

  /**
   * Start procedural background music tailored to the reel mood
   */
  public startBackgroundMusic(genre: string = "Cinematic") {
    this.init();
    if (!this.ctx || !this.musicGain) return;
    if (this.ctx.state === "suspended") {
      this.ctx.resume();
    }

    this.stopBackgroundMusic();
    this.activeGenre = genre;

    // Mood frequency chords (Root, Minor 3rd/Major 3rd, 5th, Octave)
    const chordsMap: Record<string, number[][]> = {
      Cinematic: [
        [110, 130.81, 164.81, 220], // A2 minor
        [98, 123.47, 146.83, 196],   // G2 major
        [87.31, 110, 130.81, 174.61], // F2 major
        [110, 138.59, 164.81, 220], // A2
      ],
      Electronic: [
        [130.81, 164.81, 196, 261.63], // C3
        [146.83, 174.61, 220, 293.66], // D3
        [110, 130.81, 164.81, 220],    // A2
        [123.47, 146.83, 185, 246.94], // B2
      ],
      Dramatic: [
        [73.42, 87.31, 110, 146.83],   // D2 suspense
        [65.41, 77.78, 98, 130.81],    // C2 dark
        [61.74, 73.42, 92.5, 123.47],  // B1 low drone
        [73.42, 92.5, 110, 146.83],    // D2
      ],
      Ambient: [
        [174.61, 220, 261.63, 329.63], // F3 major 7
        [196, 246.94, 293.66, 392],    // G3
        [220, 261.63, 329.63, 440],    // A3 minor 7
        [174.61, 220, 261.63, 349.23], // F3
      ],
    };

    const chords = chordsMap[genre] || chordsMap.Cinematic;
    let chordIndex = 0;

    const playChord = () => {
      if (!this.ctx || !this.musicGain) return;
      const currentChord = chords[chordIndex % chords.length];
      chordIndex++;

      const now = this.ctx.currentTime;
      const chordDuration = 3.8;

      currentChord.forEach((freq, idx) => {
        if (!this.ctx || !this.musicGain) return;
        const osc = this.ctx.createOscillator();
        const gain = this.ctx.createGain();
        const filter = this.ctx.createBiquadFilter();

        osc.type = idx === 0 ? "sawtooth" : idx === 1 ? "triangle" : "sine";
        osc.frequency.setValueAtTime(freq, now);

        filter.type = "lowpass";
        filter.frequency.setValueAtTime(450 + idx * 250, now);
        filter.frequency.exponentialRampToValueAtTime(180, now + chordDuration);

        // Soft ambient envelope
        gain.gain.setValueAtTime(0.0001, now);
        gain.gain.linearRampToValueAtTime(0.06 / (idx + 1), now + 0.9);
        gain.gain.exponentialRampToValueAtTime(0.0001, now + chordDuration);

        osc.connect(filter);
        filter.connect(gain);
        gain.connect(this.musicGain);

        osc.start(now);
        osc.stop(now + chordDuration + 0.1);
      });
    };

    playChord();
    this.musicInterval = setInterval(playChord, 3800);
  }

  public stopBackgroundMusic() {
    if (this.musicInterval) {
      clearInterval(this.musicInterval);
      this.musicInterval = null;
    }
  }

  /**
   * Duck background music volume during speech
   */
  public duckMusic(isSpeaking: boolean) {
    if (!this.ctx || !this.musicGain) return;
    const now = this.ctx.currentTime;
    const targetGain = isSpeaking ? 0.08 : 0.25;
    this.musicGain.gain.cancelScheduledValues(now);
    this.musicGain.gain.linearRampToValueAtTime(targetGain, now + 0.3);
  }

  /**
   * Play single continuous narration audio from startOffset
   */
  public async playContinuousNarration(
    audioUrl?: string,
    startOffsetSeconds: number = 0
  ): Promise<void> {
    this.init();
    this.stopNarration();

    if (!audioUrl || !this.ctx) {
      this.duckMusic(false);
      return;
    }

    try {
      let buffer = this.audioBuffersCache.get(audioUrl);
      if (!buffer) {
        const response = await fetch(audioUrl);
        const arrayBuf = await response.arrayBuffer();
        buffer = await this.ctx.decodeAudioData(arrayBuf);
        this.audioBuffersCache.set(audioUrl, buffer);
      }

      if (startOffsetSeconds >= buffer.duration) {
        this.duckMusic(false);
        return;
      }

      this.duckMusic(true);
      const source = this.ctx.createBufferSource();
      source.buffer = buffer;
      source.connect(this.voiceGain!);
      this.activeVoiceSource = source;

      source.onended = () => {
        if (this.activeVoiceSource === source) {
          this.activeVoiceSource = null;
          this.duckMusic(false);
        }
      };

      const safeOffset = Math.max(0, Math.min(startOffsetSeconds, buffer.duration - 0.05));
      source.start(0, safeOffset);
    } catch (e) {
      console.warn("Failed decoding continuous narration audio:", e);
      this.duckMusic(false);
    }
  }

  /**
   * Seek continuous narration
   */
  public async seekNarration(audioUrl?: string, offsetSeconds: number = 0) {
    if (!audioUrl) return;
    await this.playContinuousNarration(audioUrl, offsetSeconds);
  }

  /**
   * Play scene-specific narration audio if available
   */
  public async playNarration(
    narrationText: string,
    audioUrl?: string,
    voiceStyle: string = "Cinematic"
  ): Promise<void> {
    this.init();
    this.stopNarration();

    if (!audioUrl) {
      this.duckMusic(false);
      return;
    }

    if (this.ctx) {
      try {
        let buffer = this.audioBuffersCache.get(audioUrl);
        if (!buffer) {
          const response = await fetch(audioUrl);
          const arrayBuf = await response.arrayBuffer();
          buffer = await this.ctx.decodeAudioData(arrayBuf);
          this.audioBuffersCache.set(audioUrl, buffer);
        }

        this.duckMusic(true);
        const source = this.ctx.createBufferSource();
        source.buffer = buffer;
        source.connect(this.voiceGain!);
        this.activeVoiceSource = source;

        source.onended = () => {
          this.duckMusic(false);
        };

        source.start();
        return;
      } catch (e) {
        console.warn("Failed decoding audio URL:", e);
        this.duckMusic(false);
      }
    }
  }

  public stopNarration() {
    if (this.activeVoiceSource) {
      try {
        this.activeVoiceSource.stop();
        this.activeVoiceSource.disconnect();
      } catch (_) {}
      this.activeVoiceSource = null;
    }
    if ("speechSynthesis" in window) {
      window.speechSynthesis.cancel();
    }
    this.duckMusic(false);
  }

  public stopAll() {
    this.stopBackgroundMusic();
    this.stopNarration();
  }
}

export const audioEngine = new AudioEngine();
