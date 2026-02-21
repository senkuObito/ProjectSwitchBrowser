#include <fstream>
#include <imgui.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <vector>
#include <sys/stat.h>
// Standard ImGui backend for Switch (often provided by imgui-nx or ported
// manually) Assuming user has imgui_impl_nx or similar. For this example, I
// will include a placeholder that suggests where the backend code would go or
// uses a common homebrew structure. In many simple homebrew, we use a simple
// loop with helper functions.

// #include "imgui_impl_nx.h" // Uncomment if you have the backend header

// Update this to match your ImGui backend function names (e.g. imgui_nx_init)
// Common homebrew backend signatures:
// bool imgui_backend_init();
// void imgui_backend_new_frame();
// void imgui_backend_render_draw_data(ImDrawData* draw_data);
// void imgui_backend_exit();

// If you link against a library like switch-imgui, check its documentation.
// For this example, we assume you copied the source files into `source/` and
// implemented these helpers or use `imgui_impl_nx`.
extern "C" {
void imgui_backend_new_frame();
void imgui_backend_render_draw_data(ImDrawData *draw_data);
bool imgui_backend_init();
void imgui_backend_exit();
}

using json = nlohmann::json;

struct Bookmark {
  std::string name;
  std::string url;
};

std::vector<Bookmark> bookmarks;
char currentUrl[512] = "https://www.google.com";
const char *bookmarksFile = "sdmc:/config/lightbrowser/bookmarks.json";

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
      std::cout << "Error parsing bookmarks." << std::endl;
    }
    file.close();
  } else {
    // Defaults
    bookmarks.push_back({"Google", "https://google.com"});
    bookmarks.push_back({"GBATemp", "https://gbatemp.net"});
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
  WebCommonConfig config;
  Result rc = webPageCreate(&config, url);
  if (R_SUCCEEDED(rc)) {
    // Modern browsing settings
    webConfigSetScreenShot(&config, true); // Allow screenshots if desired, or false for perf
    webConfigSetBootDisplayKind(&config, WebBootDisplayKind_White); // Clean loading
    webConfigSetBackgroundKind(&config, WebBackgroundKind_Default);

    // Hide default footer for more screen space
    webConfigSetFooterFixedKind(&config, WebFooterFixedKind_Hidden); 

    WebCommonReply out;
    webConfigShow(&config, &out); // Launch!
  }
}

int main(int argc, char *argv[]) {
  // Initialize graphics
  // gfxInitDefault(); // Deprecated in modern libnx

  // Initialize Input
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);

  // Initialise console just in case we need debug
  consoleInit(NULL);

  // Initialize ImGui backend
  // Note: You need to link/include imgui and a switch backend (e.g.,
  // imgui_impl_nx)
  if (!imgui_backend_init()) {
    printf("Failed to init ImGui\n");
    // gfxExit(); // Deprecated
    return -1;
  }

  // Initialize file system
  romfsInit();
  socketInitializeDefault(); // For networking if needed

  // Create config dir if not exists
  mkdir("sdmc:/config", 0777);
  mkdir("sdmc:/config/lightbrowser", 0777);

  loadBookmarks();

  while (appletMainLoop()) {
    padUpdate(&pad);
    u64 kDown = padGetButtonsDown(&pad);

    if (kDown & HidNpadButton_Plus)
      break; // Exit

    imgui_backend_new_frame();

    // UI
    {
      ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_Always);
      ImGui::Begin("LightBrowser", NULL,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_NoSavedSettings);

      ImGui::Text("Enter URL:");
      ImGui::InputText("##url", currentUrl, sizeof(currentUrl));
      ImGui::SameLine();
      if (ImGui::Button("Go!")) {
        launchBrowser(currentUrl);
      }

      ImGui::Separator();
      ImGui::Text("Bookmarks:");

      for (const auto &b : bookmarks) {
        if (ImGui::Button(b.name.c_str())) {
          snprintf(currentUrl, sizeof(currentUrl), "%s", b.url.c_str());
          launchBrowser(currentUrl);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", b.url.c_str());
      }

      // Add bookmark section
      ImGui::Separator();
      static char newName[64] = "";
      static char newUrl[512] = "";
      ImGui::InputText("Name", newName, sizeof(newName));
      ImGui::InputText("Address", newUrl, sizeof(newUrl));
      if (ImGui::Button("Add Bookmark")) {
        bookmarks.push_back({newName, newUrl});
        saveBookmarks();
        memset(newName, 0, sizeof(newName));
        memset(newUrl, 0, sizeof(newUrl));
      }

      ImGui::End();
    }

    // Rendering
    ImGui::Render();
    ImDrawData *draw_data = ImGui::GetDrawData();
    imgui_backend_render_draw_data(draw_data); // Your backend render function

    // gfxFlushBuffers(); // Deprecated
    // gfxSwapBuffers(); // Deprecated
    // gfxWaitForVsync(); // Deprecated
    consoleUpdate(NULL); // Update console
  }

  imgui_backend_exit();
  socketExit();
  romfsExit();
  // gfxExit(); // Deprecated
  return 0;
}
