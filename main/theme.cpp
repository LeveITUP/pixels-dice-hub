#include "theme.h"

#include <atomic>

#include "esp_log.h"
#include "nvs.h"

namespace app {
namespace {

constexpr char kTag[] = "pixels_theme";
constexpr char kNamespace[] = "pixels";
constexpr char kKey[] = "theme";

constexpr Theme kThemes[kThemeCount] = {
    {
        .name = "MIDNIGHT",
        .background = 0x0b1020,
        .panel = 0x172033,
        .text = 0xf8fafc,
        .on_accent = 0xf8fafc,
        .muted = 0x94a3b8,
        .dimmed = 0x334155,
        .accent = 0x2563eb,
        .accent_text = 0xbfdbfe,
        .accent_alt = 0x1d4ed8,
        .button = 0x334155,
        .danger = 0xf87171,
        .danger_bg = 0x991b1b,
        .success = 0x4ade80,
        .success_bg = 0x166534,
        .warning = 0xf59e0b,
        .info = 0x38bdf8,
        .violet = 0xa78bfa,
        .stale = 0x64748b,
        .tile_accents = {0x8b5cf6, 0x38bdf8, 0xf59e0b, 0x22c55e, 0xec4899,
                         0x06b6d4, 0xf97316, 0x84cc16},
    },
    {
        .name = "MOCHA",
        .background = 0x1e1e2e,
        .panel = 0x313244,
        .text = 0xcdd6f4,
        .on_accent = 0xcdd6f4,
        .muted = 0xa6adc8,
        .dimmed = 0x585b70,
        .accent = 0x4f6fbf,
        .accent_text = 0xb4befe,
        .accent_alt = 0x3f5a9c,
        .button = 0x45475a,
        .danger = 0xf38ba8,
        .danger_bg = 0x8a3a52,
        .success = 0xa6e3a1,
        .success_bg = 0x3f6b48,
        .warning = 0xf9e2af,
        .info = 0x89dceb,
        .violet = 0xcba6f7,
        .stale = 0x6c7086,
        .tile_accents = {0xcba6f7, 0x89dceb, 0xfab387, 0xa6e3a1, 0xf5c2e7,
                         0x94e2d5, 0xf38ba8, 0xf9e2af},
    },
    {
        .name = "LATTE",
        .background = 0xeff1f5,
        .panel = 0xdce0e8,
        .text = 0x4c4f69,
        .on_accent = 0xffffff,
        .muted = 0x6c6f85,
        .dimmed = 0xacb0be,
        .accent = 0x1e66f5,
        .accent_text = 0xdbe6ff,
        .accent_alt = 0x7287fd,
        .button = 0x7c7f93,
        .danger = 0xd20f39,
        .danger_bg = 0xd20f39,
        .success = 0x40a02b,
        .success_bg = 0x40a02b,
        .warning = 0xdf8e1d,
        .info = 0x04a5e5,
        .violet = 0x8839ef,
        .stale = 0x9ca0b0,
        .tile_accents = {0x8839ef, 0x04a5e5, 0xfe640b, 0x40a02b, 0xea76cb,
                         0x179299, 0xd20f39, 0xdf8e1d},
    },
    {
        .name = "DRACULA",
        .background = 0x282a36,
        .panel = 0x343746,
        .text = 0xf8f8f2,
        .on_accent = 0xf8f8f2,
        .muted = 0x9da5c9,
        .dimmed = 0x44475a,
        .accent = 0x6b4fbb,
        .accent_text = 0xd6c6ff,
        .accent_alt = 0x5a3fa8,
        .button = 0x44475a,
        .danger = 0xff5555,
        .danger_bg = 0x8f2f2f,
        .success = 0x50fa7b,
        .success_bg = 0x2f6b43,
        .warning = 0xffb86c,
        .info = 0x8be9fd,
        .violet = 0xbd93f9,
        .stale = 0x6272a4,
        .tile_accents = {0xbd93f9, 0x8be9fd, 0xffb86c, 0x50fa7b, 0xff79c6,
                         0xf1fa8c, 0xff5555, 0x6272a4},
    },
    {
        .name = "NORD",
        .background = 0x2e3440,
        .panel = 0x3b4252,
        .text = 0xeceff4,
        .on_accent = 0xeceff4,
        .muted = 0xaab3c2,
        .dimmed = 0x4c566a,
        .accent = 0x5e81ac,
        .accent_text = 0xd8dee9,
        .accent_alt = 0x4c6a8f,
        .button = 0x434c5e,
        .danger = 0xbf616a,
        .danger_bg = 0x7a3a40,
        .success = 0xa3be8c,
        .success_bg = 0x4a6a3f,
        .warning = 0xebcb8b,
        .info = 0x88c0d0,
        .violet = 0xb48ead,
        .stale = 0x616e82,
        .tile_accents = {0xb48ead, 0x88c0d0, 0xd08770, 0xa3be8c, 0xbf616a,
                         0x8fbcbb, 0xebcb8b, 0x81a1c1},
    },
    {
        .name = "GRUVBOX",
        .background = 0x282828,
        .panel = 0x3c3836,
        .text = 0xebdbb2,
        .on_accent = 0xfbf1c7,
        .muted = 0xa89984,
        .dimmed = 0x665c54,
        .accent = 0x458588,
        .accent_text = 0xd4e5e5,
        .accent_alt = 0x3a7073,
        .button = 0x504945,
        .danger = 0xfb4934,
        .danger_bg = 0x9d0006,
        .success = 0xb8bb26,
        .success_bg = 0x79740e,
        .warning = 0xfabd2f,
        .info = 0x83a598,
        .violet = 0xd3869b,
        .stale = 0x928374,
        .tile_accents = {0xd3869b, 0x83a598, 0xfe8019, 0xb8bb26, 0xfb4934,
                         0x8ec07c, 0xfabd2f, 0xa89984},
    },
    {
        .name = "TOKYO",
        .background = 0x1a1b26,
        .panel = 0x24283b,
        .text = 0xc0caf5,
        .on_accent = 0xc0caf5,
        .muted = 0xa9b1d6,
        .dimmed = 0x414868,
        .accent = 0x3d59a1,
        .accent_text = 0xb4c4f8,
        .accent_alt = 0x33467c,
        .button = 0x414868,
        .danger = 0xf7768e,
        .danger_bg = 0x8c3a4e,
        .success = 0x9ece6a,
        .success_bg = 0x4c6b32,
        .warning = 0xe0af68,
        .info = 0x7dcfff,
        .violet = 0xbb9af7,
        .stale = 0x565f89,
        .tile_accents = {0xbb9af7, 0x7dcfff, 0xff9e64, 0x9ece6a, 0xf7768e,
                         0x1abc9c, 0xe0af68, 0x7aa2f7},
    },
};

std::atomic<std::size_t> current_index{0};
std::atomic<uint32_t> revision{0};

void SaveTheme(std::size_t index) {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
    return;
  }
  if (nvs_set_u8(handle, kKey, static_cast<uint8_t>(index)) == ESP_OK) {
    nvs_commit(handle);
  }
  nvs_close(handle);
}

} // namespace

