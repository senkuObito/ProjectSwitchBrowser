// KatanaReaderNX – Native libnx Framebuffer Manga Reader
// Uses stb_image.h for JPEG decoding (zero extra dependencies)
// and libnx framebufferCreate for rendering in portrait mode.

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <algorithm>
#include <curl/curl.h>
#include <regex>
#include <string>
#include <switch.h>
#include <vector>


// ─────────────────────────────────────────────────────────────────────────────
// Display constants
// ─────────────────────────────────────────────────────────────────────────────
static const int SCREEN_W = 1280;
static const int SCREEN_H = 720;

// ─────────────────────────────────────────────────────────────────────────────
// libcurl helpers
// ─────────────────────────────────────────────────────────────────────────────
struct MemoryBuffer {
  std::vector<uint8_t> data;
};

size_t WriteCallbackStr(void *c, size_t s, size_t n, void *u) {
  ((std::string *)u)->append((char *)c, s * n);
  return s * n;
}
size_t WriteCallbackBin(void *c, size_t s, size_t n, void *u) {
  auto *b = (MemoryBuffer *)u;
  uint8_t *p = (uint8_t *)c;
  b->data.insert(b->data.end(), p, p + s * n);
  return s * n;
}

// ─────────────────────────────────────────────────────────────────────────────
// HTML parser – pull image URLs out of MangaKatana JS arrays
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> chapterImages;

