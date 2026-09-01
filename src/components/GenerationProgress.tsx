import React from "react";
import { CheckCircle2, Loader2, Sparkles, AlertCircle, Film, RefreshCw } from "lucide-react";
import { PipelineStep, StoryboardScene } from "../types";

interface GenerationProgressProps {
  steps: PipelineStep[];
  currentStepIndex: number;
  progressPercent: number;
  statusMessage: string;
  partialScenes?: StoryboardScene[];
  onRetry?: () => void;
  error?: string | null;
}

export const GenerationProgress: React.FC<GenerationProgressProps> = ({
  steps,
  currentStepIndex,
  progressPercent,
  statusMessage,
  partialScenes = [],
  onRetry,
  error,
}) => {
  return (
    <div className="w-full max-w-3xl mx-auto py-10 px-4 sm:px-6 animate-in fade-in duration-300">
      <div className="bg-slate-900/90 border border-slate-800 rounded-3xl p-6 sm:p-8 shadow-2xl backdrop-blur-xl">
        {/* Top Header */}
        <div className="flex items-center justify-between mb-6 pb-6 border-b border-slate-800">
          <div className="flex items-center gap-3">
            <div className="w-12 h-12 rounded-2xl bg-rose-500/10 border border-rose-500/20 flex items-center justify-center">
              {error ? (
                <AlertCircle className="w-6 h-6 text-rose-400" />
              ) : (
                <Sparkles className="w-6 h-6 text-rose-400 animate-pulse" />
              )}
            </div>
            <div>
              <h2 className="text-xl font-bold text-white tracking-tight">
                {error ? "Generation Stalled" : "Producing Your Finished Reel"}
              </h2>
              <p className="text-xs text-slate-400">
                {error ? "An automatic recovery step is available" : "Executing complete automated production pipeline"}
              </p>
            </div>
          </div>

          <div className="text-right">
            <span className="text-2xl font-black text-rose-400">{progressPercent}%</span>
            <p className="text-[10px] uppercase font-bold tracking-wider text-slate-500">Completed</p>
          </div>
        </div>

        {/* Progress Bar */}
        <div className="w-full bg-slate-950 rounded-full h-3 mb-6 p-0.5 border border-slate-800 overflow-hidden">
          <div
            className="h-full rounded-full bg-gradient-to-r from-rose-500 via-pink-500 to-amber-400 transition-all duration-500 ease-out"
            style={{ width: `${Math.max(4, Math.min(100, progressPercent))}%` }}
          />
        </div>

        {/* Status Callout */}
        <div className={`p-4 rounded-2xl mb-8 flex items-center gap-3 border ${
          error 
            ? "bg-rose-950/40 border-rose-800 text-rose-200" 
            : "bg-slate-950/70 border-slate-800/80 text-slate-200"
        }`}>
          {error ? (
            <AlertCircle className="w-5 h-5 text-rose-400 shrink-0" />
          ) : (
            <Loader2 className="w-5 h-5 text-rose-400 animate-spin shrink-0" />
          )}
          <div className="text-sm font-medium">
            <span className="text-slate-400 mr-1.5">Current Action:</span>
            <span className="text-white font-semibold">{statusMessage}</span>
          </div>
        </div>

        {/* Pipeline Step Checklist */}
        <div className="space-y-3 mb-8">
          <h3 className="text-xs font-bold uppercase tracking-wider text-slate-400 px-1">
            Automated Pipeline Stages
          </h3>
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-2.5">
            {steps.map((step, idx) => {
              const isCompleted = step.status === "completed" || idx < currentStepIndex;
              const isCurrent = step.status === "in_progress" || idx === currentStepIndex;
              const isFailed = step.status === "failed";

              return (
                <div
                  key={step.id}
                  className={`p-3 rounded-xl border flex items-center justify-between transition-all ${
                    isCompleted
                      ? "bg-emerald-950/20 border-emerald-900/40 text-emerald-300"
                      : isCurrent
                      ? "bg-rose-950/30 border-rose-500/50 text-white shadow-lg shadow-rose-950/30"
                      : isFailed
                      ? "bg-rose-950/40 border-rose-800 text-rose-300"
                      : "bg-slate-950/40 border-slate-800/60 text-slate-500"
                  }`}
                >
                  <div className="flex items-center gap-2.5">
                    {isCompleted ? (
                      <CheckCircle2 className="w-4 h-4 text-emerald-400 shrink-0" />
                    ) : isCurrent ? (
                      <Loader2 className="w-4 h-4 text-rose-400 animate-spin shrink-0" />
                    ) : isFailed ? (
                      <AlertCircle className="w-4 h-4 text-rose-400 shrink-0" />
                    ) : (
                      <div className="w-4 h-4 rounded-full border border-slate-700 shrink-0" />
                    )}
                    <div>
                      <p className="text-xs font-bold leading-tight">{step.label}</p>
                      <p className="text-[10px] text-slate-400">{step.detail}</p>
                    </div>
                  </div>

                  <span className="text-[10px] font-semibold uppercase px-2 py-0.5 rounded-full bg-slate-900/80 border border-slate-800">
                    {isCompleted ? "Done" : isCurrent ? "Active" : isFailed ? "Retry" : "Queued"}
                  </span>
                </div>
              );
            })}
          </div>
        </div>

        {/* Live Scene Visuals Preview Cards */}
        {partialScenes.length > 0 && (
          <div className="pt-4 border-t border-slate-800">
            <h4 className="text-xs font-bold uppercase tracking-wider text-slate-400 mb-3 flex items-center gap-1.5">
              <Film className="w-3.5 h-3.5 text-amber-400" />
              <span>Live Generated Storyboard Scenes ({partialScenes.length})</span>
            </h4>
            <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
              {partialScenes.map((scene, idx) => (
                <div key={idx} className="bg-slate-950 border border-slate-800 rounded-xl overflow-hidden group">
                  <div className="aspect-[9/16] relative bg-slate-900">
                    {scene.imageUrl ? (
                      <img
                        src={scene.imageUrl}
                        alt={`Scene ${scene.scene_id}`}
                        referrerPolicy="no-referrer"
                        className="w-full h-full object-cover"
                      />
                    ) : (
                      <div className="w-full h-full flex items-center justify-center p-2 text-center text-xs text-slate-500">
                        <Loader2 className="w-4 h-4 animate-spin text-rose-400" />
                      </div>
                    )}
                    <span className="absolute top-2 left-2 px-1.5 py-0.5 rounded bg-slate-950/80 text-[10px] font-bold text-white">
                      Scene {scene.scene_id}
                    </span>
                  </div>
                  <div className="p-2 text-[10px] text-slate-300 font-medium truncate">
                    {scene.on_screen_text || scene.narration.slice(0, 20)}
                  </div>
                </div>
              ))}
            </div>
          </div>
        )}

        {/* Error Recovery Action */}
        {error && onRetry && (
          <div className="mt-6 pt-6 border-t border-slate-800 flex justify-center">
            <button
              onClick={onRetry}
              className="px-6 py-3 rounded-xl bg-rose-600 hover:bg-rose-500 text-white font-bold text-sm flex items-center gap-2 shadow-lg shadow-rose-600/30 transition-all cursor-pointer"
            >
              <RefreshCw className="w-4 h-4" />
              <span>Retry Automatic Generation</span>
            </button>
          </div>
        )}
      </div>
    </div>
  );
};
