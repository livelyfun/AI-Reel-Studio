import React, { useState } from "react";
import { Film, RefreshCw, Sparkles, Volume2, Camera, Eye, SunMedium } from "lucide-react";
import { StoryboardScene, VisualBible, VoiceStyle, VisualStyle, AspectRatio } from "../types";

interface StoryboardViewerProps {
  storyboard: StoryboardScene[];
  visualBible: VisualBible;
  voiceStyle: VoiceStyle;
  visualStyle: VisualStyle;
  aspectRatio: AspectRatio;
  onSceneUpdated: (updatedScene: StoryboardScene) => void;
}

export const StoryboardViewer: React.FC<StoryboardViewerProps> = ({
  storyboard,
  visualBible,
  voiceStyle,
  visualStyle,
  aspectRatio,
  onSceneUpdated,
}) => {
  const [regeneratingSceneId, setRegeneratingSceneId] = useState<number | null>(null);

  const handleRegenerateScene = async (scene: StoryboardScene) => {
    setRegeneratingSceneId(scene.scene_id);
    try {
      const response = await fetch("/api/reel/regenerate-scene", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          scene,
          visualBible,
          visualStyle,
          voiceStyle,
          aspectRatio,
        }),
      });

      const data = await response.json();
      if (data.success && data.scene) {
        onSceneUpdated(data.scene);
      } else {
        alert(`Regeneration failed: ${data.error || "Could not regenerate scene."}`);
      }
    } catch (e: any) {
      console.error("Failed to regenerate scene:", e);
      alert(`Failed to regenerate scene: ${e?.message || "Network error"}`);
    } finally {
      setRegeneratingSceneId(null);
    }
  };

  return (
    <div className="w-full max-w-5xl mx-auto py-6 px-4 sm:px-6">
      <div className="flex items-center justify-between mb-4">
        <div>
          <h3 className="text-xl font-bold text-white tracking-tight flex items-center gap-2">
            <Film className="w-5 h-5 text-rose-400" />
            <span>Automated Storyboard & Scene Breakdown</span>
          </h3>
          <p className="text-xs text-slate-400">
            {storyboard.length} structured scenes with camera directions, on-screen text, and narration.
          </p>
        </div>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
        {storyboard.map((scene) => {
          const isRegenerating = regeneratingSceneId === scene.scene_id;

          return (
            <div
              key={scene.scene_id}
              className="bg-slate-900/90 border border-slate-800 rounded-2xl overflow-hidden shadow-lg flex flex-col group hover:border-slate-700 transition-all"
            >
              {/* Scene Visual Header */}
              <div className="relative aspect-video bg-slate-950 overflow-hidden">
                {scene.imageUrl ? (
                  <img
                    src={scene.imageUrl}
                    alt={`Scene ${scene.scene_id}`}
                    referrerPolicy="no-referrer"
                    className="w-full h-full object-cover group-hover:scale-105 transition-transform duration-500"
                  />
                ) : (
                  <div className="w-full h-full flex items-center justify-center text-slate-600">
                    <Film className="w-8 h-8 opacity-40" />
                  </div>
                )}

                {/* Overlays */}
                <div className="absolute top-2 left-2 flex items-center gap-1.5">
                  <span className="px-2 py-0.5 rounded-lg bg-slate-950/80 backdrop-blur-sm text-[11px] font-bold text-white border border-slate-800">
                    Scene {scene.scene_id}
                  </span>
                  <span className="px-2 py-0.5 rounded-lg bg-rose-500/90 text-[11px] font-bold text-white shadow-sm">
                    {scene.duration}s
                  </span>
                </div>

                {/* On-Screen Text Badge Preview */}
                {scene.on_screen_text && (
                  <div className="absolute bottom-2 inset-x-2 text-center">
                    <span className="inline-block px-2.5 py-1 rounded-md bg-slate-950/85 text-[10px] font-black text-amber-300 border border-amber-500/30 uppercase tracking-wide truncate max-w-full">
                      {scene.on_screen_text}
                    </span>
                  </div>
                )}
              </div>

              {/* Scene Content Details */}
              <div className="p-4 flex-1 flex flex-col justify-between space-y-3">
                {/* Narration Script */}
                <div>
                  <label className="text-[10px] font-bold uppercase tracking-wider text-slate-400 block mb-1">
                    Narration Script
                  </label>
                  <p className="text-xs text-slate-200 leading-relaxed font-medium bg-slate-950/50 p-2.5 rounded-xl border border-slate-800/80">
                    "{scene.narration}"
                  </p>
                </div>

                {/* Directorial Metadata Pills */}
                <div className="space-y-1.5 text-[11px]">
                  <div className="flex items-center gap-1.5 text-slate-400">
                    <Camera className="w-3.5 h-3.5 text-indigo-400 shrink-0" />
                    <span className="text-slate-300 font-medium truncate">{scene.camera}</span>
                  </div>
                  <div className="flex items-center gap-1.5 text-slate-400">
                    <SunMedium className="w-3.5 h-3.5 text-amber-400 shrink-0" />
                    <span className="text-slate-300 font-medium truncate">{scene.lighting}</span>
                  </div>
                </div>

                {/* Keywords Highlight */}
                {scene.keywords && scene.keywords.length > 0 && (
                  <div className="flex flex-wrap gap-1 pt-1">
                    {scene.keywords.map((kw, i) => (
                      <span
                        key={i}
                        className="px-2 py-0.5 rounded-md bg-amber-500/10 border border-amber-500/20 text-[10px] font-bold text-amber-300"
                      >
                        #{kw}
                      </span>
                    ))}
                  </div>
                )}

                {/* Optional Scene Regeneration Action */}
                <div className="pt-2 border-t border-slate-800/80 flex items-center justify-between">
                  <span className="text-[10px] text-slate-500">Transition: {scene.transition}</span>
                  <button
                    onClick={() => handleRegenerateScene(scene)}
                    disabled={isRegenerating}
                    className="px-2.5 py-1 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 hover:text-white text-[11px] font-semibold flex items-center gap-1.5 transition-colors cursor-pointer disabled:opacity-50"
                  >
                    <RefreshCw className={`w-3 h-3 ${isRegenerating ? "animate-spin text-rose-400" : ""}`} />
                    <span>{isRegenerating ? "Regenerating..." : "Regenerate Scene"}</span>
                  </button>
                </div>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};
