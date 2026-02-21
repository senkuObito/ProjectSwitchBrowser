#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
#include <vector>

using json = nlohmann::json;

struct Bookmark {
  std::string name;
  std::string url;
};

std::vector<Bookmark> bookmarks;
char currentUrl[512] = "https://www.google.com";
const char *bookmarksFile = "sdmc:/config/lightbrowser/bookmarks.json";
int selectedIndex = 0;

void loadBookmarks() {
  std::ifstream file(bookmarksFile);
  if (file.is_open()) {
    try {
      json j;
      file >> j;
      bookmarks.clear();
      for (const auto &item : j) {
        bookmarks.push_back({item["name"], item["url"]});
      }
    } catch (...) {
      printf("Error parsing bookmarks.\n");
    }
    file.close();
  } else {
    // Defaults
    bookmarks.push_back({"Google", "https://google.com/"});
    bookmarks.push_back({"GBATemp", "https://gbatemp.net/"});
    bookmarks.push_back({"YouTube", "https://youtube.com/"});
  }
}

void saveBookmarks() {
  json j = json::array();
  for (const auto &b : bookmarks) {
    j.push_back({{"name", b.name}, {"url", b.url}});
  }
  std::ofstream file(bookmarksFile);
  file << j.dump(4);
}

void launchBrowser(const char *url) {
  WebWifiConfig config;
  Uuid uuid = {0};

  // Use Captive Portal Mode (WebWifiConfig) to completely bypass the
  // 2800-1006 DNS error caused by 90DNS or Exosphere blocking Nintendo servers.
  webWifiCreate(&config, url, url, uuid, 0);

  WebWifiReturnValue out;
  webWifiShow(&config, &out);
}

void promptKeyboard(char *outBuffer, size_t outSize, const char *initialText,
                    const char *guideText) {
  SwkbdConfig kbd;
  Result rc = swkbdCreate(&kbd, 0);
  if (R_SUCCEEDED(rc)) {
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetInitialText(&kbd, initialText);
    swkbdConfigSetGuideText(&kbd, guideText);
    swkbdShow(&kbd, outBuffer, outSize);
    swkbdClose(&kbd);
  }
}

void drawMenu() {
  consoleClear();
  printf("\x1b[47;30m====================================================\x1b["
         "0m\n");
  printf("\x1b[47;30m                 LightBrowser NX                    "
         "\x1b[0m\n");
  printf("\x1b[47;30m====================================================\x1b["
         "0m\n\n");

  printf("  Navigate:   [Up/Down]\n");
  printf("  Launch:     [A]\n");
  printf("  Custom URL: [Y]\n");
  printf("  Add Bkmrk:  [X]\n");
  printf("  Exit:       [+]\n\n");

  printf("------------------ Bookmarks -----------------------\n\n");

  for (size_t i = 0; i < bookmarks.size(); i++) {
    if ((int)i == selectedIndex) {
      printf("  \x1b[46;30m> %-20s (%s)\x1b[0m\n", bookmarks[i].name.c_str(),
             bookmarks[i].url.c_str());
    } else {
      printf("    %-20s (%s)\n", bookmarks[i].name.c_str(),
             bookmarks[i].url.c_str());
    }
  }
}

int main(int argc, char *argv[]) {
  // Initialize console and input
  consoleInit(NULL);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);

  // Check Applet Mode
  if (appletGetAppletType() != AppletType_Application &&
      appletGetAppletType() != AppletType_SystemApplication) {
    consoleClear();
    printf("\n\n\n  \x1b[31mError: Applet Mode Detected!\x1b[0m\n\n");
    printf("  LightBrowser requires Full RAM Mode to run the web applet.\n");
    printf("  Please hold [R] while launching an installed game.\n\n");
    printf("  Press [+] to exit.");
    while (appletMainLoop()) {
      padUpdate(&pad);
      if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
        break;
      consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return 0;
  }

  romfsInit();
  socketInitializeDefault();

  mkdir("sdmc:/config", 0777);
  mkdir("sdmc:/config/lightbrowser", 0777);
  loadBookmarks();

  drawMenu();

  while (appletMainLoop()) {
    padUpdate(&pad);
    u64 kDown = padGetButtonsDown(&pad);

    if (kDown & HidNpadButton_Plus) {
      break;
    }

    if (kDown & HidNpadButton_Down) {
      selectedIndex++;
      if (selectedIndex >= (int)bookmarks.size())
        selectedIndex = 0;
      drawMenu();
    }

    if (kDown & HidNpadButton_Up) {
      selectedIndex--;
      if (selectedIndex < 0)
        selectedIndex = (int)bookmarks.size() - 1;
      drawMenu();
    }

    if (kDown & HidNpadButton_A) {
      if (!bookmarks.empty() && selectedIndex >= 0 &&
          selectedIndex < (int)bookmarks.size()) {
        launchBrowser(bookmarks[selectedIndex].url.c_str());
        drawMenu(); // Redraw after returning from browser
      }
    }

    if (kDown & HidNpadButton_Y) {
      char urlBuf[512] = {0};
      strncpy(urlBuf, "https://", sizeof(urlBuf));
      promptKeyboard(urlBuf, sizeof(urlBuf), urlBuf, "Enter Website URL");
      if (strlen(urlBuf) > 0) {
        launchBrowser(urlBuf);
        drawMenu();
      }
    }

    if (kDown & HidNpadButton_X) {
      char nameBuf[64] = {0};
      char urlBuf[512] = {0};
      strncpy(urlBuf, "https://", sizeof(urlBuf));

      promptKeyboard(nameBuf, sizeof(nameBuf), "", "Enter Bookmark Name");
      if (strlen(nameBuf) > 0) {
        promptKeyboard(urlBuf, sizeof(urlBuf), urlBuf, "Enter Bookmark URL");
        if (strlen(urlBuf) > 0) {
          bookmarks.push_back({nameBuf, urlBuf});
          saveBookmarks();
          drawMenu();
        }
      }
    }

    consoleUpdate(
        NULL); // Needs to be called every frame to update terminal graphics!
  }

  socketExit();
  romfsExit();
  consoleExit(NULL);
  return 0;
}
