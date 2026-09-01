import React, { useState } from "react";
import { Sparkles, Mic, Palette, Clock, Smartphone, Monitor, Square, Wand2 } from "lucide-react";
import { ReelRequest, VoiceStyle, VisualStyle, ReelDuration, AspectRatio } from "../types";

interface ReelCreatorFormProps {
  onGenerate: (request: ReelRequest) => void;
  isGenerating: boolean;
}

const VOICE_STYLES: { id: VoiceStyle; label: string; desc: string; icon: string }[] = [
  { id: "Cinematic", label: "Cinematic", desc: "Epic, atmospheric movie narrator", icon: "🎬" },
  { id: "Deep", label: "Deep", desc: "Low resonant, authoritative baritone", icon: "🎙️" },
  { id: "Energetic", label: "Energetic", desc: "High octane, fast-paced retention voice", icon: "⚡" },
  { id: "Dramatic", label: "Dramatic", desc: "Suspenseful, emotionally charged pacing", icon: "🎭" },
  { id: "Storytelling", label: "Storytelling", desc: "Engaging campfire style narrative", icon: "📖" },
  { id: "Documentary", label: "Documentary", desc: "Clear, prestigious science/nature tone", icon: "🌍" },
  { id: "Motivational", label: "Motivational", desc: "Inspiring, powerful and uplifting", icon: "🔥" },
  { id: "Calm", label: "Calm", desc: "Soothing, relaxed, meditation-like", icon: "🍃" },
  { id: "Friendly", label: "Friendly", desc: "Warm, conversational buddy tone", icon: "☕" },
  { id: "News", label: "News", desc: "Direct, objective broadcast delivery", icon: "📰" },
];

const VISUAL_STYLES: { id: VisualStyle; label: string; desc: string }[] = [
  { id: "Cinematic", label: "Cinematic", desc: "Anamorphic lens flares, rich color grading, film grain" },
  { id: "Photorealistic", label: "Photorealistic", desc: "Ultra-sharp 8k realism with natural lighting" },
  { id: "Sci-Fi", label: "Sci-Fi", desc: "Futuristic neon, cyber holograms, cosmic deep space" },
  { id: "Dark / Mystery", label: "Dark / Mystery", desc: "Moody volumetric shadows and noir rim lighting" },
  { id: "Documentary", label: "Documentary", desc: "National Geographic realism with cinematic framing" },
  { id: "3D Animation", label: "3D Animation", desc: "Pixar-grade stylized 3D renders with soft depth" },
  { id: "Anime", label: "Anime", desc: "Vibrant hand-drawn Japanese animation aesthetic" },
  { id: "Fantasy", label: "Fantasy", desc: "Ethereal enchanted lighting and magical elements" },
  { id: "Educational", label: "Educational", desc: "Clean infographic visuals with bold focal elements" },
  { id: "Fast-Paced Social Media", label: "Fast-Paced Social Media", desc: "High contrast, bold viral color grading" },
  { id: "Minimal", label: "Minimal", desc: "Clean negative space with elegant focus" },
  { id: "Custom", label: "Custom", desc: "Tailored visual palette based on your description" },
];

const SAMPLE_PROMPTS = [
  {
    title: "Black Hole Mystery",
    prompt: "Create a cinematic 45-second reel explaining why black holes are so mysterious. Make it exciting, dramatic and easy for teenagers to understand.",
    voice: "Cinematic" as VoiceStyle,
    style: "Sci-Fi" as VisualStyle,
    duration: 45 as ReelDuration,
  },
  {
    title: "How Pyramids Were Built",
    prompt: "A gripping 30-second reel revealing the lost engineering secret of the Great Pyramid of Giza. Start with a shocking fact.",
    voice: "Documentary" as VoiceStyle,
    style: "Photorealistic" as VisualStyle,
    duration: 30 as ReelDuration,
  },
  {
    title: "The $100B AI Chip War",
    prompt: "An energetic 30-second breakdown of why silicon microchips are the new oil of the 21st century. Fast-paced and high retention.",
    voice: "Energetic" as VoiceStyle,
    style: "Fast-Paced Social Media" as VisualStyle,
    duration: 30 as ReelDuration,
  },
  {
    title: "Why You Procrastinate",
    prompt: "A psychological 30-second reel explaining the battle between your amygdala and prefrontal cortex when you delay work. Offer one instant fix.",
    voice: "Storytelling" as VoiceStyle,
    style: "3D Animation" as VisualStyle,
    duration: 30 as ReelDuration,
  },
];

