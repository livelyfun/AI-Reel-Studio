/**
 * Free Visual Asset Search & Retrieval Engine
 * Strictly $0 Budget — Zero Paid API / Zero Image Generation API Quota Usage.
 * Automatically obtains high-definition, royalty-free, creative-commons and public-domain
 * visual assets matching scene descriptions, keywords, and topics.
 */

interface FreeImageResult {
  url: string;
  thumbnailUrl?: string;
  source: "openverse" | "wikimedia" | "unsplash_cdn" | "pexels_cdn";
  title: string;
  author?: string;
}

/**
 * Clean search query into optimal visual search terms
 */
function extractSearchKeywords(prompt: string, keywords: string[] = [], topic?: string): string[] {
  // Remove directorial words that aren't physical subjects
  const cleanPrompt = prompt
    .replace(/\b(cinematic|photorealistic|8k|hyperdetailed|masterpiece|dramatic|lighting|shot|render|ultra|detailed|view|framing|angle|camera|slow|zoom|pan)\b/gi, "")
    .replace(/[^a-zA-Z0-9\s]/g, " ")
    .trim();

  const words = cleanPrompt.split(/\s+/).filter((w) => w.length > 2);
  const primaryKeywords = keywords.filter((k) => k && k.length > 2);

  const queryCandidates: string[] = [];

  // 1. Combined top keywords + physical topic
  if (primaryKeywords.length > 0) {
    queryCandidates.push(primaryKeywords.slice(0, 3).join(" "));
  }

  // 2. Primary clean prompt keywords (top 3-4 words)
  if (words.length > 0) {
    queryCandidates.push(words.slice(0, 4).join(" "));
  }

  // 3. Topic fallback
  if (topic) {
    const cleanTopic = topic
      .replace(/\b(create|make|explain|how|why|the|a|an)\b/gi, "")
      .replace(/[^a-zA-Z0-9\s]/g, " ")
      .trim();
    if (cleanTopic) queryCandidates.push(cleanTopic.split(/\s+/).slice(0, 3).join(" "));
  }

  // 4. Individual keyword fallbacks
  primaryKeywords.forEach((k) => queryCandidates.push(k));

  return Array.from(new Set(queryCandidates.filter(Boolean)));
}

/**
 * Fetch royalty-free images from Openverse API (100% Free, Creative Commons & Public Domain)
 */
async function searchOpenverse(query: string): Promise<FreeImageResult[]> {
  try {
    const url = `https://api.openverse.org/v1/images/?q=${encodeURIComponent(query)}&page_size=8&license_type=commercial,modification`;
    const response = await fetch(url, {
      headers: {
        "User-Agent": "AIReelStudio/1.0 (FreeMediaEngine)",
        Accept: "application/json",
      },
    });

    if (!response.ok) return [];

    const data = await response.json();
    if (!data?.results || !Array.isArray(data.results)) return [];

    const results: FreeImageResult[] = [];
    for (const item of data.results) {
      if (item.url && (item.filetype === "jpg" || item.filetype === "jpeg" || item.filetype === "png" || !item.filetype)) {
        results.push({
          url: item.url,
          thumbnailUrl: item.thumbnail || item.url,
          source: "openverse",
          title: item.title || query,
          author: item.creator,
        });
      }
    }
    return results;
  } catch (err) {
    console.warn(`Openverse query "${query}" failed:`, (err as any)?.message);
    return [];
  }
}

/**
 * Fetch high-resolution CC/Public Domain images from Wikimedia Commons API (100% Free, no API key)
 */
async function searchWikimedia(query: string): Promise<FreeImageResult[]> {
  try {
    const searchUrl = `https://commons.wikimedia.org/w/api.php?action=query&generator=search&gsrsearch=${encodeURIComponent(query)}&gsrnamespace=6&gsrlimit=8&prop=imageinfo&iiprop=url|size|mime&format=json&origin=*`;
    const response = await fetch(searchUrl, {
      headers: {
        "User-Agent": "AIReelStudio/1.0 (EducationalFreeStudio)",
        Accept: "application/json",
      },
    });

    if (!response.ok) return [];

    const data = await response.json();
    const pages = data?.query?.pages;
    if (!pages) return [];

    const results: FreeImageResult[] = [];
    for (const pageId in pages) {
      const page = pages[pageId];
      const imageInfo = page?.imageinfo?.[0];
      if (imageInfo?.url) {
        const mime = imageInfo.mime || "";
        if (mime.includes("image/jpeg") || mime.includes("image/png") || mime.includes("image/webp")) {
          results.push({
            url: imageInfo.url,
            thumbnailUrl: imageInfo.thumburl || imageInfo.url,
            source: "wikimedia",
            title: page.title?.replace(/^File:/i, "") || query,
            author: "Wikimedia Commons Contributor",
          });
        }
      }
    }
    return results;
  } catch (err) {
    console.warn(`Wikimedia query "${query}" failed:`, (err as any)?.message);
    return [];
  }
}

/**
 * Fetch curated high-resolution free images from Unsplash Direct Public Photo CDN
 */
async function searchUnsplashFree(query: string): Promise<FreeImageResult[]> {
  try {
    const searchUrl = `https://unsplash.com/napi/search/photos?query=${encodeURIComponent(query)}&per_page=8`;
    const response = await fetch(searchUrl, {
      headers: {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        Accept: "application/json",
      },
    });

    if (!response.ok) return [];

    const data = await response.json();
    if (!data?.results || !Array.isArray(data.results)) return [];

    const results: FreeImageResult[] = [];
    for (const photo of data.results) {
      const regularUrl = photo?.urls?.regular || photo?.urls?.full || photo?.urls?.small;
      if (regularUrl) {
        results.push({
          url: regularUrl,
          thumbnailUrl: photo?.urls?.thumb || photo?.urls?.small,
          source: "unsplash_cdn",
          title: photo.alt_description || photo.description || query,
          author: photo.user?.name || "Unsplash Creator",
        });
      }
    }
    return results;
  } catch (err) {
    console.warn(`Unsplash CDN query "${query}" failed:`, (err as any)?.message);
    return [];
  }
}

/**
 * Automatically fetch suitable free-to-use visual assets for a scene
 * Tries multiple free providers and queries sequentially.
 * Throws a descriptive error if no suitable free visual asset could be retrieved.
 */
export async function obtainFreeSceneVisual(
  visualPrompt: string,
  keywords: string[] = [],
  topic?: string
): Promise<string> {
  const queryCandidates = extractSearchKeywords(visualPrompt, keywords, topic);

  for (const query of queryCandidates) {
    // 1. Try Unsplash Public CDN
    const unsplashResults = await searchUnsplashFree(query);
    if (unsplashResults.length > 0 && unsplashResults[0].url) {
      return unsplashResults[0].url;
    }

    // 2. Try Openverse CC0 / CC-BY media library
    const openverseResults = await searchOpenverse(query);
    if (openverseResults.length > 0 && openverseResults[0].url) {
      return openverseResults[0].url;
    }

    // 3. Try Wikimedia Commons Public Domain / CC repository
    const wikimediaResults = await searchWikimedia(query);
    if (wikimediaResults.length > 0 && wikimediaResults[0].url) {
      return wikimediaResults[0].url;
    }
  }

  // If still empty, try general topic query
  if (topic) {
    const generalResults = await searchUnsplashFree(topic);
    if (generalResults.length > 0 && generalResults[0].url) {
      return generalResults[0].url;
    }
  }

  throw new Error(
    `Free visual asset search failed: No free-to-use images found for query terms [${queryCandidates.join(", ")}]. Please try adjusting the scene prompt or topic.`
  );
}
