import React from "react";
import { Sparkles, Video, Clapperboard, ShieldCheck } from "lucide-react";

interface HeaderProps {
  onNewReel?: () => void;
  isGenerating?: boolean;
}

export const Header: React.FC<HeaderProps> = ({ onNewReel, isGenerating }) => {
  return (
    <header className="w-full bg-slate-950/80 backdrop-blur-md border-b border-slate-800/80 sticky top-0 z-40">
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 h-16 flex items-center justify-between">
        {/* Brand Logo */}
        <div 
          onClick={!isGenerating ? onNewReel : undefined}
          className="flex items-center gap-3 cursor-pointer group"
          id="brand-header-logo"
        >
          <div className="w-10 h-10 rounded-xl bg-gradient-to-tr from-amber-500 via-rose-500 to-indigo-600 flex items-center justify-center shadow-lg shadow-rose-500/20 group-hover:scale-105 transition-transform duration-200">
            <Clapperboard className="w-5 h-5 text-white" />
          </div>
          <div>
            <div className="flex items-center gap-2">
              <span className="text-lg font-black tracking-tight text-white font-sans">
                AI Reel Studio
              </span>
              <span className="px-2 py-0.5 text-[10px] font-bold uppercase tracking-wider bg-gradient-to-r from-amber-400 to-rose-400 text-slate-950 rounded-full">
                Gemini
              </span>
            </div>
            <p className="text-xs text-slate-400 font-medium hidden sm:block">
              One Description → Instant Finished Video
            </p>
          </div>
        </div>

        {/* Right Status Badges & Quick Action */}
        <div className="flex items-center gap-3">
          <div className="hidden md:flex items-center gap-2 px-3 py-1.5 rounded-full bg-slate-900 border border-slate-800 text-xs text-slate-300 font-medium">
            <ShieldCheck className="w-3.5 h-3.5 text-emerald-400" />
            <span>100% Automated Pipeline</span>
          </div>

          <div className="flex items-center gap-2 px-3 py-1.5 rounded-full bg-slate-900 border border-slate-800 text-xs text-slate-300">
            <span className="w-2 h-2 rounded-full bg-emerald-400 animate-pulse"></span>
            <span className="font-semibold text-slate-200">Auto-Producer Ready</span>
          </div>
        </div>
      </div>
    </header>
  );
};
