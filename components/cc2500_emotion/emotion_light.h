#pragma once

#include <algorithm>
#include <cmath>
#include "cc2500_emotion.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"

namespace esphome {
namespace cc2500_emotion {

// Standard COLOR_TEMPERATURE light for the L&S Emotion Control lamp.
//
// On/off and color temperature are absolute protocol commands. Brightness has a
// fixed number of discrete levels (default 5). We track the current level and
// move to a target by relative up/down steps (no visible jumps); the slider ends
// use the absolute min/max commands (which are also exact resync points).
// Commands received from the physical remote update the tracked level and the
// whole HA state, so the two never drift apart.
class EmotionControlLight : public light::LightOutput {
 public:
  void set_parent(CC2500Emotion *parent) { this->parent_ = parent; }
  void set_cold_white_temperature(float mireds) { this->cold_white_temperature_ = mireds; }
  void set_warm_white_temperature(float mireds) { this->warm_white_temperature_ = mireds; }
  void set_brightness_levels(uint8_t levels) { this->brightness_levels_ = levels; }
  void set_ramp_duration(uint32_t ms) { this->ramp_full_ms_ = ms; }

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
    traits.set_min_mireds(this->cold_white_temperature_);
    traits.set_max_mireds(this->warm_white_temperature_);
    return traits;
  }

  void write_state(light::LightState *state) override {
    this->state_ = state;

    if (this->suppress_tx_) {
      // State was set from a received remote command. write_state is deferred to
      // LightState::loop(), so the flag must persist until now (consumed here);
      // clearing it right after perform() would let this run as a normal command
      // and re-transmit it to the lamp.
      this->suppress_tx_ = false;
      this->adopt_(state);
      return;
    }

    if (!state->current_values.is_on()) {
      if (this->is_on_) {
        this->parent_->send_off();
        this->is_on_ = false;
      }
      return;
    }

    float ct_norm, brightness;
    state->current_values_as_ct(&ct_norm, &brightness);
    // current_values_as_ct: ct_norm 0 = coldest .. 1 = warmest; protocol byte is
    // inverted (0x00 = warmest, 0xFF = coldest).
    const uint8_t ct_byte = static_cast<uint8_t>(lroundf((1.0f - ct_norm) * 255.0f));
    const int target = this->brightness_to_level_(brightness);

    if (!this->is_on_) {
      this->parent_->send_on(ct_byte);
      this->is_on_ = true;
      this->last_ct_byte_ = ct_byte;
    } else if (ct_byte != this->last_ct_byte_) {
      this->parent_->send_color_temp(ct_byte);
      this->last_ct_byte_ = ct_byte;
    }

    if (!this->level_known_) {
      this->current_level_ = target;  // first state seen becomes the baseline
      this->last_target_level_ = target;
      this->level_known_ = true;
    } else if (target != this->last_target_level_) {
      // Brightness changed in HA. set_level_ re-asserts the ends absolutely, so
      // min/max are always reachable even if our step tracking has drifted.
      this->last_target_level_ = target;
      this->set_level_(target);
    }
  }

  // Called by the hub when a valid command from our remote is received. Updates
  // the HA state without re-transmitting (suppress_tx_).
  void on_remote_command(uint8_t cmd, uint8_t val) {
    if (this->state_ == nullptr)
      return;
    // Brightness ramp: 0x10 starts ramping up, 0x20 down (~ramp_full_ms_ end to end),
    // 0x40 stops it. The final level is continuous/unknown, so we estimate how far it
    // moved from the elapsed time.
    if (cmd == 0x10) {
      this->start_ramp_(+1);
      return;
    }
    if (cmd == 0x20) {
      this->start_ramp_(-1);
      return;
    }
    if (cmd == 0x40) {
      this->finish_ramp_();
      return;
    }

    auto call = this->state_->make_call();
    switch (cmd) {
      case 0x01:  // on, carries color temperature
        call.set_state(true);
        call.set_color_temperature(this->byte_to_mireds_(val));
        break;
      case 0x02:  // off
        call.set_state(false);
        break;
      case 0x50:  // color temperature
        call.set_color_temperature(this->byte_to_mireds_(val));
        break;
      case 0x11:  // step up one level
        if (!this->level_known_)
          return;
        call.set_brightness(this->level_to_brightness_(std::min(this->current_level_ + 1, this->brightness_levels_ - 1)));
        break;
      case 0x21:  // step down one level
        if (!this->level_known_)
          return;
        call.set_brightness(this->level_to_brightness_(std::max(this->current_level_ - 1, 0)));
        break;
      default:  // 0x60 pair, ... - no state change
        return;
    }
    this->suppress_tx_ = true;
    call.perform();  // write_state (deferred) consumes suppress_tx_ without transmitting
  }

