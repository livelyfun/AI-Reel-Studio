/**
 * Core type definitions for AI Reel Studio
 */

export type VoiceStyle = 
  | 'Cinematic'
  | 'Deep'
  | 'Energetic'
  | 'Calm'
  | 'Documentary'
  | 'Storytelling'
  | 'Motivational'
  | 'News'
  | 'Friendly'
  | 'Dramatic';

export type VisualStyle = 
  | 'Cinematic'
  | 'Photorealistic'
  | 'Anime'
  | '3D Animation'
  | 'Documentary'
  | 'Sci-Fi'
  | 'Fantasy'
  | 'Dark / Mystery'
  | 'Educational'
  | 'Fast-Paced Social Media'
  | 'Minimal'
  | 'Custom';

export type ReelDuration = 15 | 30 | 45 | 60;

export type AspectRatio = '9:16' | '16:9' | '1:1';

export type CaptionStyle = 
  | 'tiktok_bold' 
  | 'karaoke_pop' 
  | 'cinematic_glow' 
  | 'minimalist_clean' 
  | 'neon_punch';

export interface ReelRequest {
  prompt: string;
  voiceStyle: VoiceStyle;
  visualStyle: VisualStyle;
  duration: ReelDuration;
  aspectRatio: AspectRatio;
}

export interface VisualBible {
  characterAppearance?: string;
  clothing?: string;
  environment: string;
  locations: string;
  objects: string;
  visualStyle: string;
  lighting: string;
  colorPalette: string[];
  cameraStyle: string;
  overallAtmosphere: string;
}

export interface WordTiming {
  word: string;
  start: number; // in seconds relative to scene
  end: number; // in seconds relative to scene
  isKeyword?: boolean;
}

export interface StoryboardScene {
  scene_id: number;
  duration: number; // seconds
  narration: string;
  visual_description: string;
  visual_prompt: string;
  camera: string; // e.g. "Slow pan right", "Dramatic zoom in", "Macro close-up"
  lighting: string;
  atmosphere?: string;
  transition: 'crossfade' | 'zoom-in' | 'pan-left' | 'glitch' | 'fade-black' | 'wipe';
  on_screen_text: string;
  subtitle_text?: string;
  keywords: string[];
  imageUrl?: string;
  audioUrl?: string;
  wordTimings: WordTiming[];
  isRegenerating?: boolean;
}

export interface YouTubeMetadata {
  reelName: string;
  youtubeTitle: string;
  youtubeDescription: string;
  tags: string[];
  hashtags: string[];
  hookAnalysis?: string;
  audienceFit?: string;
}

export interface ContentPlan {
  topic: string;
  purpose: string;
  targetAudience: string;
  tone: string;
  storyStructure: string;
  hook: string;
  ending: string;
  callToAction: string;
  pacing: 'Fast & Punchy' | 'Dynamic' | 'Cinematic & Measured' | 'Slow & Dramatic';
  sceneCount: number;
  visualBible: VisualBible;
  estimatedTotalDuration: number;
}

export type PipelineStepStatus = 'pending' | 'in_progress' | 'completed' | 'failed' | 'retrying';

export interface PipelineStep {
  id: string;
  label: string;
  detail: string;
  status: PipelineStepStatus;
}

export interface GeneratedReel {
  id: string;
  createdAt: string;
  request: ReelRequest;
  contentPlan: ContentPlan;
  storyboard: StoryboardScene[];
  metadata: YouTubeMetadata;
  totalDuration: number;
  backgroundMusicGenre: string;
  thumbnailUrl?: string;
  captionStyle: CaptionStyle;
  fullNarrationText?: string;
  fullNarrationAudioUrl?: string;
  ttsStatus?: {
    success: boolean;
    quotaExceeded?: boolean;
    isDailyQuota?: boolean;
    is503HighDemand?: boolean;
    message?: string;
    retryAfter?: string;
    fromCache?: boolean;
  };
}
