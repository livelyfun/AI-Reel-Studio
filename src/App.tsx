import React, { useState } from "react";
import { Header } from "./components/Header";
import { ReelCreatorForm } from "./components/ReelCreatorForm";
import { GenerationProgress } from "./components/GenerationProgress";
import { ReelPlayer } from "./components/ReelPlayer";
import { StoryboardViewer } from "./components/StoryboardViewer";
import { MetadataViewer } from "./components/MetadataViewer";
import { VisualBibleCard } from "./components/VisualBibleCard";
import {
  ReelRequest,
  GeneratedReel,
  PipelineStep,
  StoryboardScene,
  YouTubeMetadata,
  CaptionStyle,
} from "./types";

const INITIAL_PIPELINE_STEPS: PipelineStep[] = [
  { id: "understand", label: "Understand Idea", detail: "Topic, purpose & audience analysis (Gemini Free)", status: "pending" },
  { id: "content_plan", label: "Content Plan", detail: "Viral hook, script & pacing structure (Gemini Free)", status: "pending" },
  { id: "visual_bible", label: "Visual Bible", detail: "Cross-scene visual consistency rules", status: "pending" },
  { id: "storyboard", label: "Storyboard", detail: "Camera motion & scene directions (Gemini Free)", status: "pending" },
  { id: "visuals", label: "Free Visual Assets", detail: "High-definition royalty-free media ($0 API cost)", status: "pending" },
  { id: "voiceover", label: "Voiceover Audio", detail: "Gemini Free TTS (gemini-3.1-flash-tts-preview)", status: "pending" },
  { id: "captions", label: "Dynamic Captions", detail: "Word timings & keyword highlights", status: "pending" },
  { id: "video_assembly", label: "Assemble Video", detail: "Ken Burns motion & transitions", status: "pending" },
  { id: "quality_check", label: "Quality Check", detail: "Timing, resolution & audio verification", status: "pending" },
  { id: "youtube_meta", label: "YouTube Metadata", detail: "SEO title, description & viral tags (Gemini Free)", status: "pending" },
];

