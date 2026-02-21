// KatanaReaderNX - Native MangaKatana Reader for Nintendo Switch
// Phase 3: SDL2 Portrait Mode Image Renderer

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <curl/curl.h>
#include <regex>
#include <string>
#include <switch.h>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────────────────────────────────────
SDL_Window *gWindow = nullptr;
SDL_Renderer *gRenderer = nullptr;

std::vector<std::string> chapterImages;
std::vector<SDL_Texture *> chapterTextures;

// ─────────────────────────────────────────────────────────────────────────────
// libcurl helpers
// ─────────────────────────────────────────────────────────────────────────────
struct MemoryBuffer {
  std::vector<uint8_t> data;
};

size_t WriteCallbackStr(void *contents, size_t size, size_t nmemb,
                        void *userp) {
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
}

size_t WriteCallbackBin(void *contents, size_t size, size_t nmemb,
                        void *userp) {
  auto *buf = (MemoryBuffer *)userp;
  uint8_t *ptr = (uint8_t *)contents;
  buf->data.insert(buf->data.end(), ptr, ptr + size * nmemb);
  return size * nmemb;
}

// ─────────────────────────────────────────────────────────────────────────────
// HTML Parser – extract image URLs from MangaKatana JS arrays
// ─────────────────────────────────────────────────────────────────────────────
bool extractMangaKatanaImages(const std::string &html) {
  chapterImages.clear();

  std::regex arrayRegex(R"(var\s+[a-zA-Z_]\w*\s*=\s*\[(.*?)\];)");
  std::smatch match;
  std::string::const_iterator searchStart(html.cbegin());

  while (std::regex_search(searchStart, html.cend(), match, arrayRegex)) {
    std::string arrayContent = match[1];

    if (arrayContent.find("imgs") != std::string::npos ||
        arrayContent.find("http") != std::string::npos) {

      std::regex urlRegex(R"('(.*?)')");
      std::smatch urlMatch;
      std::string::const_iterator us(arrayContent.cbegin());

      while (std::regex_search(us, arrayContent.cend(), urlMatch, urlRegex)) {
        std::string url = urlMatch[1];
        if (url.rfind("http", 0) == 0 &&
            url.find("mangakatana.com/imgs") != std::string::npos) {
          chapterImages.push_back(url);
        }
        us = urlMatch.suffix().first;
      }
      if (!chapterImages.empty())
        return true;
    }
    searchStart = match.suffix().first;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Download one image URL into RAM, decode it, return SDL_Texture*
// ─────────────────────────────────────────────────────────────────────────────
SDL_Texture *downloadImage(CURL *curl, const std::string &url) {
  MemoryBuffer buf;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackBin);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(
      curl, CURLOPT_USERAGENT,
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK || buf.data.empty())
    return nullptr;

  SDL_RWops *rw = SDL_RWFromMem(buf.data.data(), (int)buf.data.size());
  SDL_Surface *surface = IMG_Load_RW(rw, 1); // 1 = close rw after load
  if (!surface)
    return nullptr;

  SDL_Texture *texture = SDL_CreateTextureFromSurface(gRenderer, surface);
  SDL_FreeSurface(surface);
  return texture;
}

// ─────────────────────────────────────────────────────────────────────────────
// Portrait-mode page renderer: drawn at 90 degrees, fits Switch screen width
// ─────────────────────────────────────────────────────────────────────────────
void renderPage(SDL_Texture *tex, int scrollY) {
  if (!tex)
    return;

  int texW, texH;
  SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);

  // Switch screen is 1280x720. We rotate 90 degrees so the image fills the
  // 720-wide "portrait" display. The rotated image's "width" maps to 720px.
  float scale = 720.0f / (float)texH; // original height becomes screen width
  int dstW = (int)(texW * scale);     // original width becomes screen height

  SDL_Rect dst;
  dst.x = 0;
  dst.y = scrollY;
  dst.w = 1280;
  dst.h = dstW;

  // Rotate 90 degrees clockwise so a landscape image fills a portrait screen
  SDL_Point center = {dst.w / 2, dst.h / 2};
  SDL_RenderCopyEx(gRenderer, tex, nullptr, &dst, 90.0, &center, SDL_FLIP_NONE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {

  // libnx init
  romfsInit();
  socketInitializeDefault();
  curl_global_init(CURL_GLOBAL_DEFAULT);

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);

  // ── SDL2 init ──────────────────────────────────────────────────────────
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
  IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
  TTF_Init();

  gWindow = SDL_CreateWindow("KatanaReaderNX", SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, 1280, 720,
                             SDL_WINDOW_FULLSCREEN);
  gRenderer = SDL_CreateRenderer(
      gWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  // ── Phase 1: Fetch chapter HTML ────────────────────────────────────────
  // Splash screen
  SDL_SetRenderDrawColor(gRenderer, 10, 10, 20, 255);
  SDL_RenderClear(gRenderer);
  SDL_RenderPresent(gRenderer);

  CURL *curl = curl_easy_init();
  std::string html;

  curl_easy_setopt(curl, CURLOPT_URL,
                   "https://mangakatana.com/manga/solo-leveling.16520/c200");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackStr);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(
      curl, CURLOPT_USERAGENT,
      "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
      "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
  curl_easy_perform(curl);

  bool parseOk = extractMangaKatanaImages(html);

  // ── Phase 2: Download & decode images ─────────────────────────────────
  if (parseOk) {
    for (size_t i = 0; i < chapterImages.size(); i++) {
      // Show loading progress
      SDL_SetRenderDrawColor(gRenderer, 10, 10, 20, 255);
      SDL_RenderClear(gRenderer);
      SDL_RenderPresent(gRenderer);

      SDL_Texture *tex = downloadImage(curl, chapterImages[i]);
      chapterTextures.push_back(tex); // nullptr if failed
    }
  }
  curl_easy_cleanup(curl);

  // ── Phase 3: Render loop ───────────────────────────────────────────────
  int currentPage = 0;
  int scrollY = 0;
  int scrollSpeed = 30;
  bool running = true;

  while (running && appletMainLoop()) {
    padUpdate(&pad);
    u64 kDown = padGetButtonsDown(&pad);
    u64 kHeld = padGetButtons(&pad);

    if (kDown & HidNpadButton_Plus)
      running = false;

    // Page navigation
    if (kDown & HidNpadButton_R) {
      currentPage = std::min(currentPage + 1, (int)chapterTextures.size() - 1);
      scrollY = 0;
    }
    if (kDown & HidNpadButton_L) {
      currentPage = std::max(currentPage - 1, 0);
      scrollY = 0;
    }

    // Scrolling (D-Pad Up/Down)
    if (kHeld & HidNpadButton_Down)
      scrollY -= scrollSpeed;
    if (kHeld & HidNpadButton_Up)
      scrollY += scrollSpeed;
    if (scrollY > 0)
      scrollY = 0; // cap at top

    // Clear
    SDL_SetRenderDrawColor(gRenderer, 10, 10, 20, 255);
    SDL_RenderClear(gRenderer);

    if (!parseOk || chapterTextures.empty()) {
      // Error state – nothing to draw
    } else {
      SDL_Texture *tex = chapterTextures[currentPage];
      if (tex) {
        renderPage(tex, scrollY);
      }
    }

    SDL_RenderPresent(gRenderer);
  }

  // ── Cleanup ───────────────────────────────────────────────────────────
  for (auto *t : chapterTextures)
    if (t)
      SDL_DestroyTexture(t);

  SDL_DestroyRenderer(gRenderer);
  SDL_DestroyWindow(gWindow);
  TTF_Quit();
  IMG_Quit();
  SDL_Quit();
  curl_global_cleanup();
  socketExit();
  romfsExit();
  return 0;
}
