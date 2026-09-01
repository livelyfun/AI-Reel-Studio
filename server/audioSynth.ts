/**
 * Server-side audio synthesis utilities for background music and voice fallback
 */

export interface BackgroundMusicTrack {
  id: string;
  name: string;
  genre: string;
  tempoBpm: number;
  mood: string;
}

export const PRESET_AUDIO_TRACKS: BackgroundMusicTrack[] = [
  { id: "cinematic_drone", name: "Deep Cosmic Atmosphere", genre: "Cinematic", tempoBpm: 90, mood: "Awe & Mystery" },
  { id: "electronic_pulse", name: "Cyber Tech Pulse", genre: "Electronic", tempoBpm: 120, mood: "Energetic & Fast" },
  { id: "dramatic_strings", name: "Dramatic Tension Swell", genre: "Orchestral", tempoBpm: 80, mood: "Suspenseful" },
  { id: "ambient_calm", name: "Warm Ambient Reflection", genre: "Ambient", tempoBpm: 72, mood: "Calm & Reflective" },
  { id: "motivational_rise", name: "Cinematic Horizon Rise", genre: "Motivational", tempoBpm: 110, mood: "Inspiring" },
];

export function getRecommendedMusic(voiceStyle: string, visualStyle: string): BackgroundMusicTrack {
  if (voiceStyle === "Energetic" || visualStyle === "Fast-Paced Social Media") {
    return PRESET_AUDIO_TRACKS[1];
  }
  if (voiceStyle === "Dramatic" || visualStyle === "Dark / Mystery") {
    return PRESET_AUDIO_TRACKS[2];
  }
  if (voiceStyle === "Calm" || voiceStyle === "Friendly" || visualStyle === "Minimal") {
    return PRESET_AUDIO_TRACKS[3];
  }
  if (voiceStyle === "Motivational") {
    return PRESET_AUDIO_TRACKS[4];
  }
  return PRESET_AUDIO_TRACKS[0];
}
