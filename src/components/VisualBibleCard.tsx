import React from "react";
import { Palette, Eye, Sun, Camera, Compass, Users, Sparkles } from "lucide-react";
import { VisualBible, ContentPlan } from "../types";

interface VisualBibleCardProps {
  visualBible: VisualBible;
  contentPlan: ContentPlan;
}

export const VisualBibleCard: React.FC<VisualBibleCardProps> = ({
  visualBible,
  contentPlan,
}) => {
  return (
    <div className="w-full max-w-5xl mx-auto py-6 px-4 sm:px-6">
      <div className="bg-slate-900/90 border border-slate-800 rounded-3xl p-6 sm:p-8 shadow-2xl backdrop-blur-xl">
        <div className="flex items-center justify-between pb-4 border-b border-slate-800 mb-6">
          <div className="flex items-center gap-3">
            <div className="w-10 h-10 rounded-xl bg-indigo-600/10 border border-indigo-600/20 flex items-center justify-center">
              <Palette className="w-5 h-5 text-indigo-400" />
            </div>
            <div>
              <h3 className="text-xl font-bold text-white tracking-tight">
                Visual Bible & Production Directives
              </h3>
              <p className="text-xs text-slate-400">
                Cross-scene consistency rules synthesized by Gemini to unify characters, lighting, and art direction.
              </p>
            </div>
          </div>
          <span className="px-3 py-1 rounded-full bg-indigo-500/10 border border-indigo-500/20 text-indigo-300 text-xs font-semibold">
            {contentPlan.pacing}
          </span>
        </div>

        {/* Content Plan Directorial Grid */}
        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 mb-6">
          <div className="bg-slate-950/60 p-4 rounded-2xl border border-slate-800/80">
            <div className="flex items-center gap-2 text-rose-400 text-xs font-bold uppercase tracking-wider mb-1.5">
              <Sparkles className="w-3.5 h-3.5" />
              <span>Viral Hook Strategy</span>
            </div>
            <p className="text-xs text-slate-200 leading-relaxed">{contentPlan.hook}</p>
          </div>

          <div className="bg-slate-950/60 p-4 rounded-2xl border border-slate-800/80">
            <div className="flex items-center gap-2 text-amber-400 text-xs font-bold uppercase tracking-wider mb-1.5">
              <Users className="w-3.5 h-3.5" />
              <span>Target Audience</span>
            </div>
            <p className="text-xs text-slate-200 leading-relaxed">{contentPlan.targetAudience}</p>
          </div>

          <div className="bg-slate-950/60 p-4 rounded-2xl border border-slate-800/80">
            <div className="flex items-center gap-2 text-emerald-400 text-xs font-bold uppercase tracking-wider mb-1.5">
              <Compass className="w-3.5 h-3.5" />
              <span>Story Structure</span>
            </div>
            <p className="text-xs text-slate-200 leading-relaxed">{contentPlan.storyStructure}</p>
          </div>
        </div>

        {/* Visual Consistency Guide */}
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
          <div className="bg-slate-950/40 p-3.5 rounded-xl border border-slate-800/60">
            <span className="text-[10px] font-bold uppercase tracking-wider text-slate-500 block mb-1">
              Environment & Setting
            </span>
            <p className="text-xs text-slate-300 font-medium">{visualBible.environment}</p>
          </div>

          <div className="bg-slate-950/40 p-3.5 rounded-xl border border-slate-800/60">
            <span className="text-[10px] font-bold uppercase tracking-wider text-slate-500 block mb-1">
              Lighting & Grading
            </span>
            <p className="text-xs text-slate-300 font-medium">{visualBible.lighting}</p>
          </div>

          <div className="bg-slate-950/40 p-3.5 rounded-xl border border-slate-800/60">
            <span className="text-[10px] font-bold uppercase tracking-wider text-slate-500 block mb-1">
              Camera Movement
            </span>
            <p className="text-xs text-slate-300 font-medium">{visualBible.cameraStyle}</p>
          </div>

          <div className="bg-slate-950/40 p-3.5 rounded-xl border border-slate-800/60">
            <span className="text-[10px] font-bold uppercase tracking-wider text-slate-500 block mb-1">
              Overall Atmosphere
            </span>
            <p className="text-xs text-slate-300 font-medium">{visualBible.overallAtmosphere}</p>
          </div>
        </div>

        {/* Color Palette Swatches */}
        {visualBible.colorPalette && visualBible.colorPalette.length > 0 && (
          <div className="mt-6 pt-4 border-t border-slate-800 flex items-center gap-3">
            <span className="text-xs font-bold uppercase tracking-wider text-slate-400">
              Cohesive Palette:
            </span>
            <div className="flex items-center gap-2">
              {visualBible.colorPalette.map((color, idx) => (
                <div key={idx} className="flex items-center gap-1.5 bg-slate-950 px-2 py-1 rounded-lg border border-slate-800">
                  <div
                    className="w-3.5 h-3.5 rounded-full border border-white/20"
                    style={{ backgroundColor: color }}
                  />
                  <span className="text-[10px] font-mono text-slate-400">{color}</span>
                </div>
              ))}
            </div>
          </div>
        )}
      </div>
    </div>
  );
};
