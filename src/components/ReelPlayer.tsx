import React, { useState, useEffect, useRef } from "react";
import {
  Play,
  Pause,
  Volume2,
  VolumeX,
  Maximize2,
  Download,
  Music,
  RotateCcw,
  Sparkles,
  Type,
  FileText,
  Image as ImageIcon,
  Check,
  Smartphone,
  Monitor,
  Square,
  Share2,
  AlertTriangle,
  Mic,
} from "lucide-react";
import confetti from "canvas-confetti";
import { GeneratedReel, CaptionStyle, AspectRatio } from "../types";
import { audioEngine } from "../utils/audioEngine";
import { getActiveCaptionState } from "../utils/captionRenderer";
import {
  renderAndExportVideo,
  downloadBlob,
  exportSubtitles,
  exportMetadata,
} from "../utils/videoExporter";

interface ReelPlayerProps {
  reel: GeneratedReel;
  onNewReel: () => void;
  onUpdateCaptionStyle?: (style: CaptionStyle) => void;
  onRegenerateVoice?: () => Promise<void> | void;
}

export const ReelPlayer: React.FC<ReelPlayerProps> = ({
  reel,
  onNewReel,
  onUpdateCaptionStyle,
  onRegenerateVoice,
}) => {
  const [isPlaying, setIsPlaying] = useState(false);
  const [currentTime, setCurrentTime] = useState(0);
  const [isMuted, setIsMuted] = useState(false);
  const [volume, setVolume] = useState(0.8);
  const [captionStyle, setCaptionStyle] = useState<CaptionStyle>(reel.captionStyle || "tiktok_bold");
  const [activeAspectRatio, setActiveAspectRatio] = useState<AspectRatio>(reel.request.aspectRatio || "9:16");
  const [isExporting, setIsExporting] = useState(false);
  const [isRegeneratingVoice, setIsRegeneratingVoice] = useState(false);
  const [exportProgress, setExportProgress] = useState(0);
  const [exportStatus, setExportStatus] = useState("");
  const [copiedLink, setCopiedLink] = useState(false);
  const [activeSceneIndex, setActiveSceneIndex] = useState(0);

  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const animFrameRef = useRef<number | null>(null);
  const lastTimeRef = useRef<number>(0);
  const loadedImagesRef = useRef<HTMLImageElement[]>([]);
  const containerRef = useRef<HTMLDivElement | null>(null);

  // Preload images into memory for instant high-FPS canvas rendering
  useEffect(() => {
    const images: HTMLImageElement[] = [];
    reel.storyboard.forEach((scene) => {
      const img = new Image();
      img.crossOrigin = "anonymous";
      img.src = scene.imageUrl || "";
      images.push(img);
    });
    loadedImagesRef.current = images;
  }, [reel]);

  // Audio volume sync
  useEffect(() => {
    audioEngine.setVolume(volume);
    audioEngine.setMuted(isMuted);
  }, [volume, isMuted]);

  // Track playback time and scenes
  useEffect(() => {
    let accumulated = 0;
    for (let i = 0; i < reel.storyboard.length; i++) {
      const sc = reel.storyboard[i];
      if (currentTime >= accumulated && currentTime < accumulated + sc.duration) {
        if (activeSceneIndex !== i) {
          setActiveSceneIndex(i);
        }
        break;
      }
      accumulated += sc.duration;
    }
  }, [currentTime, activeSceneIndex, reel]);

  // Animation & Rendering Loop
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    const render = (timestamp: number) => {
      if (!lastTimeRef.current) lastTimeRef.current = timestamp;
      const delta = (timestamp - lastTimeRef.current) / 1000;
      lastTimeRef.current = timestamp;

      if (isPlaying) {
        setCurrentTime((prev) => {
          const next = prev + delta;
          if (next >= reel.totalDuration) {
            // Loop or pause
            return 0;
          }
          return next;
        });
      }

      // Draw canvas frame
      drawFrame(ctx, canvas.width, canvas.height, currentTime);

      animFrameRef.current = requestAnimationFrame(render);
    };

    animFrameRef.current = requestAnimationFrame(render);

    return () => {
      if (animFrameRef.current) cancelAnimationFrame(animFrameRef.current);
    };
  }, [isPlaying, currentTime, captionStyle, activeAspectRatio, reel]);

  const drawFrame = (ctx: CanvasRenderingContext2D, width: number, height: number, time: number) => {
    let accumulatedTime = 0;
    let sceneIndex = 0;
    let sceneLocalTime = 0;

    for (let i = 0; i < reel.storyboard.length; i++) {
      const sc = reel.storyboard[i];
      if (time >= accumulatedTime && time < accumulatedTime + sc.duration) {
        sceneIndex = i;
        sceneLocalTime = time - accumulatedTime;
        break;
      }
      accumulatedTime += sc.duration;
      if (i === reel.storyboard.length - 1) {
        sceneIndex = i;
        sceneLocalTime = sc.duration;
      }
    }

    const currentScene = reel.storyboard[sceneIndex] || reel.storyboard[0];
    const currentImg = loadedImagesRef.current[sceneIndex];
    const sceneProgress = Math.min(1, sceneLocalTime / Math.max(0.1, currentScene.duration));

    // Clear
    ctx.fillStyle = "#020617";
    ctx.fillRect(0, 0, width, height);

    // 1. Draw Image with dynamic Ken Burns Zoom & Pan
    if (currentImg && currentImg.complete && currentImg.naturalWidth > 0) {
      ctx.save();
      const scale = 1.0 + sceneProgress * 0.07;
      const transX = (sceneIndex % 2 === 0 ? 1 : -1) * sceneProgress * 15;
      const transY = sceneProgress * -10;

      ctx.translate(width / 2 + transX, height / 2 + transY);
      ctx.scale(scale, scale);

      const imgAspect = currentImg.naturalWidth / currentImg.naturalHeight;
      const canvasAspect = width / height;
      let drawW = width;
      let drawH = height;

      if (imgAspect > canvasAspect) {
        drawH = height;
        drawW = height * imgAspect;
      } else {
        drawW = width;
        drawH = width / imgAspect;
      }

      ctx.drawImage(currentImg, -drawW / 2, -drawH / 2, drawW, drawH);
      ctx.restore();
    } else {
      // Background gradient fallback
      const grad = ctx.createLinearGradient(0, 0, width, height);
      grad.addColorStop(0, "#0f172a");
      grad.addColorStop(1, "#1e1b4b");
      ctx.fillStyle = grad;
      ctx.fillRect(0, 0, width, height);
    }

    // 2. Cinematic Overlays
    const gradOverlay = ctx.createLinearGradient(0, 0, 0, height);
    gradOverlay.addColorStop(0, "rgba(0, 0, 0, 0.4)");
    gradOverlay.addColorStop(0.2, "rgba(0, 0, 0, 0.05)");
    gradOverlay.addColorStop(0.65, "rgba(0, 0, 0, 0.2)");
    gradOverlay.addColorStop(1, "rgba(0, 0, 0, 0.8)");
    ctx.fillStyle = gradOverlay;
    ctx.fillRect(0, 0, width, height);

    // 3. On-Screen Headline Badge
    if (currentScene.on_screen_text) {
      ctx.save();
      const titleY = height * 0.16;
      ctx.font = `900 ${Math.round(width * 0.05)}px system-ui, -apple-system, sans-serif`;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";

      const text = currentScene.on_screen_text.toUpperCase();
      const textMetrics = ctx.measureText(text);
      const bgPadX = width * 0.04;
      const bgPadY = height * 0.015;

      ctx.fillStyle = "rgba(15, 23, 42, 0.88)";
      ctx.beginPath();
      ctx.roundRect(
        width / 2 - textMetrics.width / 2 - bgPadX,
        titleY - bgPadY * 2,
        textMetrics.width + bgPadX * 2,
        bgPadY * 4 + 8,
        16
      );
      ctx.fill();

      ctx.strokeStyle = "#38bdf8";
      ctx.lineWidth = 2.5;
      ctx.stroke();

      ctx.fillStyle = "#ffffff";
      ctx.shadowColor = "rgba(0,0,0,0.8)";
      ctx.shadowBlur = 6;
      ctx.fillText(text, width / 2, titleY);
      ctx.restore();
    }

    // 4. Render Captions according to selected CaptionStyle
    const captionState = getActiveCaptionState(currentScene, sceneLocalTime);
    if (captionState.allWords.length > 0) {
      ctx.save();
      const captionY = height * 0.78;
      const fontSize = Math.round(width * 0.046);
      ctx.font = `800 ${fontSize}px system-ui, -apple-system, sans-serif`;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";

      const wordsPerChunk = 4;
      const chunkIndex = Math.floor(captionState.activeWordIndex / wordsPerChunk);
      const currentChunk = captionState.allWords.slice(
        chunkIndex * wordsPerChunk,
        (chunkIndex + 1) * wordsPerChunk
      );
      const chunkActiveRelative = captionState.activeWordIndex % wordsPerChunk;

      const chunkText = currentChunk.join(" ");
      const totalWidth = ctx.measureText(chunkText).width;
      const cardPadX = width * 0.035;
      const cardPadY = height * 0.016;

      if (captionStyle === "tiktok_bold" || captionStyle === "neon_punch") {
        ctx.fillStyle = captionStyle === "neon_punch" ? "rgba(244, 63, 94, 0.85)" : "rgba(0, 0, 0, 0.85)";
        ctx.beginPath();
        ctx.roundRect(
          width / 2 - totalWidth / 2 - cardPadX,
          captionY - cardPadY * 2,
          totalWidth + cardPadX * 2,
          cardPadY * 4 + 6,
          14
        );
        ctx.fill();
        if (captionStyle === "neon_punch") {
          ctx.strokeStyle = "#ffffff";
          ctx.lineWidth = 2;
          ctx.stroke();
        }
      }

      let currentX = width / 2 - totalWidth / 2;
      currentChunk.forEach((w, wIdx) => {
        const isCurrentWord = wIdx === chunkActiveRelative;
        const wMeasure = ctx.measureText(w + " ");

        ctx.save();
        if (isCurrentWord) {
          if (captionStyle === "karaoke_pop") {
            ctx.fillStyle = "#38bdf8";
            ctx.shadowColor = "#38bdf8";
            ctx.shadowBlur = 14;
          } else if (captionStyle === "cinematic_glow") {
            ctx.fillStyle = "#fef08a";
            ctx.shadowColor = "#fef08a";
            ctx.shadowBlur = 16;
          } else if (captionStyle === "neon_punch") {
            ctx.fillStyle = "#ffffff";
            ctx.shadowColor = "#f43f5e";
            ctx.shadowBlur = 10;
          } else {
            ctx.fillStyle = "#facc15"; // TikTok Yellow
            ctx.shadowColor = "rgba(250, 204, 21, 0.6)";
            ctx.shadowBlur = 12;
          }
        } else {
          ctx.fillStyle = "#ffffff";
          ctx.shadowColor = "rgba(0, 0, 0, 0.9)";
          ctx.shadowBlur = 6;
        }

        ctx.fillText(w, currentX + ctx.measureText(w).width / 2, captionY);
        ctx.restore();

        currentX += wMeasure.width;
      });

      ctx.restore();
    }

    // 5. Scene Transition Quick Fade
    if (sceneProgress < 0.08 && sceneIndex > 0) {
      const transAlpha = (1 - sceneProgress / 0.08) * 0.6;
      ctx.fillStyle = `rgba(0, 0, 0, ${transAlpha})`;
      ctx.fillRect(0, 0, width, height);
    }
  };

  const handleTogglePlay = () => {
    if (!isPlaying) {
      audioEngine.startBackgroundMusic(reel.contentPlan.visualBible.visualStyle || "Cinematic");
      const audioToPlay = reel.fullNarrationAudioUrl || reel.storyboard[activeSceneIndex]?.audioUrl;
      if (audioToPlay) {
        audioEngine.playContinuousNarration(audioToPlay, currentTime);
      }
      setIsPlaying(true);
    } else {
      audioEngine.stopAll();
      setIsPlaying(false);
    }
  };

  const handleSeek = (newTime: number) => {
    setCurrentTime(newTime);
    let accumulated = 0;
    for (let i = 0; i < reel.storyboard.length; i++) {
      const sc = reel.storyboard[i];
      if (newTime >= accumulated && newTime < accumulated + sc.duration) {
        setActiveSceneIndex(i);
        break;
      }
      accumulated += sc.duration;
    }

    const audioToPlay = reel.fullNarrationAudioUrl || reel.storyboard[activeSceneIndex]?.audioUrl;
    if (isPlaying && audioToPlay) {
      audioEngine.seekNarration(audioToPlay, newTime);
    }
  };

  const handleTriggerRegenerateVoice = async () => {
    if (!onRegenerateVoice || isRegeneratingVoice) return;
    setIsRegeneratingVoice(true);
    try {
      await onRegenerateVoice();
    } finally {
      setIsRegeneratingVoice(false);
    }
  };

  const handleExportVideo = async () => {
    setIsExporting(true);
    setExportProgress(0);
    setExportStatus("Initiating video render engine...");
    setIsPlaying(false);
    audioEngine.stopAll();

    try {
      const videoBlob = await renderAndExportVideo(reel, (pct, status) => {
        setExportProgress(pct);
        setExportStatus(status);
      });

      const cleanFilename = reel.metadata.reelName || "ai_reel_output.mp4";
      downloadBlob(videoBlob, cleanFilename);

      // Trigger celebratory confetti
      confetti({
        particleCount: 80,
        spread: 70,
        origin: { y: 0.6 },
      });
    } catch (err: any) {
      console.error("Video export error:", err);
      alert("Video export encountered an issue: " + (err.message || err));
    } finally {
      setIsExporting(false);
    }
  };

  const handleDownloadThumbnail = () => {
    if (reel.thumbnailUrl) {
      const a = document.createElement("a");
      a.href = reel.thumbnailUrl;
      a.download = `${reel.metadata.reelName.replace(/\.[^/.]+$/, "")}_thumbnail.png`;
      a.click();
    }
  };

  const handleCopyShare = () => {
    navigator.clipboard.writeText(window.location.href);
    setCopiedLink(true);
    setTimeout(() => setCopiedLink(false), 2000);
  };

  const formatTime = (secs: number) => {
    const mins = Math.floor(secs / 60);
    const s = Math.floor(secs % 60);
    return `${mins}:${s.toString().padStart(2, "0")}`;
  };

  return (
    <div className="w-full max-w-5xl mx-auto py-8 px-4 sm:px-6">
      {/* Daily Free Tier TTS Quota Notice Banner */}
      {reel.ttsStatus?.isDailyQuota && (
        <div className="w-full mb-6 p-4 rounded-2xl bg-amber-500/10 border border-amber-500/30 text-amber-200 text-xs sm:text-sm flex flex-col sm:flex-row items-start sm:items-center justify-between gap-3 shadow-lg shadow-amber-950/20 animate-in fade-in">
          <div className="flex items-start gap-3">
            <AlertTriangle className="w-5 h-5 text-amber-400 shrink-0 mt-0.5" />
            <div>
              <p className="font-bold text-amber-100 text-sm">
                Gemini Free Tier TTS quota has been reached. No more TTS requests will be made until the quota resets.
              </p>
              <p className="text-amber-300/80 text-xs mt-1">
                {reel.ttsStatus?.retryAfter
                  ? `Daily limit: 10 requests. Estimated retry/reset: ${reel.ttsStatus.retryAfter}`
                  : "Your video has been assembled with full visual assets, synchronized captions, and ambient soundtrack."}
              </p>
            </div>
          </div>
          {onRegenerateVoice && (
            <button
              onClick={handleTriggerRegenerateVoice}
              disabled={isRegeneratingVoice}
              className="px-4 py-2 rounded-xl bg-amber-500 hover:bg-amber-600 active:scale-95 text-slate-950 font-bold text-xs transition-all shrink-0 flex items-center gap-1.5 cursor-pointer disabled:opacity-50 shadow-sm shadow-amber-500/20"
            >
              <Mic className="w-3.5 h-3.5" />
              <span>{isRegeneratingVoice ? "Synthesizing..." : "Regenerate Voice"}</span>
            </button>
          )}
        </div>
      )}

      {/* Top Header & Actions Bar */}
      <div className="flex flex-col sm:flex-row items-start sm:items-center justify-between gap-4 mb-6">
        <div>
          <div className="inline-flex items-center gap-2 px-3 py-1 rounded-full bg-emerald-500/10 border border-emerald-500/20 text-emerald-400 text-xs font-bold uppercase tracking-wider mb-1.5">
            <Check className="w-3.5 h-3.5" />
            <span>Finished Video Ready</span>
          </div>
          <h2 className="text-2xl sm:text-3xl font-black text-white tracking-tight">
            {reel.metadata.youtubeTitle}
          </h2>
          <p className="text-xs sm:text-sm text-slate-400">
            {reel.totalDuration}s • {reel.request.voiceStyle} Voice • {reel.request.visualStyle} Style • {reel.storyboard.length} Scenes
          </p>
        </div>

        <div className="flex items-center gap-2 w-full sm:w-auto">
          {onRegenerateVoice && (
            <button
              onClick={handleTriggerRegenerateVoice}
              disabled={isRegeneratingVoice}
              className="px-3.5 py-2.5 rounded-xl bg-slate-900 border border-slate-800 hover:border-slate-700 text-slate-300 hover:text-white text-xs font-bold flex items-center justify-center gap-2 transition-all cursor-pointer disabled:opacity-50"
              title="Only calls Gemini TTS upon explicit user click"
            >
              <Mic className="w-3.5 h-3.5 text-rose-400" />
              <span>{isRegeneratingVoice ? "Synthesizing..." : "Regenerate Voice"}</span>
            </button>
          )}

          <button
            onClick={onNewReel}
            className="flex-1 sm:flex-none px-4 py-2.5 rounded-xl bg-slate-900 border border-slate-800 hover:border-slate-700 text-slate-300 hover:text-white text-xs font-bold flex items-center justify-center gap-2 transition-all cursor-pointer"
          >
            <RotateCcw className="w-3.5 h-3.5" />
            <span>Create New Reel</span>
          </button>

          <button
            onClick={handleCopyShare}
            className="px-3 py-2.5 rounded-xl bg-slate-900 border border-slate-800 hover:border-slate-700 text-slate-300 hover:text-white text-xs font-bold flex items-center gap-1.5 transition-all cursor-pointer"
            title="Copy app link"
          >
            <Share2 className="w-3.5 h-3.5" />
            <span>{copiedLink ? "Copied!" : "Share"}</span>
          </button>
        </div>
      </div>

      {/* Main Video Player Container */}
      <div className="grid grid-cols-1 lg:grid-cols-12 gap-8 items-start">
        {/* Left: Video Stage */}
        <div className="lg:col-span-6 flex flex-col items-center">
          {/* Mockup Frame based on Aspect Ratio */}
          <div
            ref={containerRef}
            className={`relative rounded-3xl overflow-hidden shadow-2xl border-4 border-slate-800 bg-slate-950 transition-all duration-300 ${
              activeAspectRatio === "9:16"
                ? "w-[300px] sm:w-[340px] aspect-[9/16]"
                : activeAspectRatio === "16:9"
                ? "w-full max-w-[520px] aspect-[16/9]"
                : "w-[320px] sm:w-[380px] aspect-square"
            }`}
          >
            {/* Phone speaker notch (for 9:16) */}
            {activeAspectRatio === "9:16" && (
              <div className="absolute top-2 left-1/2 -translate-x-1/2 w-20 h-3 rounded-full bg-slate-900/90 z-20 pointer-events-none border border-slate-800/80" />
            )}

            {/* Video Canvas */}
            <canvas
              ref={canvasRef}
              width={activeAspectRatio === "16:9" ? 1920 : 1080}
              height={activeAspectRatio === "16:9" ? 1080 : 1920}
              onClick={handleTogglePlay}
              className="w-full h-full object-cover cursor-pointer"
            />

            {/* Floating Play/Pause Overlay on Hover */}
            <div
              onClick={handleTogglePlay}
              className={`absolute inset-0 flex items-center justify-center bg-black/20 hover:bg-black/30 transition-opacity cursor-pointer ${
                isPlaying ? "opacity-0 hover:opacity-100" : "opacity-100"
              }`}
            >
              <div className="w-16 h-16 rounded-full bg-rose-500/90 text-white flex items-center justify-center shadow-xl shadow-rose-500/40 backdrop-blur-sm transform hover:scale-110 active:scale-95 transition-transform">
                {isPlaying ? <Pause className="w-7 h-7" /> : <Play className="w-7 h-7 ml-1" />}
              </div>
            </div>

            {/* Timeline Bar Overlaid on Video Bottom */}
            <div className="absolute bottom-0 inset-x-0 p-3 bg-gradient-to-t from-black/90 via-black/40 to-transparent z-10">
              <input
                type="range"
                min={0}
                max={reel.totalDuration || 30}
                step={0.1}
                value={currentTime}
                onChange={(e) => handleSeek(parseFloat(e.target.value))}
                className="w-full h-1.5 bg-slate-700/80 rounded-lg appearance-none cursor-pointer accent-rose-500"
              />
              <div className="flex items-center justify-between text-[11px] font-mono text-slate-300 font-semibold mt-1">
                <span>{formatTime(currentTime)}</span>
                <span>{formatTime(reel.totalDuration)}</span>
              </div>
            </div>
          </div>

          {/* Player Controls Bar */}
          <div className="w-full max-w-[340px] sm:max-w-[420px] mt-4 p-3 rounded-2xl bg-slate-900 border border-slate-800 flex items-center justify-between gap-3 shadow-lg">
            <button
              onClick={handleTogglePlay}
              className="w-9 h-9 rounded-xl bg-rose-500 hover:bg-rose-600 text-white flex items-center justify-center transition-colors cursor-pointer"
            >
              {isPlaying ? <Pause className="w-4 h-4" /> : <Play className="w-4 h-4 ml-0.5" />}
            </button>

            <div className="flex items-center gap-2 flex-1">
              <button
                onClick={() => setIsMuted(!isMuted)}
                className="text-slate-400 hover:text-white p-1"
              >
                {isMuted ? <VolumeX className="w-4 h-4" /> : <Volume2 className="w-4 h-4" />}
              </button>
              <input
                type="range"
                min={0}
                max={1}
                step={0.05}
                value={isMuted ? 0 : volume}
                onChange={(e) => setVolume(parseFloat(e.target.value))}
                className="w-20 h-1 bg-slate-800 rounded-lg appearance-none cursor-pointer accent-rose-500"
              />
            </div>

            {/* Aspect Ratio Quick Toggles */}
            <div className="flex items-center gap-1 bg-slate-950 p-1 rounded-xl border border-slate-800">
              <button
                onClick={() => setActiveAspectRatio("9:16")}
                className={`p-1 rounded-lg text-xs ${
                  activeAspectRatio === "9:16" ? "bg-rose-500 text-white" : "text-slate-400 hover:text-white"
                }`}
                title="9:16 Vertical"
              >
                <Smartphone className="w-3.5 h-3.5" />
              </button>
              <button
                onClick={() => setActiveAspectRatio("16:9")}
                className={`p-1 rounded-lg text-xs ${
                  activeAspectRatio === "16:9" ? "bg-rose-500 text-white" : "text-slate-400 hover:text-white"
                }`}
                title="16:9 Landscape"
              >
                <Monitor className="w-3.5 h-3.5" />
              </button>
              <button
                onClick={() => setActiveAspectRatio("1:1")}
                className={`p-1 rounded-lg text-xs ${
                  activeAspectRatio === "1:1" ? "bg-rose-500 text-white" : "text-slate-400 hover:text-white"
                }`}
                title="1:1 Square"
              >
                <Square className="w-3.5 h-3.5" />
              </button>
            </div>
          </div>
        </div>

        {/* Right: Primary Download & Customization Panel */}
        <div className="lg:col-span-6 space-y-6">
          {/* Main Download Callout */}
          <div className="p-6 rounded-3xl bg-gradient-to-br from-slate-900 to-slate-950 border border-slate-800 shadow-2xl">
            <div className="flex items-center justify-between mb-4">
              <span className="text-xs font-bold uppercase tracking-wider text-rose-400 flex items-center gap-1.5">
                <Sparkles className="w-4 h-4" />
                <span>Export & Publish</span>
              </span>
              <span className="text-xs px-2.5 py-0.5 rounded-full bg-slate-800 text-slate-300 font-semibold">
                1080 × 1920 HD
              </span>
            </div>

            <h3 className="text-xl font-bold text-white mb-2">Download Finished Video</h3>
            <p className="text-xs text-slate-400 mb-6">
              Assembled with smooth camera moves, dynamic captions, voiceover sync, and ambient music mix.
            </p>

            {/* BIG DOWNLOAD BUTTON */}
            <button
              onClick={handleExportVideo}
              disabled={isExporting}
              id="download-mp4-btn"
              className="w-full py-4 px-6 rounded-2xl bg-gradient-to-r from-rose-500 via-pink-500 to-amber-500 hover:from-rose-600 hover:via-pink-600 hover:to-amber-600 active:scale-[0.99] text-white font-black text-lg tracking-wide shadow-xl shadow-rose-500/25 flex items-center justify-center gap-3 transition-all cursor-pointer disabled:opacity-50"
            >
              <Download className={`w-5 h-5 ${isExporting ? "animate-bounce" : ""}`} />
              <span>{isExporting ? "EXPORTING MP4 VIDEO..." : "DOWNLOAD MP4 VIDEO"}</span>
            </button>

            {/* Export Progress Modal/Bar */}
            {isExporting && (
              <div className="mt-4 p-4 rounded-xl bg-slate-950 border border-slate-800">
                <div className="flex items-center justify-between text-xs mb-1.5">
                  <span className="text-slate-300 font-semibold">{exportStatus}</span>
                  <span className="text-rose-400 font-bold">{exportProgress}%</span>
                </div>
                <div className="w-full bg-slate-900 rounded-full h-2 overflow-hidden">
                  <div
                    className="h-full bg-gradient-to-r from-rose-500 to-amber-400 transition-all duration-300"
                    style={{ width: `${exportProgress}%` }}
                  />
                </div>
              </div>
            )}

            {/* Secondary Export Options */}
            <div className="grid grid-cols-2 sm:grid-cols-4 gap-2.5 mt-4 pt-4 border-t border-slate-800">
              <button
                onClick={() => exportSubtitles(reel.storyboard, reel.metadata.reelName, "srt")}
                className="py-2 px-3 rounded-xl bg-slate-950 border border-slate-800 hover:border-slate-700 text-slate-300 hover:text-white text-xs font-semibold flex flex-col items-center justify-center gap-1 transition-all cursor-pointer"
              >
                <FileText className="w-3.5 h-3.5 text-blue-400" />
                <span>.SRT Subtitles</span>
              </button>

              <button
                onClick={() => exportSubtitles(reel.storyboard, reel.metadata.reelName, "vtt")}
                className="py-2 px-3 rounded-xl bg-slate-950 border border-slate-800 hover:border-slate-700 text-slate-300 hover:text-white text-xs font-semibold flex flex-col items-center justify-center gap-1 transition-all cursor-pointer"
              >
                <FileText className="w-3.5 h-3.5 text-teal-400" />
                <span>.VTT Subtitles</span>
              </button>

              <button
                onClick={handleDownloadThumbnail}
                className="py-2 px-3 rounded-xl bg-slate-950 border border-slate-800 hover:border-slate-700 text-slate-300 hover:text-white text-xs font-semibold flex flex-col items-center justify-center gap-1 transition-all cursor-pointer"
              >
                <ImageIcon className="w-3.5 h-3.5 text-amber-400" />
                <span>Thumbnail</span>
              </button>

              <button
                onClick={() => exportMetadata(reel)}
                className="py-2 px-3 rounded-xl bg-slate-950 border border-slate-800 hover:border-slate-700 text-slate-300 hover:text-white text-xs font-semibold flex flex-col items-center justify-center gap-1 transition-all cursor-pointer"
              >
                <Share2 className="w-3.5 h-3.5 text-purple-400" />
                <span>Metadata</span>
              </button>
            </div>
          </div>

          {/* Caption Style Switcher */}
          <div className="p-5 rounded-3xl bg-slate-900 border border-slate-800">
            <h4 className="text-xs font-bold uppercase tracking-wider text-slate-300 flex items-center gap-2 mb-3">
              <Type className="w-4 h-4 text-amber-400" />
              <span>Animated Caption Theme</span>
            </h4>
            <div className="grid grid-cols-2 sm:grid-cols-3 gap-2">
              {[
                { id: "tiktok_bold" as CaptionStyle, name: "TikTok Viral", desc: "Yellow punch highlight" },
                { id: "karaoke_pop" as CaptionStyle, name: "Karaoke Pop", desc: "Cyan bounce glow" },
                { id: "cinematic_glow" as CaptionStyle, name: "Cinematic Glow", desc: "Soft atmospheric glow" },
                { id: "neon_punch" as CaptionStyle, name: "Neon Punch", desc: "High contrast magenta" },
                { id: "minimalist_clean" as CaptionStyle, name: "Clean Minimal", desc: "Crisp white text" },
              ].map((style) => (
                <button
                  key={style.id}
                  onClick={() => {
                    setCaptionStyle(style.id);
                    onUpdateCaptionStyle?.(style.id);
                  }}
                  className={`p-2.5 rounded-xl border text-left transition-all ${
                    captionStyle === style.id
                      ? "bg-amber-500/10 border-amber-500 text-white shadow-md shadow-amber-500/10"
                      : "bg-slate-950 border-slate-800 text-slate-400 hover:text-slate-200"
                  }`}
                >
                  <p className="text-xs font-bold">{style.name}</p>
                  <p className="text-[10px] text-slate-500">{style.desc}</p>
                </button>
              ))}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
