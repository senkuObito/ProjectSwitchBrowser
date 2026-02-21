#include <curl/curl.h>
#include <iostream>
#include <regex>
#include <string>
#include <switch.h>
#include <vector>

std::vector<std::string> chapterImages;

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
}

bool extractMangaKatanaImages(const std::string &html) {
  chapterImages.clear();

  // MangaKatana stores images in JS arrays like `var thzq=['url1','url2',];`
  // We use regex to find the array content.
  std::regex arrayRegex(R"(var\s+[a-zA-Z_]\w*\s*=\s*\[(.*?)\];)");
  std::smatch match;

  std::string::const_iterator searchStart(html.cbegin());
  while (std::regex_search(searchStart, html.cend(), match, arrayRegex)) {
    std::string arrayContent = match[1];

    // If it contains "mangakatana.com/imgs" it is highly likely the image array
    if (arrayContent.find("imgs") != std::string::npos ||
        arrayContent.find("http") != std::string::npos) {

      // Extract each URL inside the array
      std::regex urlRegex(R"('(.*?)')");
      std::smatch urlMatch;
      std::string::const_iterator urlSearchStart(arrayContent.cbegin());

      while (std::regex_search(urlSearchStart, arrayContent.cend(), urlMatch,
                               urlRegex)) {
        std::string url = urlMatch[1];
        if (url.find("http") == 0 &&
            url.find("mangakatana.com/imgs") != std::string::npos) {
          chapterImages.push_back(url);
        }
        urlSearchStart = urlMatch.suffix().first;
      }
      return !chapterImages.empty();
    }
    searchStart = match.suffix().first;
  }
  return false;
}

int main(int argc, char *argv[]) {
  // Initialize console
  consoleInit(NULL);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);

  // Initialize Network and Curl
  socketInitializeDefault();
  curl_global_init(CURL_GLOBAL_DEFAULT);

  printf("\x1b[46;30mKatanaReaderNX - Phase 2 (HTML Parsing)\x1b[0m\n\n");
  printf("Target: https://mangakatana.com/manga/solo-leveling.16520/c200\n");
  printf("Status: Fetching raw HTML...\n\n");
  consoleUpdate(NULL);

  CURL *curl = curl_easy_init();
  std::string readBuffer;

  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL,
                     "https://mangakatana.com/manga/solo-leveling.16520/c200");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                     "AppleWebKit/537.36 (KHTML, like Gecko) "
                     "Chrome/120.0.0.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      printf("\x1b[31m[ERROR]\x1b[0m Scraping failed: %s\n",
             curl_easy_strerror(res));
    } else {
      printf("\x1b[32m[SUCCESS]\x1b[0m Download complete! %zu bytes.\n\n",
             readBuffer.length());
      printf("Parsing JS Arrays to find direct Image URLs...\n");
      consoleUpdate(NULL);

      if (extractMangaKatanaImages(readBuffer)) {
        printf("\x1b[32m[SUCCESS]\x1b[0m Found \x1b[33m%zu\x1b[0m images for "
               "this chapter!\n\n",
               chapterImages.size());
        for (size_t i = 0; i < chapterImages.size() && i < 5; i++) {
          printf("  %zu: %s\n", i + 1, chapterImages[i].c_str());
        }
        if (chapterImages.size() > 5) {
          printf("  ...and %zu more.\n", chapterImages.size() - 5);
        }
      } else {
        printf("\x1b[31m[ERROR]\x1b[0m Failed to find image array in HTML.\n");
      }
    }
    curl_easy_cleanup(curl);
  }

  printf("\nPress [+] to exit.\n");

  while (appletMainLoop()) {
    padUpdate(&pad);
    u64 kDown = padGetButtonsDown(&pad);

    if (kDown & HidNpadButton_Plus)
      break;
    consoleUpdate(NULL);
  }

  curl_global_cleanup();
  socketExit();
  consoleExit(NULL);
  return 0;
}
