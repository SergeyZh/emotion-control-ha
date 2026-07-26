#include "cc2500_sniffer.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace cc2500_emotion_sniffer {

static const char *const TAG = "cc2500_emotion_sniffer";

// Known commands, from the protocol notes. Returns nullptr for anything else -
// that is exactly what a user hunting for new commands wants to see.
static const char *command_name(uint8_t cmd) {
  switch (cmd) {
    case 0x01:
      return "on";
    case 0x02:
      return "off";
    case 0x10:
      return "ramp up (hold)";
    case 0x11:
      return "brightness up";
    case 0x20:
      return "ramp down (hold)";
    case 0x21:
      return "brightness down";
    case 0x40:
      return "ramp stop (release)";
    case 0x50:
      return "color temperature";
    case 0x60:
      return "pair";
    default:
      return nullptr;
  }
}

// CC2500 datasheet section 17.3: the appended RSSI byte is a signed value in
// half-dBm steps, offset by ~72 dB at 250 kBaud.
static int rssi_dbm(uint8_t raw) {
  const int value = raw >= 128 ? static_cast<int>(raw) - 256 : static_cast<int>(raw);
  return value / 2 - 72;
}

void CC2500Sniffer::setup() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(TAG, "No cc2500_emotion hub configured.");
    this->mark_failed();
    return;
  }
  this->parent_->register_frame_listener(this);
}

void CC2500Sniffer::dump_config() {
  ESP_LOGCONFIG(TAG, "CC2500 Emotion sniffer:");
  ESP_LOGCONFIG(TAG, "  Dump raw frames: %s", YESNO(this->dump_raw_));
  ESP_LOGCONFIG(TAG, "  Press a button on a remote - its ID appears below.");
}

void CC2500Sniffer::on_emotion_frame(const uint8_t *id, uint8_t cmd, uint8_t val,
                                     uint8_t rssi_raw, uint8_t lqi, const uint8_t *payload) {
  const char *name = command_name(cmd);
  // LQI is an error count (0 = perfect) and the offset is the carrier mismatch
  // between the sender's crystal and ours - both matter when reception is flaky.
  const int offset_khz = this->parent_->last_freq_offset() * 1587 / 1000;
  ESP_LOGI(TAG,
           "remote %02X%02X%02X%02X  cmd=0x%02X (%s)  val=0x%02X  RSSI=%d dBm  LQI=%u  "
           "offset=%+d kHz",
           id[0], id[1], id[2], id[3], cmd, name != nullptr ? name : "UNKNOWN", val,
           rssi_dbm(rssi_raw), lqi & 0x7F, offset_khz);
  // Unknown commands may carry data in the six bytes we assume are reserved, so
  // always show the full frame for those.
  if (this->dump_raw_ || name == nullptr)
    ESP_LOGI(TAG, "  raw: %s", format_hex_pretty(payload, 14).c_str());

  this->record_remote_(id, rssi_dbm(rssi_raw));
}

void CC2500Sniffer::record_remote_(const uint8_t *id, int rssi_dbm) {
  if (this->remotes_sensor_ == nullptr)
    return;

  SeenRemote *slot = nullptr;
  for (uint8_t i = 0; i < this->remote_count_; i++) {
    const SeenRemote &seen = this->remotes_[i];
    if (seen.id[0] == id[0] && seen.id[1] == id[1] && seen.id[2] == id[2] && seen.id[3] == id[3]) {
      slot = &this->remotes_[i];
      break;
    }
  }

  if (slot == nullptr) {
    // Full: the log still shows every frame, so nothing is actually lost.
    if (this->remote_count_ >= MAX_REMOTES)
      return;
    slot = &this->remotes_[this->remote_count_++];
    for (uint8_t i = 0; i < 4; i++)
      slot->id[i] = id[i];
    slot->best_rssi = static_cast<int8_t>(rssi_dbm);
  } else if (rssi_dbm > slot->best_rssi) {
    slot->best_rssi = static_cast<int8_t>(rssi_dbm);
  } else {
    return;  // nothing new to show
  }

  std::string state;
  for (uint8_t i = 0; i < this->remote_count_; i++) {
    const SeenRemote &seen = this->remotes_[i];
    char entry[24];
    snprintf(entry, sizeof(entry), "%s%02X%02X%02X%02X %ddBm", i == 0 ? "" : ", ", seen.id[0],
             seen.id[1], seen.id[2], seen.id[3], seen.best_rssi);
    state += entry;
  }
  if (state != this->remotes_state_) {
    this->remotes_state_ = state;
    this->remotes_sensor_->publish_state(state);
  }
}

}  // namespace cc2500_emotion_sniffer
}  // namespace esphome
