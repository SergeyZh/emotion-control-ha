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

static std::string remote_id_hex(const uint8_t *id, uint8_t length) {
  char value[9]{};
  for (uint8_t i = 0; i < length; i++)
    snprintf(value + i * 2, sizeof(value) - i * 2, "%02X", id[i]);
  return std::string(value);
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

SnifferFrameTrigger::SnifferFrameTrigger(CC2500Sniffer *parent) { parent->add_frame_trigger(this); }

void CC2500Sniffer::on_emotion_frame(const uint8_t *id, uint8_t id_length, uint8_t cmd, uint8_t val,
                                     uint8_t rssi_raw, uint8_t lqi, const uint8_t *payload,
                                     uint8_t payload_length, bool repeat) {
  const char *name = command_name(cmd);
  // LQI is an error count (0 = perfect) and the offset is the carrier mismatch
  // between the sender's crystal and ours - both matter when reception is flaky.
  const int offset_khz = this->parent_->last_freq_offset() * 1587 / 1000;
  const std::string id_hex = remote_id_hex(id, id_length);
  ESP_LOGI(TAG,
           "remote %s  cmd=0x%02X (%s)  val=0x%02X  RSSI=%d dBm  LQI=%u  "
           "offset=%+d kHz",
           id_hex.c_str(), cmd, name != nullptr ? name : "UNKNOWN", val, rssi_dbm(rssi_raw),
           lqi & 0x7F, offset_khz);
  // Unknown commands may carry data in the six bytes we assume are reserved, so
  // always show the full frame for those.
  if (this->dump_raw_ || name == nullptr)
    ESP_LOGI(TAG, "  raw: %s", format_hex_pretty(payload, payload_length).c_str());

  this->record_remote_(id, id_length, rssi_dbm(rssi_raw));

  // Automations fire once per press. Every copy of a burst reaches the log above,
  // but a held button sends dozens of frames/s and each on_frame action can end up as a
  // Home Assistant event - that has to be one event per press, not per copy.
  if (repeat || this->frame_triggers_.empty())
    return;
  for (auto *trigger : this->frame_triggers_) {
    trigger->trigger(id_hex, cmd, std::string(name != nullptr ? name : "UNKNOWN"), val,
                     rssi_dbm(rssi_raw), static_cast<uint8_t>(lqi & 0x7F));
  }
}

void CC2500Sniffer::on_unsupported_emotion_frame(const uint8_t *payload, uint8_t length,
                                                 uint8_t rssi_raw, uint8_t lqi) {
  const int offset_khz = this->parent_->last_freq_offset() * 1587 / 1000;
  ESP_LOGI(TAG,
           "CRC-valid unsupported frame  len=%u  RSSI=%d dBm  LQI=%u  offset=%+d kHz  raw: %s",
           length, rssi_dbm(rssi_raw), lqi & 0x7F, offset_khz,
           format_hex_pretty(payload, length).c_str());
}

void CC2500Sniffer::record_remote_(const uint8_t *id, uint8_t id_length, int rssi_dbm) {
  if (this->remotes_sensor_ == nullptr)
    return;

  SeenRemote *slot = nullptr;
  for (uint8_t i = 0; i < this->remote_count_; i++) {
    const SeenRemote &seen = this->remotes_[i];
    bool matches = seen.id_length == id_length;
    for (uint8_t j = 0; matches && j < id_length; j++)
      matches = seen.id[j] == id[j];
    if (matches) {
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
      slot->id[i] = i < id_length ? id[i] : 0x00;
    slot->id_length = id_length;
    slot->best_rssi = static_cast<int8_t>(rssi_dbm);
  } else if (rssi_dbm > slot->best_rssi) {
    slot->best_rssi = static_cast<int8_t>(rssi_dbm);
  } else {
    return;  // nothing new to show
  }

  std::string state;
  for (uint8_t i = 0; i < this->remote_count_; i++) {
    const SeenRemote &seen = this->remotes_[i];
    const std::string seen_id = remote_id_hex(seen.id, seen.id_length);
    char entry[24];
    snprintf(entry, sizeof(entry), "%s%s %ddBm", i == 0 ? "" : ", ", seen_id.c_str(),
             seen.best_rssi);
    state += entry;
  }
  if (state != this->remotes_state_) {
    this->remotes_state_ = state;
    this->remotes_sensor_->publish_state(state);
  }
}

}  // namespace cc2500_emotion_sniffer
}  // namespace esphome
