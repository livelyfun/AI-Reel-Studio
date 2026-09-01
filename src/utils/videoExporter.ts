import { GeneratedReel, StoryboardScene } from "../types";
import { generateSrt, generateVtt, getActiveCaptionState } from "./captionRenderer";
import { audioEngine } from "./audioEngine";

/**
 * Trigger browser file download
 */
export function downloadBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/**
 * Download text content as file
 */
export function downloadText(content: string, filename: string, mimeType = "text/plain") {
  const blob = new Blob([content], { type: mimeType });
  downloadBlob(blob, filename);
}

/**
 * Download Subtitle Files (.srt, .vtt, .json)
 */
export function exportSubtitles(storyboard: StoryboardScene[], baseName: string, format: "srt" | "vtt" | "json") {
  const cleanName = baseName.replace(/\.[^/.]+$/, "");
  if (format === "srt") {
    const content = generateSrt(storyboard);
    downloadText(content, `${cleanName}_subtitles.srt`, "text/plain");
  } else if (format === "vtt") {
    const content = generateVtt(storyboard);
    downloadText(content, `${cleanName}_subtitles.vtt`, "text/vtt");
  } else if (format === "json") {
    const content = JSON.stringify(
      storyboard.map((s) => ({
        scene_id: s.scene_id,
        duration: s.duration,
        narration: s.narration,
        on_screen_text: s.on_screen_text,
        wordTimings: s.wordTimings,
      })),
      null,
      2
    );
    downloadText(content, `${cleanName}_word_timings.json`, "application/json");
  }
}

/**
 * Download complete YouTube / Social Metadata package
 */
export function exportMetadata(reel: GeneratedReel) {
  const cleanName = reel.metadata.reelName.replace(/\.[^/.]+$/, "");
  const textContent = `=====================================================
AI REEL STUDIO — YOUTUBE & SOCIAL METADATA
=====================================================
Reel Name: ${reel.metadata.reelName}
Total Duration: ${reel.totalDuration}s
Created: ${new Date(reel.createdAt).toLocaleString()}
Voice Style: ${reel.request.voiceStyle}
Visual Style: ${reel.request.visualStyle}

-----------------------------------------------------
YOUTUBE TITLE
-----------------------------------------------------
${reel.metadata.youtubeTitle}

-----------------------------------------------------
YOUTUBE DESCRIPTION
-----------------------------------------------------
${reel.metadata.youtubeDescription}

-----------------------------------------------------
TAGS (Comma-separated for YouTube upload)
-----------------------------------------------------
${reel.metadata.tags.join(", ")}

-----------------------------------------------------
VIRAL HASHTAGS
-----------------------------------------------------
${reel.metadata.hashtags.join(" ")}

-----------------------------------------------------
CONTENT PLAN INSIGHTS
-----------------------------------------------------
Target Audience: ${reel.contentPlan.targetAudience}
Tone: ${reel.contentPlan.tone}
Hook Analysis: ${reel.metadata.hookAnalysis || "Optimized high-retention opening"}
Audience Fit: ${reel.metadata.audienceFit || "Optimized for YouTube Shorts & TikTok"}
=====================================================`;

  downloadText(textContent, `${cleanName}_youtube_metadata.txt`);
}

/**
 * Render and record the complete finished Reel to an MP4/WebM video file
 */
