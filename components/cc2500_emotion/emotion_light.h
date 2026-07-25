#pragma once

#include <cmath>
#include "cc2500_emotion.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"

namespace esphome {
namespace cc2500_emotion {

// Standard COLOR_TEMPERATURE light backed by the L&S Emotion Control remote.
// On/off and color temperature are absolute protocol commands. Brightness is
// only relative on this protocol, so the brightness slider is emulated with
// up/down steps (interim, until an absolute-brightness command is found).
class EmotionControlLight : public light::LightOutput {
 public:
  void set_parent(CC2500Emotion *parent) { this->parent_ = parent; }
  void set_cold_white_temperature(float mireds) { this->cold_white_temperature_ = mireds; }
  void set_warm_white_temperature(float mireds) { this->warm_white_temperature_ = mireds; }
  void set_brightness_steps(uint8_t steps) { this->brightness_steps_ = steps; }

  light::LightTraits get_traits() override {
    auto traits = light::LightTraits();
    traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
    traits.set_min_mireds(this->cold_white_temperature_);
    traits.set_max_mireds(this->warm_white_temperature_);
    return traits;
  }

  void write_state(light::LightState *state) override {
    if (!state->current_values.is_on()) {
      if (this->is_on_) {
        this->parent_->send_off();
        this->is_on_ = false;
      }
      return;
    }

    float ct_norm, brightness;
    state->current_values_as_ct(&ct_norm, &brightness);
    // current_values_as_ct: ct_norm 0 = coldest .. 1 = warmest. Protocol byte is
    // inverted: 0x00 = warmest, 0xFF = coldest.
    const uint8_t ct_byte = static_cast<uint8_t>(lroundf((1.0f - ct_norm) * 255.0f));

    if (!this->is_on_) {
      // Turn on carrying the current color temperature; adopt brightness as baseline.
      this->parent_->send_on(ct_byte);
      this->is_on_ = true;
      this->last_ct_byte_ = ct_byte;
      this->last_brightness_ = brightness;
      return;
    }

    if (ct_byte != this->last_ct_byte_) {
      this->parent_->send_color_temp(ct_byte);
      this->last_ct_byte_ = ct_byte;
    }

    // Emulate the absolute brightness slider with relative steps.
    const int steps = lroundf((brightness - this->last_brightness_) * this->brightness_steps_);
    for (int i = 0; i < steps; i++)
      this->parent_->send_brightness_up();
    for (int i = 0; i > steps; i--)
      this->parent_->send_brightness_down();
    if (steps != 0)
      this->last_brightness_ = brightness;
  }

 protected:
  CC2500Emotion *parent_{nullptr};
  float cold_white_temperature_{153.846f};  // 6500 K
  float warm_white_temperature_{370.370f};  // 2700 K
  uint8_t brightness_steps_{10};
  bool is_on_{false};
  uint8_t last_ct_byte_{0x80};
  float last_brightness_{1.0f};
};

}  // namespace cc2500_emotion
}  // namespace esphome