bool extractMangaKatanaImages(const std::string &html) {
  chapterImages.clear();
  std::regex arrayRx(R"(var\s+[a-zA-Z_]\w*\s*=\s*\[(.*?)\];)");
  std::smatch m;
  auto it = html.cbegin();
  while (std::regex_search(it, html.cend(), m, arrayRx)) {
    std::string arr = m[1];
    if (arr.find("imgs") != std::string::npos) {
      std::regex urlRx(R"('(https?://[^']+)')");
      std::smatch um;
      auto ui = arr.cbegin();
      while (std::regex_search(ui, arr.cend(), um, urlRx)) {
        if (um[1].str().find("mangakatana.com/imgs") != std::string::npos)
          chapterImages.push_back(um[1]);
        ui = um.suffix().first;
      }
      if (!chapterImages.empty())
        return true;
    }
    it = m.suffix().first;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Download image into RAM
// ─────────────────────────────────────────────────────────────────────────────
static const char *UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
    "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

MemoryBuffer downloadRaw(CURL *curl, const std::string &url) {
  MemoryBuffer buf;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackBin);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, UA);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_perform(curl);
  return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Blit RGBA pixels to the native libnx framebuffer in portrait mode.
// The Switch framebuffer is RGBA8 linear at 1280×720.
// We rotate the image 90° clockwise so a tall manga strip fills the screen
// when the user holds the Switch sideways (Tate/Portrait mode).
// ─────────────────────────────────────────────────────────────────────────────
struct DecodedImage {
  uint8_t *pixels = nullptr;
  int w = 0, h = 0;
  ~DecodedImage() {
    if (pixels)
      stbi_image_free(pixels);
  }
};

void blitPortrait(u32 *fb, const DecodedImage &img, int scrollY) {
  // Scale so the original image height maps to SCREEN_W (720px)
  // (we rotate 90°, so the image height becomes the display width)
  float scaleY = (float)SCREEN_W / img.h; // fits height → screen width
  float scaleX = scaleY;                  // keep aspect ratio
  int dstH = (int)(img.w * scaleX);       // how tall on screen after rotation

  for (int sy = 0; sy < SCREEN_H; sy++) {
    // Map screen column (sy) back through the 90° rotation
    // rotated y on screen ↔ original x axis
    int origX = (int)((sy - scrollY) / scaleX);
    if (origX < 0 || origX >= img.w)
      continue;

    for (int sx = 0; sx < SCREEN_W; sx++) {
      // rotated x on screen ↔ original y axis (reversed)
      int origY = img.h - 1 - (int)(sx / scaleY);
      if (origY < 0 || origY >= img.h)
        continue;

      uint8_t *p = img.pixels + (origY * img.w + origX) * 4;
      u32 colour = RGBA8(p[0], p[1], p[2], 0xFF);
      fb[sy * SCREEN_W + sx] = colour;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal console helper – print without a full console init so we can display
// progress before the framebuffer takes over.
// ─────────────────────────────────────────────────────────────────────────────
PrintConsole statusConsole;

void showStatus(const char *msg) {
  consoleClear();
  printf("\x1b[1;1H\x1b[46;30m KatanaReaderNX \x1b[0m\n\n");
  printf("%s\n", msg);
  consoleUpdate(NULL);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
  // libnx basics
  consoleInit(&statusConsole);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);
  socketInitializeDefault();
  curl_global_init(CURL_GLOBAL_DEFAULT);

  showStatus("Connecting to MangaKatana...");

  // ── Step 1: Fetch chapter HTML ─────────────────────────────────────────
  CURL *curl = curl_easy_init();
  std::string html;
  curl_easy_setopt(curl, CURLOPT_URL,
                   "https://mangakatana.com/manga/solo-leveling.16520/c200");
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallbackStr);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, UA);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_perform(curl);

  showStatus("Parsing image list...");

  if (!extractMangaKatanaImages(html) || chapterImages.empty()) {
    showStatus("[ERROR] Could not parse chapter images. Press [+] to exit.");
    while (appletMainLoop()) {
      padUpdate(&pad);
      if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
        break;
      consoleUpdate(NULL);
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    socketExit();
    consoleExit(NULL);
    return 1;
  }

  printf("Found %zu pages!\n", chapterImages.size());
  consoleUpdate(NULL);

  // ── Step 2: Download & decode one page at a time ───────────────────────
  // We keep at most 3 decoded images in RAM to avoid OOM.
  // For the initial test, load the first page only.
  std::vector<DecodedImage *> pages(chapterImages.size(), nullptr);
  int current = 0;

  auto loadPage = [&](int idx) {
    if (idx < 0 || idx >= (int)pages.size())
      return;
    if (pages[idx])
      return; // already loaded
    char buf[64];
    snprintf(buf, sizeof(buf), "Downloading page %d / %zu...", idx + 1,
             pages.size());
    showStatus(buf);

    MemoryBuffer raw = downloadRaw(curl, chapterImages[idx]);
    if (raw.data.empty())
      return;

    auto *di = new DecodedImage();
    int channels;
    di->pixels = stbi_load_from_memory(raw.data.data(), (int)raw.data.size(),
                                       &di->w, &di->h, &channels, 4);
    if (!di->pixels) {
      delete di;
      return;
    }
    pages[idx] = di;
  };

  // Pre-load first page
  loadPage(0);
  curl_easy_cleanup(curl);

  // ── Step 3: Framebuffer rendering loop ────────────────────────────────
  consoleExit(NULL); // done with text console, switch to raw framebuffer

  Framebuffer fb;
  framebufferCreate(&fb, nwindowGetDefault(), SCREEN_W, SCREEN_H,
                    PIXEL_FORMAT_RGBA_8888, 2);
  framebufferMakeLinear(&fb);

  int scrollY = 0;
  int scrollStep = 20;
  bool running = true;

  while (running && appletMainLoop()) {
    padUpdate(&pad);
    u64 kDown = padGetButtonsDown(&pad);
    u64 kHeld = padGetButtons(&pad);

    if (kDown & HidNpadButton_Plus)
      running = false;

    // Page navigation
    if (kDown & HidNpadButton_R) {
      current = std::min(current + 1, (int)pages.size() - 1);
      scrollY = 0;
      loadPage(current);
    }
    if (kDown & HidNpadButton_L) {
      current = std::max(current - 1, 0);
      scrollY = 0;
    }

    // Scroll
    if (kHeld & HidNpadButton_Down)
      scrollY -= scrollStep;
    if (kHeld & HidNpadButton_Up)
      scrollY += scrollStep;
    if (scrollY > 0)
      scrollY = 0;

    // Draw
    u32 stride;
    u32 *framebuf = (u32 *)framebufferBegin(&fb, &stride);

    // Clear to dark background
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
      framebuf[i] = RGBA8(15, 15, 25, 255);

    if (pages[current]) {
      blitPortrait(framebuf, *pages[current], scrollY);
    }

    framebufferEnd(&fb);
  }

  // Cleanup
  framebufferClose(&fb);
  for (auto *p : pages)
    delete p;
  curl_global_cleanup();
  socketExit();
  return 0;
}
