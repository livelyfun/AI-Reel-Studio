import { StoryboardScene, WordTiming } from "../types";

/**
 * Format seconds to SRT timestamp (00:00:00,000)
 */
export function formatSrtTime(seconds: number): string {
  const pad = (num: number, size: number) => num.toString().padStart(size, "0");
  const hrs = Math.floor(seconds / 3600);
  const mins = Math.floor((seconds % 3600) / 60);
  const secs = Math.floor(seconds % 60);
  const millis = Math.floor((seconds % 1) * 1000);
  return `${pad(hrs, 2)}:${pad(mins, 2)}:${pad(secs, 2)},${pad(millis, 3)}`;
}

/**
 * Format seconds to WebVTT timestamp (00:00:00.000)
 */
export function formatVttTime(seconds: number): string {
  return formatSrtTime(seconds).replace(",", ".");
}

/**
 * Generate standard SubRip (.SRT) subtitle file content
 */
export function generateSrt(storyboard: StoryboardScene[]): string {
  let srt = "";
  let currentTime = 0;
  let index = 1;

  for (const scene of storyboard) {
    const startTime = currentTime;
    const endTime = currentTime + scene.duration;

    srt += `${index}\n`;
    srt += `${formatSrtTime(startTime)} --> ${formatSrtTime(endTime)}\n`;
    srt += `${scene.narration.trim()}\n\n`;

    currentTime = endTime;
    index++;
  }

  return srt;
}

/**
 * Generate WebVTT (.VTT) subtitle file content
 */
export function generateVtt(storyboard: StoryboardScene[]): string {
  let vtt = "WEBVTT\n\n";
  let currentTime = 0;
  let index = 1;

  for (const scene of storyboard) {
    const startTime = currentTime;
    const endTime = currentTime + scene.duration;

    vtt += `${index}\n`;
    vtt += `${formatVttTime(startTime)} --> ${formatVttTime(endTime)}\n`;
    vtt += `${scene.narration.trim()}\n\n`;

    currentTime = endTime;
    index++;
  }

  return vtt;
}

/**
 * Get active words and active keyword for current scene playback time
 */
export function getActiveCaptionState(
  scene: StoryboardScene,
  sceneTime: number
): {
  allWords: string[];
  activeWordIndex: number;
  activeWord: string;
  isKeywordActive: boolean;
} {
  const words = scene.wordTimings;
  if (!words || words.length === 0) {
    const splitWords = scene.narration.split(/\s+/).filter(Boolean);
    const estIndex = Math.min(
      splitWords.length - 1,
      Math.floor((sceneTime / Math.max(0.1, scene.duration)) * splitWords.length)
    );
    return {
      allWords: splitWords,
      activeWordIndex: Math.max(0, estIndex),
      activeWord: splitWords[estIndex] || "",
      isKeywordActive: false,
    };
  }

  let activeIndex = -1;
  for (let i = 0; i < words.length; i++) {
    if (sceneTime >= words[i].start && sceneTime <= words[i].end) {
      activeIndex = i;
      break;
    }
  }

  if (activeIndex === -1) {
    // If past all words or before first
    if (sceneTime > (words[words.length - 1]?.end || 0)) {
      activeIndex = words.length - 1;
    } else {
      activeIndex = 0;
    }
  }

  const activeWordObj = words[activeIndex];

  return {
    allWords: words.map((w) => w.word),
    activeWordIndex: activeIndex,
    activeWord: activeWordObj ? activeWordObj.word : "",
    isKeywordActive: Boolean(activeWordObj?.isKeyword),
  };
}