export const ReelCreatorForm: React.FC<ReelCreatorFormProps> = ({ onGenerate, isGenerating }) => {
  const [prompt, setPrompt] = useState("");
  const [voiceStyle, setVoiceStyle] = useState<VoiceStyle>("Cinematic");
  const [visualStyle, setVisualStyle] = useState<VisualStyle>("Cinematic");
  const [duration, setDuration] = useState<ReelDuration>(30);
  const [aspectRatio, setAspectRatio] = useState<AspectRatio>("9:16");

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    if (!prompt.trim() || isGenerating) return;
    onGenerate({
      prompt: prompt.trim(),
      voiceStyle,
      visualStyle,
      duration,
      aspectRatio,
    });
  };

  const applySample = (sample: typeof SAMPLE_PROMPTS[0]) => {
    setPrompt(sample.prompt);
    setVoiceStyle(sample.voice);
    setVisualStyle(sample.style);
    setDuration(sample.duration);
  };

  return (
    <div className="w-full max-w-4xl mx-auto py-8 px-4 sm:px-6">
      {/* Hero Badge */}
      <div className="text-center mb-8">
        <div className="inline-flex items-center gap-2 px-4 py-1.5 rounded-full bg-rose-500/10 border border-rose-500/20 text-rose-300 text-xs font-semibold uppercase tracking-wider mb-4">
          <Sparkles className="w-3.5 h-3.5" />
          <span>Fully Automated Gemini Video Creator</span>
        </div>
        <h1 className="text-3xl sm:text-4xl md:text-5xl font-black text-white tracking-tight mb-3">
          Create Your Next Viral Reel
        </h1>
        <p className="text-base sm:text-lg text-slate-400 max-w-2xl mx-auto">
          Enter one description. Gemini Free automatically handles scriptwriting, royalty-free visual matching, Gemini Free TTS voiceover, dynamic captions, and video assembly.
        </p>
      </div>

      <form onSubmit={handleSubmit} className="space-y-6 bg-slate-900/90 border border-slate-800 rounded-3xl p-6 sm:p-8 shadow-2xl backdrop-blur-xl">
        {/* 1. Main Description */}
        <div>
          <div className="flex items-center justify-between mb-2">
            <label htmlFor="reel-prompt-input" className="text-sm font-bold uppercase tracking-wider text-slate-300 flex items-center gap-2">
              <Wand2 className="w-4 h-4 text-rose-400" />
              <span>Describe the reel you want to create</span>
            </label>
            <span className="text-xs text-slate-500">Natural language prompt</span>
          </div>

          <div className="relative">
            <textarea
              id="reel-prompt-input"
              value={prompt}
              onChange={(e) => setPrompt(e.target.value)}
              placeholder="e.g. Create a cinematic 45-second reel explaining why black holes are so mysterious. Make it exciting, dramatic and easy for teenagers to understand."
              rows={4}
              required
              disabled={isGenerating}
              className="w-full px-4 py-3.5 bg-slate-950/80 border border-slate-800 focus:border-rose-500 focus:ring-2 focus:ring-rose-500/20 rounded-2xl text-slate-100 placeholder-slate-500 text-base leading-relaxed transition-all resize-none outline-none"
            />
          </div>

          {/* Quick Preset Ideas */}
          <div className="mt-3">
            <p className="text-xs text-slate-400 font-medium mb-2">Quick Inspiration Ideas:</p>
            <div className="flex flex-wrap gap-2">
              {SAMPLE_PROMPTS.map((s, idx) => (
                <button
                  key={idx}
                  type="button"
                  onClick={() => applySample(s)}
                  disabled={isGenerating}
                  className="text-xs px-3 py-1.5 rounded-xl bg-slate-950 border border-slate-800 hover:border-slate-700 text-slate-300 hover:text-white transition-all flex items-center gap-1.5"
                >
                  <span>✨</span>
                  <span>{s.title}</span>
                </button>
              ))}
            </div>
          </div>
        </div>

        {/* 2. Configuration Grid */}
        <div className="grid grid-cols-1 sm:grid-cols-2 gap-4 pt-2">
          {/* Voice Style */}
          <div className="bg-slate-950/60 border border-slate-800/80 rounded-2xl p-4">
            <label htmlFor="voice-style-select" className="text-xs font-bold uppercase tracking-wider text-slate-300 flex items-center gap-2 mb-2">
              <Mic className="w-3.5 h-3.5 text-amber-400" />
              <span>Voice Style</span>
            </label>
            <select
              id="voice-style-select"
              value={voiceStyle}
              onChange={(e) => setVoiceStyle(e.target.value as VoiceStyle)}
              disabled={isGenerating}
              className="w-full px-3 py-2.5 bg-slate-900 border border-slate-700/80 rounded-xl text-slate-100 font-medium text-sm focus:outline-none focus:border-amber-400 transition-colors"
            >
              {VOICE_STYLES.map((v) => (
                <option key={v.id} value={v.id}>
                  {v.icon} {v.label} — {v.desc}
                </option>
              ))}
            </select>
          </div>

          {/* Visual / Reel Style */}
          <div className="bg-slate-950/60 border border-slate-800/80 rounded-2xl p-4">
            <label htmlFor="visual-style-select" className="text-xs font-bold uppercase tracking-wider text-slate-300 flex items-center gap-2 mb-2">
              <Palette className="w-3.5 h-3.5 text-indigo-400" />
              <span>Visual / Reel Style</span>
            </label>
            <select
              id="visual-style-select"
              value={visualStyle}
              onChange={(e) => setVisualStyle(e.target.value as VisualStyle)}
              disabled={isGenerating}
              className="w-full px-3 py-2.5 bg-slate-900 border border-slate-700/80 rounded-xl text-slate-100 font-medium text-sm focus:outline-none focus:border-indigo-400 transition-colors"
            >
              {VISUAL_STYLES.map((vis) => (
                <option key={vis.id} value={vis.id}>
                  🎨 {vis.label}
                </option>
              ))}
            </select>
          </div>
        </div>

        {/* 3. Duration & Format Controls */}
        <div className="grid grid-cols-1 sm:grid-cols-2 gap-4">
          {/* Duration Selector */}
          <div className="bg-slate-950/60 border border-slate-800/80 rounded-2xl p-4">
            <label className="text-xs font-bold uppercase tracking-wider text-slate-300 flex items-center gap-2 mb-2.5">
              <Clock className="w-3.5 h-3.5 text-emerald-400" />
              <span>Duration</span>
            </label>
            <div className="grid grid-cols-4 gap-2">
              {([15, 30, 45, 60] as ReelDuration[]).map((dur) => (
                <button
                  key={dur}
                  type="button"
                  onClick={() => setDuration(dur)}
                  disabled={isGenerating}
                  className={`py-2 px-1 text-center rounded-xl font-bold text-xs transition-all ${
                    duration === dur
                      ? "bg-emerald-500 text-slate-950 shadow-md shadow-emerald-500/20"
                      : "bg-slate-900 text-slate-400 hover:text-white border border-slate-800"
                  }`}
                >
                  {dur}s
                </button>
              ))}
            </div>
          </div>

          {/* Aspect Ratio Format */}
          <div className="bg-slate-950/60 border border-slate-800/80 rounded-2xl p-4">
            <label className="text-xs font-bold uppercase tracking-wider text-slate-300 flex items-center gap-2 mb-2.5">
              <Smartphone className="w-3.5 h-3.5 text-rose-400" />
              <span>Format</span>
            </label>
            <div className="grid grid-cols-3 gap-2">
              {[
                { id: "9:16" as AspectRatio, label: "9:16", sub: "Shorts/Reel", icon: Smartphone },
                { id: "16:9" as AspectRatio, label: "16:9", sub: "YouTube", icon: Monitor },
                { id: "1:1" as AspectRatio, label: "1:1", sub: "Square", icon: Square },
              ].map((fmt) => {
                const Icon = fmt.icon;
                const active = aspectRatio === fmt.id;
                return (
                  <button
                    key={fmt.id}
                    type="button"
                    onClick={() => setAspectRatio(fmt.id)}
                    disabled={isGenerating}
                    className={`py-2 px-1.5 flex flex-col items-center justify-center rounded-xl font-semibold transition-all ${
                      active
                        ? "bg-rose-500 text-white shadow-md shadow-rose-500/20"
                        : "bg-slate-900 text-slate-400 hover:text-white border border-slate-800"
                    }`}
                  >
                    <Icon className="w-3.5 h-3.5 mb-1" />
                    <span className="text-xs font-bold leading-none">{fmt.label}</span>
                  </button>
                );
              })}
            </div>
          </div>
        </div>

        {/* 4. Large Generate Button */}
        <div className="pt-2">
          <button
            type="submit"
            id="generate-reel-main-button"
            disabled={isGenerating || !prompt.trim()}
            className="w-full py-4 px-6 rounded-2xl bg-gradient-to-r from-rose-500 via-pink-500 to-amber-500 hover:from-rose-600 hover:via-pink-600 hover:to-amber-600 active:scale-[0.99] text-white font-black text-lg sm:text-xl tracking-wide shadow-xl shadow-rose-500/25 flex items-center justify-center gap-3 transition-all duration-200 disabled:opacity-50 disabled:cursor-not-allowed group cursor-pointer"
          >
            <Sparkles className="w-6 h-6 animate-spin text-amber-200 group-hover:scale-110 transition-transform" />
            <span>✨ GENERATE REEL</span>
          </button>
          <p className="text-center text-xs text-slate-400 mt-2.5">
            Zero manual editing required. Gemini produces script, visuals, voice, audio sync, captions, and MP4.
          </p>
        </div>
      </form>
    </div>
  );
};
