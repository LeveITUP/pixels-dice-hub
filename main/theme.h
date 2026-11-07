#pragma once

#include <cstddef>
#include <cstdint>

namespace app {

// Named colour roles used by the device UI. All values are 0xRRGGBB.
struct Theme {
  const char *name;
  uint32_t background;
  uint32_t panel;
  uint32_t text;
  uint32_t on_accent;    // label text on coloured buttons
  uint32_t muted;
  uint32_t dimmed;
  uint32_t accent;       // primary buttons, aggregate panel
  uint32_t accent_text;  // caption text drawn on top of accent
  uint32_t accent_alt;   // "enabled" setting buttons
  uint32_t button;       // neutral buttons
  uint32_t danger;       // error / low battery text and borders
  uint32_t danger_bg;    // destructive buttons
  uint32_t success;      // charging / complete text
  uint32_t success_bg;   // positive action buttons
  uint32_t warning;      // handling state / battery warn
  uint32_t info;         // rolling state / highlights
  uint32_t violet;       // on-face state
  uint32_t stale;        // offline dice
  uint32_t tile_accents[8];
};

constexpr std::size_t kThemeCount = 7;

void InitTheme();
const Theme &CurrentTheme();
void SetTheme(std::size_t index);
void CycleTheme();

// Blends `color` toward the current panel colour for subtle card tints.
uint32_t ThemeTint(uint32_t color);

} // namespace app