  // Called from the hub loop: finalize a ramp whose stop (0x40) was never received
  // (single-shot, may be lost) by assuming it reached the end.
  void update_ramp(uint32_t now) {
    if (this->ramp_dir_ != 0 && (now - this->ramp_start_) >= this->ramp_full_ms_) {
      this->apply_ramp_(now);
      this->ramp_dir_ = 0;
    }
  }

  // Called by the hub for the brightness +/- buttons: always sends one step command,
  // even at the ends, so a press re-asserts lamp<->UI sync.
  void step_brightness(int direction) {
    if (!this->is_on_ || this->state_ == nullptr)
      return;
    this->parent_->queue_brightness_steps(direction > 0 ? 1 : -1);
    const int target =
        std::min(std::max(this->current_level_ + direction, 0), this->brightness_levels_ - 1);
    this->current_level_ = target;
    this->last_target_level_ = target;
    this->level_known_ = true;
    // Reflect the new level in HA without re-transmitting (flag consumed in write_state).
    this->suppress_tx_ = true;
    this->state_->make_call().set_brightness(this->level_to_brightness_(target)).perform();
  }

 protected:
  int brightness_to_level_(float b) const {
    int level = static_cast<int>(lroundf(b * (this->brightness_levels_ - 1)));
    return std::min(std::max(level, 0), this->brightness_levels_ - 1);
  }
  float level_to_brightness_(int level) const {
    // Levels span 1%..100% (levels-1 evenly, but the lowest is pinned to HA's 1%
    // floor since 0% would read as "off").
    if (level <= 0)
      return 0.01f;
    return static_cast<float>(level) / (this->brightness_levels_ - 1);
  }
  float byte_to_mireds_(uint8_t val) const {
    const float frac = 1.0f - val / 255.0f;  // 0x00 warm -> 1, 0xFF cold -> 0
    return this->cold_white_temperature_ +
           frac * (this->warm_white_temperature_ - this->cold_white_temperature_);
  }

  // Move the lamp to a level with relative step commands (0x11/0x21), queued and paced
  // by the hub. 0x10/0x20 are NOT used - they are ramps, not instant ends. At the ends
  // we queue one extra step (the lamp clamps) so the floor/ceiling is reached even if
  // our tracking drifted by 1.
  void set_level_(int target) {
    const int span = this->brightness_levels_ - 1;
    int steps;
    if (target <= 0)
      steps = -(this->current_level_ + 1);  // over-step down, clamps at the floor
    else if (target >= span)
      steps = (span - this->current_level_) + 1;  // over-step up, clamps at the ceiling
    else
      steps = target - this->current_level_;
    this->parent_->queue_brightness_steps(steps);
    this->current_level_ = target;
  }

  void start_ramp_(int dir) {
    if (!this->level_known_)
      return;
    this->ramp_dir_ = dir;
    this->ramp_start_ = millis();
  }
  void finish_ramp_() {
    if (this->ramp_dir_ == 0)
      return;
    this->apply_ramp_(millis());
    this->ramp_dir_ = 0;
  }
  // Estimate the level reached by a ramp from its elapsed time and publish it.
  void apply_ramp_(uint32_t now) {
    if (this->state_ == nullptr)
      return;
    uint32_t elapsed = now - this->ramp_start_;
    if (elapsed > this->ramp_full_ms_)
      elapsed = this->ramp_full_ms_;
    const int span = this->brightness_levels_ - 1;
    const int moved =
        static_cast<int>(lroundf(static_cast<float>(elapsed) / this->ramp_full_ms_ * span));
    const int target = std::min(std::max(this->current_level_ + this->ramp_dir_ * moved, 0), span);
    this->last_target_level_ = target;
    this->suppress_tx_ = true;
    this->state_->make_call().set_brightness(this->level_to_brightness_(target)).perform();
  }

  void adopt_(light::LightState *state) {
    this->is_on_ = state->current_values.is_on();
    if (this->is_on_) {
      float ct_norm, brightness;
      state->current_values_as_ct(&ct_norm, &brightness);
      this->last_ct_byte_ = static_cast<uint8_t>(lroundf((1.0f - ct_norm) * 255.0f));
      const int level = this->brightness_to_level_(brightness);
      this->current_level_ = level;
      this->last_target_level_ = level;
      this->level_known_ = true;
    }
  }

  CC2500Emotion *parent_{nullptr};
  light::LightState *state_{nullptr};
  float cold_white_temperature_{153.846f};  // 6500 K
  float warm_white_temperature_{370.370f};  // 2700 K
  uint8_t brightness_levels_{5};
  bool is_on_{false};
  uint8_t last_ct_byte_{0x80};
  int current_level_{0};
  int last_target_level_{-1};
  bool level_known_{false};
  bool suppress_tx_{false};
  uint32_t ramp_start_{0};
  int ramp_dir_{0};  // 0 = idle, +1 ramping up, -1 ramping down
  uint32_t ramp_full_ms_{1000};  // time for a full min<->max ramp
};

}  // namespace cc2500_emotion
}  // namespace esphome
