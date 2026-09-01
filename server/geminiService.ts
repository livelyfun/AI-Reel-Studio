import { GoogleGenAI, Type } from "@google/genai";
import { ReelRequest, ContentPlan, StoryboardScene, YouTubeMetadata, VisualBible } from "../src/types";
import { obtainFreeSceneVisual } from "./freeAssetService";

let aiClient: GoogleGenAI | null = null;

function getAiClient(): GoogleGenAI {
  if (!aiClient) {
    const apiKey = process.env.GEMINI_API_KEY;
    if (!apiKey) {
      throw new Error(
        "GEMINI_API_KEY environment variable is not configured. Please set your Gemini API key in AI Studio Settings > Secrets."
      );
    }
    aiClient = new GoogleGenAI({
      apiKey,
      httpOptions: {
        headers: {
          "User-Agent": "aistudio-build",
        },
      },
    });
  }
  return aiClient;
}

// Strictly Free Tier Valid Text Model
function getValidTextModel(): string {
  const envModel = process.env.GEMINI_TEXT_MODEL?.trim().replace(/^['"]|['"]$/g, "").replace(/^models\//, "");
  if (envModel && (envModel.startsWith("gemini-") || envModel.startsWith("learnlm-"))) {
    return envModel;
  }
  return "gemini-2.5-flash";
}

const TEXT_MODEL = getValidTextModel();
// Free Tier Gemini TTS Model
const TTS_MODEL = "gemini-3.1-flash-tts-preview";

export interface ParsedGeminiError {
  isQuotaExceeded: boolean;
  isDailyQuota: boolean;
  isRateLimit: boolean;
  is503HighDemand: boolean;
  userFacingMessage: string;
  originalMessage: string;
  retryAfter?: string;
  quotaMetric?: string;
  quotaId?: string;
  limit?: string | number;
}

/**
 * Robust parser for Gemini API errors detecting HTTP 429, daily free tier quota,
 * retry timers, and temporary 503 high-demand conditions.
 */
export function parseGeminiError(error: any): ParsedGeminiError {
  const rawMsg = typeof error === "string" ? error : error?.message || JSON.stringify(error || {});
  let jsonErrorObj: any = null;

  try {
    if (typeof error?.message === "string" && error.message.startsWith("{")) {
      jsonErrorObj = JSON.parse(error.message);
    } else if (error?.error) {
      jsonErrorObj = error;
    }
  } catch (_) {}

  const errObj = jsonErrorObj?.error || jsonErrorObj || {};
  const status = String(errObj.status || error?.status || "");
  const code = Number(errObj.code || error?.code || error?.status || 0);
  const fullText = (rawMsg + " " + JSON.stringify(errObj)).toLowerCase();

  const is503HighDemand =
    code === 503 ||
    status === "UNAVAILABLE" ||
    fullText.includes("503") ||
    fullText.includes("high demand") ||
    fullText.includes("temporary capacity") ||
    fullText.includes("unavailable");

  const is429 =
    code === 429 ||
    status === "RESOURCE_EXHAUSTED" ||
    fullText.includes("429") ||
    fullText.includes("resource_exhausted") ||
    fullText.includes("quota exceeded") ||
    fullText.includes("rate-limits");

  // Detect specifically if Daily Free Tier request quota has been reached (e.g. 10 requests/day)
  const isDailyQuota =
    is429 &&
    (fullText.includes("generaterequestsperday") ||
      fullText.includes("perday") ||
      fullText.includes("daily") ||
      fullText.includes("limit: 10") ||
      fullText.includes("quotavalue\": \"10\"") ||
      fullText.includes("quotavalue\":\"10\"") ||
      fullText.includes("free_tier_requests"));

  // Extract retry delay / remaining information if provided by Gemini API
  let retryAfter: string | undefined;
  const retryInfo = errObj.details?.find?.(
    (d: any) => d["@type"]?.includes("RetryInfo") || d.retryDelay
  );
  if (retryInfo?.retryDelay) {
    retryAfter = String(retryInfo.retryDelay);
  } else {
    const match = rawMsg.match(/retry in\s+([0-9a-zA-Z\.\_]+)/i) || rawMsg.match(/retry after\s+([0-9a-zA-Z\.\_]+)/i);
    if (match && match[1]) {
      retryAfter = match[1];
    }
  }

  let quotaMetric: string | undefined;
  let quotaId: string | undefined;
  let limit: string | number | undefined;

  const quotaFailure = errObj.details?.find?.(
    (d: any) => d["@type"]?.includes("QuotaFailure") || d.violations
  );
  if (quotaFailure?.violations?.[0]) {
    const v = quotaFailure.violations[0];
    quotaMetric = v.quotaMetric;
    quotaId = v.quotaId;
    limit = v.quotaValue;
  }

  let userFacingMessage = rawMsg;
  if (isDailyQuota) {
    userFacingMessage = "Gemini Free Tier TTS quota has been reached. No more TTS requests will be made until the quota resets.";
    if (retryAfter) {
      userFacingMessage += ` (Estimated retry/reset time: ${retryAfter})`;
    }
  } else if (is429) {
    userFacingMessage = "Gemini API rate limit exceeded. Please wait a moment before trying again.";
    if (retryAfter) {
      userFacingMessage += ` (Retry after ${retryAfter})`;
    }
  } else if (is503HighDemand) {
    userFacingMessage = "Gemini model is currently experiencing temporary high demand. Please try again shortly.";
  }

  return {
    isQuotaExceeded: is429,
    isDailyQuota,
    isRateLimit: is429 && !isDailyQuota,
    is503HighDemand,
    userFacingMessage,
    originalMessage: rawMsg,
    retryAfter,
    quotaMetric,
    quotaId,
    limit: limit || (isDailyQuota ? 10 : undefined),
  };
}

/**
 * Small, limited retry strategy for transient 503 high demand errors only.
 * CRITICAL: NEVER retries 429 Daily Quota Exhaustion!
 */
async function callWithRetry<T>(fn: () => Promise<T>, maxRetries = 2, initialDelayMs = 1500): Promise<T> {
  let lastError: any;
  for (let attempt = 0; attempt <= maxRetries; attempt++) {
    try {
      return await fn();
    } catch (err: any) {
      lastError = err;
      const parsed = parseGeminiError(err);

      // Do NOT repeatedly retry when the daily quota has been exhausted!
      if (parsed.isDailyQuota) {
        console.warn(`[Gemini API] Daily Free Tier Quota reached. Aborting immediately without retry.`);
        throw err;
      }

      // Only retry transient 503 "model experiencing high demand" or transient rate-limit spikes
      const isTransient = parsed.is503HighDemand || parsed.isRateLimit;
      if (isTransient && attempt < maxRetries) {
        const delay = initialDelayMs * Math.pow(2, attempt);
        console.warn(`[Gemini API] Transient capacity notice (${parsed.userFacingMessage}). Retrying in ${delay}ms (attempt ${attempt + 1}/${maxRetries})...`);
        await new Promise((resolve) => setTimeout(resolve, delay));
        continue;
      }
      throw err;
    }
  }
  throw lastError;
}

/**
 * Server-side in-memory Narration Audio Cache
 * Ensures that identical narration + voiceStyle combination NEVER triggers another TTS API call
 */
interface CachedAudioEntry {
  audioBase64: string;
  voiceName: string;
  durationEstimate: number;
  createdAt: number;
  textHash: string;
}

const narrationAudioCache = new Map<string, CachedAudioEntry>();

function normalizeCacheKey(text: string, voiceStyle: string): string {
  return `${voiceStyle.trim().toLowerCase()}:::${text.trim().replace(/\s+/g, " ").toLowerCase()}`;
}

export function getCachedVoiceover(text: string, voiceStyle: string): CachedAudioEntry | null {
  const key = normalizeCacheKey(text, voiceStyle);
  return narrationAudioCache.get(key) || null;
}

export function setCachedVoiceover(
  text: string,
  voiceStyle: string,
  entry: { audioBase64: string; voiceName: string; durationEstimate: number }
): CachedAudioEntry {
  const key = normalizeCacheKey(text, voiceStyle);
  const fullEntry: CachedAudioEntry = {
    ...entry,
    createdAt: Date.now(),
    textHash: key,
  };
  narrationAudioCache.set(key, fullEntry);
  return fullEntry;
}

/**
 * Step 1, 2, 3 & 5: Understand Idea, Create Content Plan, Storyboard & YouTube Metadata
 * Uses Gemini Free Tier Text Model
 */
export async function generatePlanAndStoryboard(req: ReelRequest): Promise<{
  contentPlan: ContentPlan;
  storyboard: StoryboardScene[];
  metadata: YouTubeMetadata;
}> {
  const ai = getAiClient();

  // Determine scene count based on duration (approx 4-6 seconds per scene)
  const targetDuration = req.duration || 30;
  const targetScenes = Math.max(3, Math.min(8, Math.round(targetDuration / 5)));

  const systemInstruction = `You are the world's top viral short-form video director, scriptwriter, and creative producer.
Your job is to take a single user prompt and transform it into an ultra-engaging, high-retention video reel (for YouTube Shorts, TikTok, Instagram Reels).

You must adhere strictly to these principles:
1. HOOK: The first 3 seconds MUST immediately grab attention with a curiosity gap, shocking visual contrast, or bold question.
2. VISUAL BIBLE: To maintain visual consistency across all scenes, specify strict environment palette, lighting style, and camera aesthetics.
3. SCENE BREAKDOWN: Generate exactly ${targetScenes} scenes that total exactly ${targetDuration} seconds.
4. NARRATION: Spoken-word conversational script without fluff. Words that sound impactful and natural when spoken aloud.
5. ON-SCREEN TEXT & KEYWORDS: Provide 2-5 impactful words to display on screen per scene, and identify 1-3 highlighted keywords.
6. YOUTUBE / SOCIAL METADATA: Create high-CTR YouTube titles, a rich formatted description with timestamps, tags, and hashtags.

Output everything in clean structured JSON.`;

  const promptText = `USER REQUEST:
- Description: "${req.prompt}"
- Voice Style: ${req.voiceStyle}
- Visual Style: ${req.visualStyle}
- Target Duration: ${targetDuration} seconds
- Target Scene Count: ${targetScenes} scenes
- Aspect Ratio: ${req.aspectRatio}

Generate the complete Content Plan, Visual Bible, Scene-by-Scene Storyboard, and YouTube Metadata.`;

  const response = await callWithRetry(() =>
    ai.models.generateContent({
      model: TEXT_MODEL,
      contents: promptText,
      config: {
        systemInstruction,
        temperature: 0.7,
        responseMimeType: "application/json",
        responseSchema: {
          type: Type.OBJECT,
          properties: {
            contentPlan: {
              type: Type.OBJECT,
              properties: {
                topic: { type: Type.STRING },
                purpose: { type: Type.STRING },
                targetAudience: { type: Type.STRING },
                tone: { type: Type.STRING },
                storyStructure: { type: Type.STRING },
                hook: { type: Type.STRING },
                ending: { type: Type.STRING },
                callToAction: { type: Type.STRING },
                pacing: { type: Type.STRING },
                sceneCount: { type: Type.INTEGER },
                estimatedTotalDuration: { type: Type.NUMBER },
                visualBible: {
                  type: Type.OBJECT,
                  properties: {
                    characterAppearance: { type: Type.STRING },
                    clothing: { type: Type.STRING },
                    environment: { type: Type.STRING },
                    locations: { type: Type.STRING },
                    objects: { type: Type.STRING },
                    visualStyle: { type: Type.STRING },
                    lighting: { type: Type.STRING },
                    colorPalette: {
                      type: Type.ARRAY,
                      items: { type: Type.STRING },
                    },
                    cameraStyle: { type: Type.STRING },
                    overallAtmosphere: { type: Type.STRING },
                  },
                  required: ["environment", "visualStyle", "lighting", "cameraStyle", "overallAtmosphere"],
                },
              },
              required: ["topic", "purpose", "targetAudience", "tone", "hook", "ending", "visualBible"],
            },
            storyboard: {
              type: Type.ARRAY,
              items: {
                type: Type.OBJECT,
                properties: {
                  scene_id: { type: Type.INTEGER },
                  duration: { type: Type.NUMBER },
                  narration: { type: Type.STRING },
                  visual_description: { type: Type.STRING },
                  visual_prompt: { type: Type.STRING },
                  camera: { type: Type.STRING },
                  lighting: { type: Type.STRING },
                  transition: { type: Type.STRING },
                  on_screen_text: { type: Type.STRING },
                  keywords: {
                    type: Type.ARRAY,
                    items: { type: Type.STRING },
                  },
                },
                required: ["scene_id", "duration", "narration", "visual_prompt", "camera", "lighting", "transition", "on_screen_text"],
              },
            },
            metadata: {
              type: Type.OBJECT,
              properties: {
                reelName: { type: Type.STRING },
                youtubeTitle: { type: Type.STRING },
                youtubeDescription: { type: Type.STRING },
                tags: {
                  type: Type.ARRAY,
                  items: { type: Type.STRING },
                },
                hashtags: {
                  type: Type.ARRAY,
                  items: { type: Type.STRING },
                },
                hookAnalysis: { type: Type.STRING },
                audienceFit: { type: Type.STRING },
              },
              required: ["reelName", "youtubeTitle", "youtubeDescription", "tags", "hashtags"],
            },
          },
          required: ["contentPlan", "storyboard", "metadata"],
        },
      },
    })
  );

  if (!response.text) {
    throw new Error("Gemini API returned an empty response during script & storyboard generation.");
  }

  const parsed = JSON.parse(response.text);

  // Format & calculate word timings per scene
  const formattedScenes: StoryboardScene[] = (parsed.storyboard || []).map((scene: any, idx: number) => {
    const duration = Number(scene.duration) || (targetDuration / targetScenes);
    const narration = scene.narration || "";
    const words = narration.split(/\s+/).filter(Boolean);
    const wordCount = Math.max(1, words.length);
    const timePerWord = duration / wordCount;
    const keywords = scene.keywords || [];

    const wordTimings = words.map((word: string, wIdx: number) => {
      const cleanWord = word.replace(/[^a-zA-Z0-9]/g, "").toLowerCase();
      const isKeyword = keywords.some(
        (k: string) => cleanWord.includes(k.toLowerCase()) || k.toLowerCase().includes(cleanWord)
      );
      return {
        word,
        start: Number((wIdx * timePerWord).toFixed(2)),
        end: Number(((wIdx + 1) * timePerWord).toFixed(2)),
        isKeyword,
      };
    });

    return {
      scene_id: scene.scene_id || idx + 1,
      duration: Math.round(duration),
      narration,
      visual_description: scene.visual_description || scene.visual_prompt,
      visual_prompt: scene.visual_prompt,
      camera: scene.camera || "Dynamic slow zoom in",
      lighting: scene.lighting || "Cinematic volumetric lighting",
      transition: scene.transition || "crossfade",
      on_screen_text: scene.on_screen_text || words.slice(0, 3).join(" ").toUpperCase(),
      subtitle_text: narration,
      keywords: keywords,
      wordTimings,
    };
  });

  return {
    contentPlan: parsed.contentPlan,
    storyboard: formattedScenes,
    metadata: parsed.metadata,
  };
}

/**
 * Step 4: Automatically obtain suitable free-to-use visual assets
 * Strict $0 budget — NO paid image-generation models, NO fake SVGs.
 */
export async function generateSceneVisualImage(
  visualPrompt: string,
  _visualBible: VisualBible,
  _visualStyle: string,
  _aspectRatio: string = "9:16",
  keywords: string[] = [],
  topic?: string
): Promise<string> {
  // Obtain high-definition royalty-free image matching scene description and keywords
  return await obtainFreeSceneVisual(visualPrompt, keywords, topic);
}

/**
 * Convert 24kHz 16-bit Mono PCM buffer to standard WAV buffer with 44-byte header
 */
function pcmToWavBuffer(pcmData: Buffer, sampleRate = 24000, numChannels = 1, bitsPerSample = 16): Buffer {
  // Check if buffer is already a RIFF/WAV file
  if (pcmData.length >= 4 && pcmData.subarray(0, 4).toString("ascii") === "RIFF") {
    return pcmData;
  }

  const byteRate = (sampleRate * numChannels * bitsPerSample) / 8;
  const blockAlign = (numChannels * bitsPerSample) / 8;
  const dataSize = pcmData.length;
  const header = Buffer.alloc(44);

  header.write("RIFF", 0);
  header.writeUInt32LE(36 + dataSize, 4);
  header.write("WAVE", 8);
  header.write("fmt ", 12);
  header.writeUInt32LE(16, 16); // Subchunk1Size
  header.writeUInt16LE(1, 20); // PCM format
  header.writeUInt16LE(numChannels, 22);
  header.writeUInt32LE(sampleRate, 24);
  header.writeUInt32LE(byteRate, 28);
  header.writeUInt16LE(blockAlign, 32);
  header.writeUInt16LE(bitsPerSample, 34);
  header.write("data", 36);
  header.writeUInt32LE(dataSize, 40);

  return Buffer.concat([header, pcmData]);
}

/**
 * Step 6: Generate TTS Voiceover audio using Gemini Free Tier TTS model (gemini-3.1-flash-tts-preview)
 * Integrates smart single-request continuous generation and server-side audio caching.
 */
export async function generateVoiceoverAudio(
  text: string,
  voiceStyle: string,
  forceRegenerate = false
): Promise<{
  audioBase64: string;
  voiceName: string;
  durationEstimate: number;
  fromCache: boolean;
}> {
  const cleanText = text.trim();
  if (!cleanText) {
    throw new Error("Narration text is empty for TTS generation.");
  }

  // 1. Check in-memory audio cache first (Prevents duplicate TTS calls)
  if (!forceRegenerate) {
    const cached = getCachedVoiceover(cleanText, voiceStyle);
    if (cached) {
      console.log(`[Gemini TTS Cache] Reusing cached voiceover audio for "${cleanText.slice(0, 35)}..." (${voiceStyle})`);
      return {
        audioBase64: cached.audioBase64,
        voiceName: cached.voiceName,
        durationEstimate: cached.durationEstimate,
        fromCache: true,
      };
    }
  }

  const ai = getAiClient();

  // Select Gemini prebuilt voice based on user preference
  const voiceMap: Record<string, string> = {
    Deep: "Fenrir",
    Dramatic: "Fenrir",
    Cinematic: "Zephyr",
    Documentary: "Zephyr",
    Energetic: "Puck",
    Storytelling: "Puck",
    Motivational: "Puck",
    Calm: "Kore",
    Friendly: "Kore",
    News: "Charon",
  };

  const selectedVoice = voiceMap[voiceStyle] || "Zephyr";
  const words = cleanText.split(/\s+/).filter(Boolean).length;
  const durationEstimate = Math.max(2, words / 2.5);

  try {
    const response = await callWithRetry(() =>
      ai.models.generateContent({
        model: TTS_MODEL,
        contents: [{ parts: [{ text: `Say in a ${voiceStyle.toLowerCase()} tone: ${cleanText}` }] }],
        config: {
          responseModalities: ["AUDIO" as any],
          speechConfig: {
            voiceConfig: {
              prebuiltVoiceConfig: { voiceName: selectedVoice },
            },
          },
        },
      })
    );

    const rawBase64 = response.candidates?.[0]?.content?.parts?.[0]?.inlineData?.data;
    if (!rawBase64) {
      throw new Error(
        `Gemini TTS (${TTS_MODEL}) did not return audio data for narration: "${cleanText.slice(0, 40)}..."`
      );
    }

    // Convert raw PCM to standard WAV
    const rawPcmBuffer = Buffer.from(rawBase64, "base64");
    const wavBuffer = pcmToWavBuffer(rawPcmBuffer, 24000, 1, 16);
    const audioBase64 = wavBuffer.toString("base64");

    // Cache generated voiceover
    setCachedVoiceover(cleanText, voiceStyle, {
      audioBase64,
      voiceName: selectedVoice,
      durationEstimate,
    });

    return {
      audioBase64,
      voiceName: selectedVoice,
      durationEstimate,
      fromCache: false,
    };
  } catch (err: any) {
    const parsed = parseGeminiError(err);
    console.error(`[Gemini TTS Error] ${parsed.userFacingMessage}`);
    const wrappedErr: any = new Error(parsed.userFacingMessage);
    wrappedErr.parsedGeminiError = parsed;
    wrappedErr.status = parsed.isQuotaExceeded ? 429 : 500;
    throw wrappedErr;
  }
}
