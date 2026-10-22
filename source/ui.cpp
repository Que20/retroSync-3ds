#include "retrosync/ui.hpp"
#include <3ds.h>
#include <algorithm>
#include <citro2d.h>
#include <cmath>
namespace rs {
namespace {
C3D_RenderTarget *top = nullptr, *bottom = nullptr;
C2D_TextBuf buffer = nullptr;
const u32 bg = C2D_Color32(12, 18, 32, 255),
          card = C2D_Color32(23, 33, 51, 255),
          muted = C2D_Color32(148, 167, 188, 255),
          white = C2D_Color32(235, 243, 250, 255),
          accent = C2D_Color32(69, 224, 181, 255),
          orange = C2D_Color32(255, 184, 97, 255);
std::string safe(const std::string &text) {
  std::string s;
  for (unsigned char c : text)
    if (c >= 32 && c != 127)
      s += static_cast<char>(c);
  return s;
}
void rect(float x, float y, float w, float h, u32 color) {
  C2D_DrawRectSolid(x, y, 0, w, h, color);
}
void text(const std::string &raw, float x, float y, float scale, u32 color,
          float width = 0) {
  C2D_Text t;
  const auto str = safe(raw);
  C2D_TextParse(&t, buffer, str.c_str());
  C2D_TextOptimize(&t);
  if (width > 0)
    C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, x, y, 0, scale, scale, color,
                 width);
  else
    C2D_DrawText(&t, C2D_WithColor, x, y, 0, scale, scale, color);
}
std::string clipped(const std::string &s, size_t n) {
  if (s.size() <= n)
    return s;
  size_t end = n - 3;
  while (end && ((static_cast<unsigned char>(s[end]) & 0xc0) == 0x80))
    --end;
  return s.substr(0, end) + "...";
}
void pill(const std::string &s, float x, float y, float w, u32 color) {
  rect(x, y, w, 18, C2D_Color32(30, 50, 65, 255));
  text(s, x + 7, y + 1, 0.36f, color);
}
} // namespace
bool initUi() {
  gfxInitDefault();
  if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
    gfxExit();
    return false;
  }
  if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
    C3D_Fini();
    gfxExit();
    return false;
  }
  C2D_Prepare();
  top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
  buffer = C2D_TextBufNew(8192);
  return top && bottom && buffer;
}
void shutdownUi() {
  if (buffer)
    C2D_TextBufDelete(buffer);
  C2D_Fini();
  C3D_Fini();
  gfxExit();
}
void drawUi(const UiFrame &f) {
  C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
  C2D_TextBufClear(buffer);
  C2D_TargetClear(top, bg);
  C2D_SceneBegin(top);
  rect(0, 0, 400, 2, accent);
  rect(14, 14, 24, 24, accent);
  text("R", 19, 12, 0.65f, bg);
  text("RetroSync", 47, 12, 0.67f, white);
  text("3DS", 153, 21, 0.34f, muted);
  pill(f.connected ? "CONNECTED" : "SIGN IN", 289, 16, 95,
       f.connected ? accent : orange);
  text(clipped(f.eyebrow.empty() ? "YOUR COLLECTION" : f.eyebrow, 54), 17, 53,
       0.38f, accent);
  rect(16, 75, 368, 112, card);
  rect(16, 75, 3, 112, f.warning ? orange : accent);
  text(clipped(f.title, 52), 29, 84, 0.68f, white, 340);
  text(clipped(f.subtitle, 62), 29, 126, 0.38f, muted, 340);
  text(clipped(f.description, 100), 29, 150, 0.36f, muted, 335);
  if (f.busy) {
    rect(16, 197, 368, 4, card);
    float fraction = f.progress >= 0 ? std::min(1.f, f.progress) : 0.22f;
    float pos =
        f.progress >= 0 ? 16.f : 16.f + ((f.tick % 160) / 160.f) * (368 - 81);
    rect(pos, 197, f.progress >= 0 ? 368 * fraction : 81, 4, accent);
  }
  text(clipped(f.status, 75), 17, 208, 0.35f, f.warning ? orange : muted, 366);
  C2D_TargetClear(bottom, bg);
  C2D_SceneBegin(bottom);
  rect(0, 0, 320, 28, card);
  text(clipped(f.section, 28), 12, 5, 0.49f, white);
  if (!f.rows.empty())
    text(std::to_string(f.selected + 1) + " / " + std::to_string(f.rows.size()),
         259, 8, 0.34f, muted);
  size_t first = (f.selected / 5) * 5;
  for (size_t row = 0; row < 5 && first + row < f.rows.size(); ++row) {
    size_t i = first + row;
    float y = 33 + row * 33;
    const auto &item = f.rows[i];
    bool selected = i == f.selected;
    rect(8, y, 304, 30, selected ? C2D_Color32(33, 69, 75, 255) : card);
    if (selected)
      rect(8, y, 3, 30, accent);
    text(clipped(item.title, item.badge.empty() ? 37 : 29), 17, y + 1, 0.43f,
         selected ? white : muted);
    text(clipped(item.subtitle, 53), 17, y + 17, 0.34f, muted);
    if (!item.badge.empty())
      text(clipped(item.badge, 9), 264, y + 7, 0.30f, accent);
  }
  rect(0, 204, 320, 36, card);
  text(clipped(f.busy ? "B  Cancel transfer" : f.hint, 58), 12, 207, 0.33f,
       f.warning ? orange : accent);
  text("SELECT  Settings                         START  Exit", 12, 224, 0.32f,
       muted);
  C3D_FrameEnd(0);
}
} // namespace rs