void InitTheme() {
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return;
  }
  uint8_t stored = 0;
  if (nvs_get_u8(handle, kKey, &stored) == ESP_OK && stored < kThemeCount) {
    current_index.store(stored);
  }
  nvs_close(handle);
  ESP_LOGI(kTag, "Theme %s", CurrentTheme().name);
}

const Theme &CurrentTheme() { return kThemes[current_index.load()]; }

std::size_t CurrentThemeIndex() { return current_index.load(); }

const Theme &ThemeAt(std::size_t index) {
  return kThemes[index < kThemeCount ? index : 0];
}

uint32_t ThemeRevision() { return revision.load(); }

void SetTheme(std::size_t index) {
  if (index >= kThemeCount) {
    index = 0;
  }
  current_index.store(index);
  revision.fetch_add(1);
  SaveTheme(index);
  ESP_LOGI(kTag, "Theme %s", CurrentTheme().name);
}

void CycleTheme() { SetTheme((current_index.load() + 1) % kThemeCount); }

uint32_t ThemeTint(uint32_t color) {
  // ~22% of the accent over the panel colour, matching the previous
  // hand-picked card tints.
  const uint32_t panel = CurrentTheme().panel;
  uint32_t result = 0;
  for (int shift = 0; shift <= 16; shift += 8) {
    const uint32_t a = (color >> shift) & 0xff;
    const uint32_t b = (panel >> shift) & 0xff;
    result |= ((a * 56 + b * 200) / 256) << shift;
  }
  return result;
}

} // namespace app
