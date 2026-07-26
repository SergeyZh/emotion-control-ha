#pragma once

#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/components/cc2500_emotion/cc2500_emotion.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome {
namespace cc2500_emotion_sniffer {

class CC2500Sniffer;

// on_frame: fires once per press (repeat copies of a burst are not reported), for
// any remote. Arguments: remote id as hex, command byte, decoded command name
// ("UNKNOWN" if we do not know it), value byte, RSSI in dBm, LQI error count.
class SnifferFrameTrigger
    : public Trigger<std::string, uint8_t, std::string, uint8_t, int, uint8_t> {
 public:
  explicit SnifferFrameTrigger(CC2500Sniffer *parent);
};

// Logs every Emotion Control frame heard on air, from any remote. Used to find
// the 4-byte ID of your own remote (press a button, read the log) and to collect
// commands that are not decoded yet.
class CC2500Sniffer : public Component, public cc2500_emotion::EmotionFrameListener {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_parent(cc2500_emotion::CC2500Emotion *parent) { this->parent_ = parent; }
  void set_dump_raw(bool dump_raw) { this->dump_raw_ = dump_raw; }
  void set_remotes_text_sensor(text_sensor::TextSensor *sensor) { this->remotes_sensor_ = sensor; }
  void add_frame_trigger(SnifferFrameTrigger *trigger) { this->frame_triggers_.push_back(trigger); }

  void on_emotion_frame(const uint8_t *id, uint8_t cmd, uint8_t val, uint8_t rssi_raw,
                        uint8_t lqi, const uint8_t *payload, bool repeat) override;

 protected:
  // Remotes heard since boot, for the Home Assistant entity. ESPHome entities are
  // created at compile time, so a remote discovered at runtime cannot get one of its
  // own - they all share a single text sensor listing "ID signal, ID signal, ...".
  static const uint8_t MAX_REMOTES = 8;
  struct SeenRemote {
    uint8_t id[4];
    int8_t best_rssi;
  };
  // Strongest RSSI rather than latest: it converges, so the entity stops republishing
  // instead of churning on every frame of a held button (~85 frames/s).
  void record_remote_(const uint8_t *id, int rssi_dbm);

  cc2500_emotion::CC2500Emotion *parent_{nullptr};
  text_sensor::TextSensor *remotes_sensor_{nullptr};
  std::vector<SnifferFrameTrigger *> frame_triggers_;
  SeenRemote remotes_[MAX_REMOTES];
  uint8_t remote_count_{0};
  std::string remotes_state_;
  bool dump_raw_{false};
};

}  // namespace cc2500_emotion_sniffer
}  // namespace esphome
