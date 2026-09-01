import express from "express";
import path from "path";
import { createServer as createViteServer } from "vite";
import dotenv from "dotenv";
import {
  generatePlanAndStoryboard,
  generateSceneVisualImage,
  generateVoiceoverAudio,
} from "./server/geminiService";
import { getRecommendedMusic } from "./server/audioSynth";
import { ReelRequest, GeneratedReel, StoryboardScene } from "./src/types";

dotenv.config();

async function startServer() {
  const app = express();
  const PORT = 3000;

  app.use(express.json({ limit: "50mb" }));
  app.use(express.urlencoded({ extended: true, limit: "50mb" }));

  // Health check
  app.get("/api/health", (_req, res) => {
    res.json({
      status: "ok",
      geminiKeyConfigured: Boolean(process.env.GEMINI_API_KEY),
      timestamp: new Date().toISOString(),
    });
  });

  // Step 1, 2, 3: Create Content Plan & Storyboard
  app.post("/api/reel/plan", async (req, res) => {
    try {
      const reelRequest: ReelRequest = req.body;
      const result = await generatePlanAndStoryboard(reelRequest);
      res.json({ success: true, data: result });
    } catch (error: any) {
      console.error("Error in /api/reel/plan:", error);
      res.status(500).json({ success: false, error: error.message || "Failed to create content plan" });
    }
  });

  // Step 4 & 5: Generate single scene visual (automatically obtains free visual asset)
  app.post("/api/reel/generate-scene-visual", async (req, res) => {
    try {
      const { visualPrompt, visualBible, visualStyle, aspectRatio, keywords, topic } = req.body;
      const imageUrl = await generateSceneVisualImage(
        visualPrompt,
        visualBible,
        visualStyle,
        aspectRatio,
        keywords || [],
        topic
      );
      res.json({ success: true, imageUrl });
    } catch (error: any) {
      console.error("Error in /api/reel/generate-scene-visual:", error);
      res.status(500).json({ success: false, error: error.message || "Failed to obtain scene visual" });
    }
  });

  // Step 6: Generate voiceover audio using Gemini Free TTS (gemini-3.1-flash-tts-preview)
  // Supports single continuous narration request, caching, and quota error detection
  app.post("/api/reel/generate-voiceover", async (req, res) => {
    try {
      const { text, voiceStyle, forceRegenerate } = req.body;
      const voiceover = await generateVoiceoverAudio(text, voiceStyle, Boolean(forceRegenerate));
      res.json({ success: true, voiceover });
    } catch (error: any) {
      console.error("Error in /api/reel/generate-voiceover:", error);
      const parsed = error.parsedGeminiError;
      const statusCode = error.status || (parsed?.isQuotaExceeded ? 429 : 500);
      
      res.status(statusCode).json({
        success: false,
        isQuotaExceeded: Boolean(parsed?.isQuotaExceeded),
        isDailyQuota: Boolean(parsed?.isDailyQuota),
        is503HighDemand: Boolean(parsed?.is503HighDemand),
        error: parsed?.userFacingMessage || error.message || "Failed to generate voiceover with Gemini Free TTS",
        retryAfter: parsed?.retryAfter,
        quotaMetric: parsed?.quotaMetric,
        limit: parsed?.limit,
      });
    }
  });

  // Regenerate individual scene visual and optionally voiceover
  app.post("/api/reel/regenerate-scene", async (req, res) => {
    try {
      const { scene, visualBible, visualStyle, voiceStyle, aspectRatio, topic, regenerateVoice } = req.body;
      const imageUrl = await generateSceneVisualImage(
        scene.visual_prompt,
        visualBible,
        visualStyle,
        aspectRatio,
        scene.keywords || [],
        topic
      );

      let audioUrl = scene.audioUrl;
      // Only call TTS if explicitly requested to regenerate voice
      if (regenerateVoice) {
        try {
          const voiceover = await generateVoiceoverAudio(scene.narration, voiceStyle, true);
          audioUrl = voiceover.audioBase64 ? `data:audio/wav;base64,${voiceover.audioBase64}` : audioUrl;
        } catch (e: any) {
          console.warn("Could not regenerate scene voiceover:", e.message);
        }
      }

      const updatedScene: StoryboardScene = {
        ...scene,
        imageUrl,
        audioUrl,
      };

      res.json({ success: true, scene: updatedScene });
    } catch (error: any) {
      console.error("Error in /api/reel/regenerate-scene:", error);
      res.status(500).json({ success: false, error: error.message || "Failed to regenerate scene" });
    }
  });

  // COMPLETE AUTOMATED PIPELINE ENDPOINT
  // Executes entire pipeline server-side and returns the fully assembled reel package
  // Uses ONE single TTS call for the full narration and reuses cached audio
  app.post("/api/reel/full-pipeline", async (req, res) => {
    try {
      const reelRequest: ReelRequest = req.body;

      // 1. Content Plan & Storyboard
      const planResult = await generatePlanAndStoryboard(reelRequest);
      const { contentPlan, storyboard, metadata } = planResult;

      // 2. Generate visuals for all scenes with free asset resolver ($0 API cost)
      const enrichedScenes: StoryboardScene[] = await Promise.all(
        storyboard.map(async (scene) => {
          let img = "";
          try {
            img = await generateSceneVisualImage(
              scene.visual_prompt,
              contentPlan.visualBible,
              reelRequest.visualStyle,
              reelRequest.aspectRatio,
              scene.keywords || [],
              contentPlan.topic
            );
          } catch (e) {
            console.error(`Failed to generate visual for scene ${scene.scene_id}:`, e);
          }

          return {
            ...scene,
            imageUrl: img || undefined,
          };
        })
      );

      // 3. Synthesize Voiceover: ONE SINGLE request for full continuous narration
      const fullNarrationText = enrichedScenes.map((s) => s.narration).filter(Boolean).join(" ");
      let fullNarrationAudioUrl: string | undefined = undefined;
      let ttsStatus: any = { success: true };

      try {
        const voiceover = await generateVoiceoverAudio(fullNarrationText, reelRequest.voiceStyle, false);
        if (voiceover.audioBase64) {
          fullNarrationAudioUrl = `data:audio/wav;base64,${voiceover.audioBase64}`;
          ttsStatus = { success: true, fromCache: voiceover.fromCache };
        }
      } catch (voiceErr: any) {
        console.warn("TTS Notice during full pipeline:", voiceErr.message);
        const parsed = voiceErr.parsedGeminiError;
        ttsStatus = {
          success: false,
          quotaExceeded: Boolean(parsed?.isQuotaExceeded),
          isDailyQuota: Boolean(parsed?.isDailyQuota),
          message: parsed?.userFacingMessage || voiceErr.message,
          retryAfter: parsed?.retryAfter,
        };
      }

      // Attach audio URL to scenes if available
      const scenesWithAudio = enrichedScenes.map((sc) => ({
        ...sc,
        audioUrl: fullNarrationAudioUrl || undefined,
      }));

      // 4. Recommended background music
      const recommendedMusic = getRecommendedMusic(reelRequest.voiceStyle, reelRequest.visualStyle);

      // 5. Calculate total duration
      const totalDuration = scenesWithAudio.reduce((acc, s) => acc + s.duration, 0);

      // 6. Build completed reel object
      const fullReel: GeneratedReel = {
        id: `reel_${Date.now()}_${Math.random().toString(36).substr(2, 6)}`,
        createdAt: new Date().toISOString(),
        request: reelRequest,
        contentPlan,
        storyboard: scenesWithAudio,
        metadata,
        totalDuration,
        backgroundMusicGenre: recommendedMusic.name,
        thumbnailUrl: scenesWithAudio[0]?.imageUrl,
        captionStyle: "tiktok_bold",
        fullNarrationText,
        fullNarrationAudioUrl,
        ttsStatus,
      };

      res.json({ success: true, reel: fullReel });
    } catch (error: any) {
      console.error("Pipeline execution failed:", error);
      res.status(500).json({ success: false, error: error.message || "Pipeline failed" });
    }
  });

  // Vite middleware for development
  if (process.env.NODE_ENV !== "production") {
    const vite = await createViteServer({
      server: { middlewareMode: true },
      appType: "spa",
    });
    app.use(vite.middlewares);
  } else {
    const distPath = path.join(process.cwd(), "dist");
    app.use(express.static(distPath));
    app.get("*", (_req, res) => {
      res.sendFile(path.join(distPath, "index.html"));
    });
  }

  app.listen(PORT, "0.0.0.0", () => {
    console.log(`AI Reel Studio server running on http://0.0.0.0:${PORT}`);
  });
}

startServer();