export async function renderAndExportVideo(
  reel: GeneratedReel,
  onProgress?: (progress: number, status: string) => void
): Promise<Blob> {
  const isLandscape = reel.request.aspectRatio === "16:9";
  const isSquare = reel.request.aspectRatio === "1:1";

  const width = isLandscape ? 1920 : isSquare ? 1080 : 1080;
  const height = isLandscape ? 1080 : isSquare ? 1080 : 1920;

  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const ctx = canvas.getContext("2d");

  if (!ctx) throw new Error("Could not create canvas 2D context");

  onProgress?.(5, "Preloading scene visuals...");

  // Preload all scene images
  const loadedImages: HTMLImageElement[] = await Promise.all(
    reel.storyboard.map(
      (scene) =>
        new Promise<HTMLImageElement>((resolve) => {
          const img = new Image();
          img.crossOrigin = "anonymous";
          img.onload = () => resolve(img);
          img.onerror = () => {
            // Fallback image
            const fallback = new Image();
            fallback.src =
              "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='1080' height='1920'><rect width='100%' height='100%' fill='%230f172a'/><text x='50%' y='50%' fill='%23ffffff' font-size='48' text-anchor='middle'>Scene Visual</text></svg>";
            fallback.onload = () => resolve(fallback);
          };
          img.src = scene.imageUrl || "";
        })
    )
  );

  onProgress?.(15, "Setting up video capture stream...");

  // Capture canvas video stream
  const videoStream = canvas.captureStream(30);

  // Connect Web Audio stream if available
  const audioDest = audioEngine.getDestination();
  if (audioDest && audioDest.stream.getAudioTracks().length > 0) {
    audioDest.stream.getAudioTracks().forEach((track) => videoStream.addTrack(track));
  }

  // Setup MediaRecorder
  let mimeType = "video/webm;codecs=vp9";
  if (!MediaRecorder.isTypeSupported(mimeType)) {
    mimeType = "video/webm;codecs=vp8";
  }
  if (!MediaRecorder.isTypeSupported(mimeType)) {
    mimeType = "video/webm";
  }

  const recordedChunks: Blob[] = [];
  const recorder = new MediaRecorder(videoStream, {
    mimeType,
    videoBitsPerSecond: 6000000, // 6 Mbps high quality
  });

  recorder.ondataavailable = (e) => {
    if (e.data.size > 0) recordedChunks.push(e.data);
  };

  const recordingPromise = new Promise<Blob>((resolve, reject) => {
    recorder.onstop = () => {
      const videoBlob = new Blob(recordedChunks, { type: mimeType });
      resolve(videoBlob);
    };
    recorder.onerror = (e) => reject(e);
  });

  recorder.start(100);

  // Start background audio during render
  audioEngine.startBackgroundMusic(reel.contentPlan.visualBible.visualStyle || "Cinematic");
  if (reel.fullNarrationAudioUrl) {
    audioEngine.playContinuousNarration(reel.fullNarrationAudioUrl, 0);
  }

  const fps = 30;
  const totalFrames = Math.max(30, Math.floor(reel.totalDuration * fps));
  let currentFrame = 0;

  // Render loop
  for (let f = 0; f < totalFrames; f++) {
    const globalTime = f / fps;

    // Determine current scene
    let accumulatedTime = 0;
    let sceneIndex = 0;
    let sceneLocalTime = 0;

    for (let i = 0; i < reel.storyboard.length; i++) {
      const sc = reel.storyboard[i];
      if (globalTime >= accumulatedTime && globalTime < accumulatedTime + sc.duration) {
        sceneIndex = i;
        sceneLocalTime = globalTime - accumulatedTime;
        break;
      }
      accumulatedTime += sc.duration;
      if (i === reel.storyboard.length - 1) {
        sceneIndex = i;
        sceneLocalTime = sc.duration;
      }
    }

    const currentScene = reel.storyboard[sceneIndex];
    const currentImg = loadedImages[sceneIndex] || loadedImages[0];
    const sceneProgress = Math.min(1, sceneLocalTime / Math.max(0.1, currentScene.duration));

    // 1. Draw Background & Ken Burns Zoom/Pan
    ctx.save();
    ctx.fillStyle = "#000000";
    ctx.fillRect(0, 0, width, height);

    // Subtle Ken Burns scale & translation
    const scale = 1.0 + sceneProgress * 0.08;
    const transX = (sceneIndex % 2 === 0 ? 1 : -1) * sceneProgress * 20;
    const transY = sceneProgress * -15;

    ctx.translate(width / 2 + transX, height / 2 + transY);
    ctx.scale(scale, scale);

    // Calculate aspect fill
    const imgAspect = currentImg.width / currentImg.height;
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

    // 2. Cinematic Lighting & Gradient Overlays (Vignette & Bottom Shading for captions)
    const overlayGrad = ctx.createLinearGradient(0, 0, 0, height);
    overlayGrad.addColorStop(0, "rgba(0, 0, 0, 0.45)");
    overlayGrad.addColorStop(0.2, "rgba(0, 0, 0, 0.05)");
    overlayGrad.addColorStop(0.7, "rgba(0, 0, 0, 0.2)");
    overlayGrad.addColorStop(1, "rgba(0, 0, 0, 0.75)");
    ctx.fillStyle = overlayGrad;
    ctx.fillRect(0, 0, width, height);

    // 3. Draw On-Screen Headline Punch
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

      // Rounded background badge
      ctx.fillStyle = "rgba(15, 23, 42, 0.85)";
      ctx.beginPath();
      ctx.roundRect(
        width / 2 - textMetrics.width / 2 - bgPadX,
        titleY - bgPadY * 2,
        textMetrics.width + bgPadX * 2,
        bgPadY * 4 + 10,
        18
      );
      ctx.fill();

      // Border glow
      ctx.strokeStyle = "#38bdf8";
      ctx.lineWidth = 3;
      ctx.stroke();

      // Text with shadow
      ctx.fillStyle = "#ffffff";
      ctx.shadowColor = "rgba(0, 0, 0, 0.8)";
      ctx.shadowBlur = 8;
      ctx.fillText(text, width / 2, titleY);
      ctx.restore();
    }

    // 4. Draw Dynamic Word-by-Word Synchronized Captions
    const captionState = getActiveCaptionState(currentScene, sceneLocalTime);
    if (captionState.allWords.length > 0) {
      ctx.save();
      const captionY = height * 0.78;
      const fontSize = Math.round(width * 0.046);
      ctx.font = `800 ${fontSize}px system-ui, -apple-system, sans-serif`;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";

      // Group into 4-word chunk for short-form video punchiness
      const wordsPerChunk = 4;
      const chunkIndex = Math.floor(captionState.activeWordIndex / wordsPerChunk);
      const currentChunk = captionState.allWords.slice(
        chunkIndex * wordsPerChunk,
        (chunkIndex + 1) * wordsPerChunk
      );
      const chunkActiveRelative = captionState.activeWordIndex % wordsPerChunk;

      const chunkText = currentChunk.join(" ");
      const totalWidth = ctx.measureText(chunkText).width;

      // Draw caption card backdrop
      const cardPadX = width * 0.035;
      const cardPadY = height * 0.016;
      ctx.fillStyle = "rgba(0, 0, 0, 0.85)";
      ctx.beginPath();
      ctx.roundRect(
        width / 2 - totalWidth / 2 - cardPadX,
        captionY - cardPadY * 2,
        totalWidth + cardPadX * 2,
        cardPadY * 4 + 6,
        14
      );
      ctx.fill();

      // Render words with individual highlighting
      let currentX = width / 2 - totalWidth / 2;
      currentChunk.forEach((w, wIdx) => {
        const isCurrentWord = wIdx === chunkActiveRelative;
        const wMeasure = ctx.measureText(w + " ");

        ctx.save();
        if (isCurrentWord) {
          ctx.fillStyle = "#facc15"; // Vibrant TikTok yellow highlight
          ctx.shadowColor = "rgba(250, 204, 21, 0.6)";
          ctx.shadowBlur = 12;
        } else {
          ctx.fillStyle = "#ffffff";
          ctx.shadowColor = "rgba(0, 0, 0, 0.8)";
          ctx.shadowBlur = 4;
        }

        ctx.fillText(w, currentX + ctx.measureText(w).width / 2, captionY);
        ctx.restore();

        currentX += wMeasure.width;
      });

      ctx.restore();
    }

    // 5. Scene Transition Effects (Fade / Flash on scene boundary)
    if (sceneProgress < 0.08 && sceneIndex > 0) {
      const transAlpha = (1 - sceneProgress / 0.08) * 0.6;
      ctx.fillStyle = `rgba(0, 0, 0, ${transAlpha})`;
      ctx.fillRect(0, 0, width, height);
    }

    currentFrame++;
    if (currentFrame % 10 === 0) {
      const pct = Math.min(95, Math.round(20 + (currentFrame / totalFrames) * 75));
      onProgress?.(pct, `Rendering video frames (${currentFrame}/${totalFrames})...`);
    }

    // Allow frame processing pause
    await new Promise((r) => setTimeout(r, 16));
  }

  onProgress?.(96, "Finalizing audio and encoding MP4 video...");
  audioEngine.stopAll();
  recorder.stop();

  const finalBlob = await recordingPromise;
  onProgress?.(100, "Reel export completed!");
  return finalBlob;
}