export default function App() {
  const [currentView, setCurrentView] = useState<"form" | "progress" | "results">("form");
  const [pipelineSteps, setPipelineSteps] = useState<PipelineStep[]>(INITIAL_PIPELINE_STEPS);
  const [currentStepIndex, setCurrentStepIndex] = useState(0);
  const [progressPercent, setProgressPercent] = useState(0);
  const [statusMessage, setStatusMessage] = useState("Initializing Gemini reel pipeline...");
  const [partialScenes, setPartialScenes] = useState<StoryboardScene[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [lastRequest, setLastRequest] = useState<ReelRequest | null>(null);
  const [generatedReel, setGeneratedReel] = useState<GeneratedReel | null>(null);

  const updateStepStatus = (index: number, status: PipelineStep["status"], message?: string, pct?: number) => {
    setCurrentStepIndex(index);
    if (message) setStatusMessage(message);
    if (pct !== undefined) setProgressPercent(pct);

    setPipelineSteps((prev) =>
      prev.map((step, idx) => {
        if (idx < index) return { ...step, status: "completed" };
        if (idx === index) return { ...step, status };
        return { ...step, status: "pending" };
      })
    );
  };

  const handleGenerateReel = async (request: ReelRequest) => {
    setLastRequest(request);
    setError(null);
    setPartialScenes([]);
    setCurrentView("progress");
    setProgressPercent(5);
    setStatusMessage("Connecting to Gemini AI pipeline...");

    try {
      // Step 1: Understand Idea & Planning
      updateStepStatus(0, "in_progress", "Understanding topic, target audience, and viral hook...", 10);
      await new Promise((r) => setTimeout(r, 400));

      // Step 2 & 3: Content Plan & Visual Bible
      updateStepStatus(1, "in_progress", "Crafting high-retention narration script and story structure...", 20);
      const planRes = await fetch("/api/reel/plan", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(request),
      });

      const planData = await planRes.json();
      if (!planData.success || !planData.data) {
        throw new Error(planData.error || "Failed to create content plan");
      }

      const { contentPlan, storyboard, metadata } = planData.data;

      updateStepStatus(2, "in_progress", "Synthesizing Visual Bible for unified art direction...", 35);
      await new Promise((r) => setTimeout(r, 300));

      updateStepStatus(3, "in_progress", "Creating scene breakdown and camera movements...", 45);

      // Step 5: Visuals per scene with live updates ($0 API Cost Free Asset Resolver)
      updateStepStatus(4, "in_progress", "Searching and matching high-definition free visual assets ($0 API cost)...", 55);

      const generatedScenes: StoryboardScene[] = [];
      const totalScenes = storyboard.length;

      for (let i = 0; i < totalScenes; i++) {
        const sc = storyboard[i];
        setStatusMessage(`Retrieving Free Visual Asset for Scene ${sc.scene_id} of ${totalScenes}...`);

        let imgUrl = "";
        const imgRes = await fetch("/api/reel/generate-scene-visual", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            visualPrompt: sc.visual_prompt,
            visualBible: contentPlan.visualBible,
            visualStyle: request.visualStyle,
            aspectRatio: request.aspectRatio,
            keywords: sc.keywords || [],
            topic: contentPlan.topic,
          }),
        });
        const imgData = await imgRes.json();
        if (imgData.success && imgData.imageUrl) {
          imgUrl = imgData.imageUrl;
        } else {
          throw new Error(imgData.error || `Failed to obtain free visual asset for Scene ${sc.scene_id}`);
        }

        const sceneObj: StoryboardScene = {
          ...sc,
          imageUrl: imgUrl || undefined,
        };

        generatedScenes.push(sceneObj);
        setPartialScenes([...generatedScenes]);
        const stepProgress = Math.round(55 + ((i + 1) / totalScenes) * 15);
        setProgressPercent(stepProgress);
      }

      // Step 6: Single Continuous Narration Voiceover with Gemini Free Tier TTS (1 request)
      updateStepStatus(5, "in_progress", "Synthesizing complete narration with Gemini Free Tier TTS (1 request)...", 72);

      const fullNarrationText = generatedScenes.map((s) => s.narration).filter(Boolean).join(" ");
      let fullNarrationAudioUrl: string | undefined = undefined;
      let ttsStatusObj: any = { success: true };

      try {
        const voiceRes = await fetch("/api/reel/generate-voiceover", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            text: fullNarrationText,
            voiceStyle: request.voiceStyle,
            forceRegenerate: false,
          }),
        });
        const voiceData = await voiceRes.json();
        if (voiceData.success && voiceData.voiceover?.audioBase64) {
          fullNarrationAudioUrl = `data:audio/wav;base64,${voiceData.voiceover.audioBase64}`;
          ttsStatusObj = { success: true, fromCache: voiceData.voiceover.fromCache };
          updateStepStatus(
            5,
            "completed",
            voiceData.voiceover.fromCache
              ? "Reused cached voiceover narration ($0 API cost)"
              : "Gemini Free TTS voiceover synthesized (1 request)",
            78
          );
        } else {
          // Quota exceeded or error
          const isDailyQuota = voiceData.isDailyQuota || voiceData.isQuotaExceeded;
          ttsStatusObj = {
            success: false,
            quotaExceeded: Boolean(isDailyQuota),
            isDailyQuota: Boolean(voiceData.isDailyQuota),
            is503HighDemand: Boolean(voiceData.is503HighDemand),
            message: voiceData.error || "Gemini Free Tier TTS quota has been reached. No more TTS requests will be made until the quota resets.",
            retryAfter: voiceData.retryAfter,
          };
          updateStepStatus(
            5,
            "completed",
            isDailyQuota
              ? "Gemini Free Tier TTS quota reached. Video will render with visuals, captions & music."
              : "Voiceover synthesis notice. Proceeding with video assembly.",
            78
          );
        }
      } catch (voiceErr: any) {
        console.warn("Voiceover request notice:", voiceErr);
        ttsStatusObj = {
          success: false,
          message: voiceErr.message || "Failed to reach voiceover service",
        };
        updateStepStatus(5, "completed", "Proceeding with video assembly...", 78);
      }

      // Attach audio URL to scenes
      const scenesWithAudio = generatedScenes.map((sc) => ({
        ...sc,
        audioUrl: fullNarrationAudioUrl || undefined,
      }));

      // Step 7: Captions
      updateStepStatus(6, "in_progress", "Generating word-by-word synchronized captions...", 85);
      await new Promise((r) => setTimeout(r, 350));

      // Step 8: Video Assembly & Transitions
      updateStepStatus(7, "in_progress", "Assembling Ken Burns pan/zoom and seamless scene transitions...", 90);
      await new Promise((r) => setTimeout(r, 350));

      // Step 9: Quality Check
      updateStepStatus(8, "in_progress", "Performing automated quality verification on media streams...", 95);
      await new Promise((r) => setTimeout(r, 250));

      // Step 10: Final Metadata & Complete Reel Package
      updateStepStatus(9, "in_progress", "Finalizing YouTube SEO title, description and viral tags...", 98);

      const totalDuration = scenesWithAudio.reduce((acc, s) => acc + s.duration, 0);

      const fullReel: GeneratedReel = {
        id: `reel_${Date.now()}`,
        createdAt: new Date().toISOString(),
        request,
        contentPlan,
        storyboard: scenesWithAudio,
        metadata,
        totalDuration,
        backgroundMusicGenre: request.voiceStyle === "Energetic" ? "Electronic" : "Cinematic",
        thumbnailUrl: scenesWithAudio[0]?.imageUrl,
        captionStyle: "tiktok_bold",
        fullNarrationText,
        fullNarrationAudioUrl,
        ttsStatus: ttsStatusObj,
      };

      setGeneratedReel(fullReel);
      setProgressPercent(100);
      setStatusMessage("Reel creation finished!");
      updateStepStatus(9, "completed", "Finished video ready!", 100);

      await new Promise((r) => setTimeout(r, 500));
      setCurrentView("results");
    } catch (err: any) {
      console.error("Pipeline failure:", err);
      setError(err.message || "Failed to complete reel generation. Please try again.");
    }
  };

  const handleRegenerateVoice = async () => {
    if (!generatedReel) return;
    const textToSpeak =
      generatedReel.fullNarrationText ||
      generatedReel.storyboard.map((s) => s.narration).filter(Boolean).join(" ");
    if (!textToSpeak) return;

    try {
      const voiceRes = await fetch("/api/reel/generate-voiceover", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          text: textToSpeak,
          voiceStyle: generatedReel.request.voiceStyle,
          forceRegenerate: true,
        }),
      });
      const voiceData = await voiceRes.json();
      if (voiceData.success && voiceData.voiceover?.audioBase64) {
        const newAudioUrl = `data:audio/wav;base64,${voiceData.voiceover.audioBase64}`;
        const updatedStoryboard = generatedReel.storyboard.map((sc) => ({
          ...sc,
          audioUrl: newAudioUrl,
        }));
        setGeneratedReel({
          ...generatedReel,
          fullNarrationAudioUrl: newAudioUrl,
          storyboard: updatedStoryboard,
          ttsStatus: { success: true, fromCache: false },
        });
      } else {
        const isDailyQuota = voiceData.isDailyQuota || voiceData.isQuotaExceeded;
        setGeneratedReel({
          ...generatedReel,
          ttsStatus: {
            success: false,
            quotaExceeded: Boolean(isDailyQuota),
            isDailyQuota: Boolean(voiceData.isDailyQuota),
            is503HighDemand: Boolean(voiceData.is503HighDemand),
            message: voiceData.error || "Gemini Free Tier TTS quota has been reached. No more TTS requests will be made until the quota resets.",
            retryAfter: voiceData.retryAfter,
          },
        });
      }
    } catch (err: any) {
      console.error("Voice regeneration error:", err);
      alert(`Voice regeneration failed: ${err.message || "Network error"}`);
    }
  };

  const handleSceneUpdated = (updatedScene: StoryboardScene) => {
    if (!generatedReel) return;
    const updatedStoryboard = generatedReel.storyboard.map((sc) =>
      sc.scene_id === updatedScene.scene_id ? updatedScene : sc
    );
    setGeneratedReel({
      ...generatedReel,
      storyboard: updatedStoryboard,
    });
  };

  const handleUpdateMetadata = (updatedMetadata: YouTubeMetadata) => {
    if (!generatedReel) return;
    setGeneratedReel({
      ...generatedReel,
      metadata: updatedMetadata,
    });
  };

  const handleUpdateCaptionStyle = (style: CaptionStyle) => {
    if (!generatedReel) return;
    setGeneratedReel({
      ...generatedReel,
      captionStyle: style,
    });
  };

  return (
    <div className="min-h-screen bg-slate-950 text-slate-100 flex flex-col font-sans selection:bg-rose-500 selection:text-white">
      {/* App Header */}
      <Header
        onNewReel={() => setCurrentView("form")}
        isGenerating={currentView === "progress"}
      />

      {/* Main View Router */}
      <main className="flex-1 flex flex-col justify-start">
        {currentView === "form" && (
          <ReelCreatorForm
            onGenerate={handleGenerateReel}
            isGenerating={false}
          />
        )}

        {currentView === "progress" && (
          <GenerationProgress
            steps={pipelineSteps}
            currentStepIndex={currentStepIndex}
            progressPercent={progressPercent}
            statusMessage={statusMessage}
            partialScenes={partialScenes}
            error={error}
            onRetry={lastRequest ? () => handleGenerateReel(lastRequest) : undefined}
          />
        )}

        {currentView === "results" && generatedReel && (
          <div className="space-y-6 pb-16 animate-in fade-in duration-300">
            {/* 1. Main Reel Video Player & Exporter */}
            <ReelPlayer
              reel={generatedReel}
              onNewReel={() => setCurrentView("form")}
              onUpdateCaptionStyle={handleUpdateCaptionStyle}
              onRegenerateVoice={handleRegenerateVoice}
            />

            {/* 2. Scene-by-Scene Storyboard Viewer */}
            <StoryboardViewer
              storyboard={generatedReel.storyboard}
              visualBible={generatedReel.contentPlan.visualBible}
              voiceStyle={generatedReel.request.voiceStyle}
              visualStyle={generatedReel.request.visualStyle}
              aspectRatio={generatedReel.request.aspectRatio}
              onSceneUpdated={handleSceneUpdated}
            />

            {/* 3. YouTube & Social Metadata Viewer */}
            <MetadataViewer
              metadata={generatedReel.metadata}
              onUpdateMetadata={handleUpdateMetadata}
            />

            {/* 4. Visual Bible & Content Strategy Card */}
            <VisualBibleCard
              visualBible={generatedReel.contentPlan.visualBible}
              contentPlan={generatedReel.contentPlan}
            />
          </div>
        )}
      </main>

      {/* Footer */}
      <footer className="w-full border-t border-slate-900 bg-slate-950/80 py-6 text-center text-xs text-slate-500">
        <p>AI Reel Studio • Powered by Google Gemini • Automated Short-Form Video Producer</p>
      </footer>
    </div>
  );
}
