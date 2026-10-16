#include "status_led.h"

#include <algorithm>
#include <array>
#include <atomic>

#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

namespace app {
namespace {

constexpr char kTag[] = "status_led";
constexpr uint64_t kCompletionSettleMs = 750;
constexpr uint64_t kCompletionHoldMs = 5000;
constexpr uint32_t kUpdatePeriodMs = 50;

board::Hardware *hardware;
DiceModel *model;
std::atomic<StatusLedMode> mode{StatusLedMode::kPulse};
std::atomic<StatusLedState> state{StatusLedState::kIdle};
std::array<uint32_t, kMaxDice> participants{};
std::size_t participant_count;
uint64_t all_rolled_since_ms;
uint64_t completed_at_ms;

bool IsActive(const Die &die, uint64_t now_ms) {
  return die.last_activity_ms != 0 &&
         now_ms - die.last_activity_ms <= kDieActiveMs &&
         die.last_seen_ms != 0 && now_ms - die.last_seen_ms <= kDieOfflineMs;
}

bool HasParticipant(uint32_t pixel_id) {
  return std::find(participants.begin(),
                   participants.begin() + participant_count,
                   pixel_id) != participants.begin() + participant_count;
}

void AddParticipant(uint32_t pixel_id) {
  if (!HasParticipant(pixel_id) && participant_count < participants.size()) {
    participants[participant_count++] = pixel_id;
  }
}

const Die *FindDie(const Snapshot &snapshot, uint32_t pixel_id) {
  for (std::size_t i = 0; i < snapshot.dice_count; ++i) {
    if (snapshot.dice[i].pixel_id == pixel_id) {
      return &snapshot.dice[i];
    }
  }
  return nullptr;
}

void ResetRound() {
  participants.fill(0);
  participant_count = 0;
  all_rolled_since_ms = 0;
  completed_at_ms = 0;
}

StatusLedState DetermineState(const Snapshot &snapshot, uint64_t now_ms) {
  bool joined = false;
  for (std::size_t i = 0; i < snapshot.dice_count; ++i) {
    const Die &die = snapshot.dice[i];
    if (!IsActive(die, now_ms)) {
      continue;
    }
    if (die.roll_state == pixels::RollState::kHandling ||
        die.roll_state == pixels::RollState::kRolling ||
        die.roll_state == pixels::RollState::kCrooked) {
      AddParticipant(die.pixel_id);
      joined = true;
    }
  }

  if (joined && completed_at_ms != 0) {
    ResetRound();
    for (std::size_t i = 0; i < snapshot.dice_count; ++i) {
      const Die &die = snapshot.dice[i];
      if (IsActive(die, now_ms) &&
          (die.roll_state == pixels::RollState::kHandling ||
           die.roll_state == pixels::RollState::kRolling ||
           die.roll_state == pixels::RollState::kCrooked)) {
        AddParticipant(die.pixel_id);
      }
    }
  }

  if (participant_count == 0) {
    return StatusLedState::kIdle;
  }

  bool handling = false;
  bool rolling = false;
  bool crooked = false;
  bool all_rolled = true;
  for (std::size_t i = 0; i < participant_count; ++i) {
    const Die *die = FindDie(snapshot, participants[i]);
    if (die == nullptr || !IsActive(*die, now_ms)) {
      all_rolled = false;
      continue;
    }
    switch (die->roll_state) {
    case pixels::RollState::kCrooked:
      crooked = true;
      all_rolled = false;
      break;
    case pixels::RollState::kRolling:
      rolling = true;
      all_rolled = false;
      break;
    case pixels::RollState::kHandling:
      handling = true;
      all_rolled = false;
      break;
    case pixels::RollState::kRolled:
      break;
    default:
      all_rolled = false;
      break;
    }
  }

  if (crooked) {
    all_rolled_since_ms = 0;
    return StatusLedState::kCrooked;
  }
  if (rolling) {
    all_rolled_since_ms = 0;
    return StatusLedState::kRolling;
  }
  if (handling) {
    all_rolled_since_ms = 0;
    return StatusLedState::kHandling;
  }
  if (!all_rolled) {
    all_rolled_since_ms = 0;
    return StatusLedState::kIdle;
  }

  if (all_rolled_since_ms == 0) {
    all_rolled_since_ms = now_ms;
  }
  if (now_ms - all_rolled_since_ms < kCompletionSettleMs) {
    return StatusLedState::kRolling;
  }
  if (completed_at_ms == 0) {
    completed_at_ms = now_ms;
  }
  if (now_ms - completed_at_ms < kCompletionHoldMs) {
    return StatusLedState::kComplete;
  }
  ResetRound();
  return StatusLedState::kIdle;
}

board::RgbColor StateColor(StatusLedState current) {
  switch (current) {
  case StatusLedState::kHandling:
    return {.red = 48, .green = 20, .blue = 0};
  case StatusLedState::kRolling:
    return {.red = 0, .green = 24, .blue = 64};
  case StatusLedState::kCrooked:
    return {.red = 64, .green = 0, .blue = 0};
  case StatusLedState::kComplete:
    return {.red = 0, .green = 64, .blue = 8};
  case StatusLedState::kIdle:
  default:
    return {};
  }
}

board::RgbColor ApplyPulse(board::RgbColor color, uint64_t now_ms) {
  constexpr uint64_t kPeriodMs = 1200;
  const uint64_t position = now_ms % kPeriodMs;
  const uint32_t triangle =
      position < kPeriodMs / 2 ? position : kPeriodMs - position;
  const uint32_t brightness = 25 + triangle * 150 / kPeriodMs;
  color.red = static_cast<uint8_t>(color.red * brightness / 100);
  color.green = static_cast<uint8_t>(color.green * brightness / 100);
  color.blue = static_cast<uint8_t>(color.blue * brightness / 100);
  return color;
}

void SaveMode(StatusLedMode value) {
  nvs_handle_t handle;
  if (nvs_open("pixels", NVS_READWRITE, &handle) != ESP_OK) {
    return;
  }
  if (nvs_set_u8(handle, "status_led", static_cast<uint8_t>(value)) == ESP_OK) {
    nvs_commit(handle);
  }
  nvs_close(handle);
}

void RestoreMode() {
  nvs_handle_t handle;
  if (nvs_open("pixels", NVS_READONLY, &handle) != ESP_OK) {
    return;
  }
  uint8_t stored = static_cast<uint8_t>(StatusLedMode::kPulse);
  if (nvs_get_u8(handle, "status_led", &stored) == ESP_OK &&
      stored <= static_cast<uint8_t>(StatusLedMode::kPulse)) {
    mode.store(static_cast<StatusLedMode>(stored));
  }
  nvs_close(handle);
}

void StatusLedTask(void *) {
  StatusLedState previous_state = StatusLedState::kIdle;
  board::RgbColor previous_color{255, 255, 255};
  while (true) {
    const uint64_t now_ms = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    const Snapshot snapshot = model->GetSnapshot(now_ms);
    const StatusLedState current = DetermineState(snapshot, now_ms);
    state.store(current);
    const StatusLedMode current_mode = mode.load();
    board::RgbColor color =
        current_mode == StatusLedMode::kOff ? board::RgbColor{}
                                            : StateColor(current);
    if (current_mode == StatusLedMode::kPulse &&
        current != StatusLedState::kIdle) {
      color = ApplyPulse(color, now_ms);
    }
    if (color.red != previous_color.red || color.green != previous_color.green ||
        color.blue != previous_color.blue) {
      const esp_err_t result = board::SetStatusLed(hardware, color);
      if (result != ESP_OK) {
        ESP_LOGW(kTag, "Unable to update status LED: %s",
                 esp_err_to_name(result));
      }
      previous_color = color;
    }
    if (current != previous_state) {
      ESP_LOGI(kTag, "Status LED state: %s", StatusLedStateName(current));
      previous_state = current;
    }
    vTaskDelay(pdMS_TO_TICKS(kUpdatePeriodMs));
  }
}

} // namespace

const char *StatusLedModeName(StatusLedMode value) {
  switch (value) {
  case StatusLedMode::kSolid:
    return "SOLID";
  case StatusLedMode::kPulse:
    return "PULSE";
  case StatusLedMode::kOff:
  default:
    return "OFF";
  }
}

const char *StatusLedStateName(StatusLedState value) {
  switch (value) {
  case StatusLedState::kHandling:
    return "HANDLING";
  case StatusLedState::kRolling:
    return "ROLLING";
  case StatusLedState::kCrooked:
    return "CROOKED";
  case StatusLedState::kComplete:
    return "COMPLETE";
  case StatusLedState::kIdle:
  default:
    return "IDLE";
  }
}

StatusLedMode GetStatusLedMode() { return mode.load(); }

StatusLedState GetStatusLedState() { return state.load(); }

bool StatusLedSupported() { return board::GetCapabilities().has_status_led; }

bool CycleStatusLedMode() {
  if (!StatusLedSupported()) {
    return false;
  }
  const auto next = static_cast<StatusLedMode>(
      (static_cast<uint8_t>(mode.load()) + 1U) % 3U);
  mode.store(next);
  SaveMode(next);
  return true;
}

esp_err_t StartStatusLed(board::Hardware *board_hardware,
                         DiceModel *dice_model) {
  if (board_hardware == nullptr || dice_model == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!StatusLedSupported()) {
    return ESP_OK;
  }
  hardware = board_hardware;
  model = dice_model;
  RestoreMode();
  if (xTaskCreate(StatusLedTask, "status_led", 8192, nullptr, 3, nullptr) !=
      pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

} // namespace app
