#pragma once
#include <string>
#include <vector>
namespace rs {
struct UiRow {
  std::string title, subtitle, badge;
};
struct UiFrame {
  std::string section, title, subtitle, description, status, hint,
      badge = "OFFLINE", eyebrow;
  std::vector<UiRow> rows;
  size_t selected = 0;
  bool connected = false, busy = false, warning = false;
  float progress = -1;
  unsigned tick = 0;
};
bool initUi();
void shutdownUi();
void drawUi(const UiFrame &frame);
} // namespace rs
