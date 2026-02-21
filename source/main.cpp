#include <curl/curl.h>
#include <iostream>
#include <string>
#include <switch.h>

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
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

  printf("\x1b[46;30mKatanaReaderNX - Phase 1 Test\x1b[0m\n\n");
  printf("Target: https://mangakatana.com/\n");
  printf("Status: Fetching raw HTML...\n\n");
  consoleUpdate(NULL);

  CURL *curl = curl_easy_init();
  std::string readBuffer;

  if (curl) {
    // We masquerade as a normal Windows Chrome browser to prevent 403 blocks
    curl_easy_setopt(curl, CURLOPT_URL, "https://mangakatana.com/");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    curl_easy_setopt(
        curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, "
        "like Gecko) Chrome/120.0.0.0 Safari/537.36");

    // Ignore SSL verification for homebrew simplicity
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
      printf("\x1b[31m[ERROR]\x1b[0m Scraping failed: %s\n",
             curl_easy_strerror(res));
    } else {
      printf("\x1b[32m[SUCCESS]\x1b[0m Download complete!\n");
      printf("Total HTML Length: %zu bytes\n", readBuffer.length());

      // Let's print the first 150 characters to prove we got the HTML
      printf("HTML Snippet:\n");
      printf("%.150s...\n", readBuffer.c_str());
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
