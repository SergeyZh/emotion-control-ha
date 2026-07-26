#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace cc2500_emotion {

class EmotionControlLight;  // defined in emotion_light.h

// Receives every valid Emotion frame heard on air, whatever remote sent it.
// Declared here (rather than including the sniffer's header) because the sniffer
// lives in a separate component that may not be part of the build.
class EmotionFrameListener {
 public:
  virtual ~EmotionFrameListener() = default;
  // `id` points at the frame's 4 ID bytes, `payload` at all 14 on-air bytes.
  // `rssi_raw` and `lqi` are the status bytes the chip appends after the frame.
  virtual void on_emotion_frame(const uint8_t *id, uint8_t cmd, uint8_t val, uint8_t rssi_raw,
                                uint8_t lqi, const uint8_t *payload) = 0;
};

// --- SPI header bits (CC2500 datasheet, section 10) ---
static const uint8_t CC2500_READ_SINGLE = 0x80;  // read bit
static const uint8_t CC2500_BURST = 0x40;        // burst bit

// --- Command strobes ---
static const uint8_t CC2500_SRES = 0x30;   // reset chip
static const uint8_t CC2500_SCAL = 0x33;   // calibrate frequency synthesizer
static const uint8_t CC2500_SRX = 0x34;    // enable RX
static const uint8_t CC2500_STX = 0x35;    // enable TX
static const uint8_t CC2500_SIDLE = 0x36;  // exit RX/TX, go to IDLE
static const uint8_t CC2500_SFRX = 0x3A;   // flush RX FIFO
static const uint8_t CC2500_SFTX = 0x3B;   // flush TX FIFO
static const uint8_t CC2500_SNOP = 0x3D;   // no-op (read status byte)

// --- Status registers (require read + burst bit, 0xC0 | addr) ---
static const uint8_t CC2500_PARTNUM = 0x30;    // expected 0x80 for CC2500
static const uint8_t CC2500_VERSION = 0x31;    // chip revision (nonzero)
static const uint8_t CC2500_FREQEST = 0x32;    // demodulator frequency offset estimate (signed)
static const uint8_t CC2500_MARCSTATE = 0x35;  // main radio control state (0x01 = IDLE)
static const uint8_t CC2500_RXBYTES = 0x3B;    // RX FIFO byte count (bit7 = overflow)

// --- FIFO (TX write / RX read share address 0x3F, direction via R/W bit) ---
static const uint8_t CC2500_TXFIFO = 0x3F;
static const uint8_t CC2500_RXFIFO = 0x3F;

// --- PATABLE (output power table) ---
static const uint8_t CC2500_PATABLE = 0x3E;

class CC2500Emotion : public Component,
                      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                            spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_gdo0_pin(GPIOPin *pin) { this->gdo0_pin_ = pin; }
  void set_receive(bool receive) { this->receive_ = receive; }
  // Optional: without it the hub cannot transmit and only listens (sniffer mode).
  void set_remote_id(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    this->remote_id_[0] = a;
    this->remote_id_[1] = b;
    this->remote_id_[2] = c;
    this->remote_id_[3] = d;
    this->has_remote_id_ = true;
  }

  // Frequency offset of the most recent frame, in FSCTRL0 units (1.587 kHz each).
  int8_t last_freq_offset() const { return static_cast<int8_t>(this->last_freqest_); }

  // A sniffer attaches here to see frames from every remote, not just ours.
  // Listening is pointless without RX, so registering forces it on.
  void register_frame_listener(EmotionFrameListener *listener) {
    this->listener_ = listener;
    this->receive_ = true;
  }

  // Emotion Control commands (each transmitted 5x, like the original remote).
  void send_on(uint8_t color_temp) { this->send_command_(0x01, color_temp); }
  void send_off() { this->send_command_(0x02, 0x00); }
  // Brightness step commands are single-shot on the real remote, so send them once.
  void send_brightness_up() { this->send_command_(0x11, 0x00, 1); }
  void send_brightness_down() { this->send_command_(0x21, 0x00, 1); }
  void send_color_temp(uint8_t value) { this->send_command_(0x50, value); }
  void send_pair() { this->send_command_(0x60, 0x00); }
  // Send an arbitrary command/value pair (for experimenting with the protocol).
  void send_raw(uint8_t cmd, uint8_t val) { this->send_command_(cmd, val); }

  // The light platform registers itself here so received remote commands can
  // update the HA state (Phase 6b).
  void set_light(EmotionControlLight *light) { this->light_ = light; }
  // Brightness +/- buttons: step through the light so its tracked level stays in sync.
  void brightness_step(int direction);
  // Queue relative brightness steps (signed: + up, - down). They are transmitted one
  // at a time, `step_interval` apart, so the lamp registers each as a distinct step
  // instead of collapsing a back-to-back burst into one.
  void queue_brightness_steps(int steps) { this->pending_brightness_steps_ += steps; }
  void set_step_interval(uint32_t ms) { this->step_interval_ms_ = ms; }

 protected:
  // Low-level SPI helpers.
  uint8_t strobe_(uint8_t cmd);
  uint8_t read_status_(uint8_t addr);
  uint8_t read_register_(uint8_t addr);
  void write_register_(uint8_t addr, uint8_t value);
  void write_burst_(uint8_t addr, const uint8_t *data, size_t len);
  void reset_chip_();

  // Load the captured L&S register map + PATABLE and calibrate. Returns true if
  // every config register reads back the value we wrote.
  bool configure_radio_();

  // Build the 14-byte on-air packet for one command and transmit it `repeats` times
  // (the remote repeats most commands 5x; brightness steps are single-shot).
  void send_command_(uint8_t cmd, uint8_t val, uint8_t repeats = 5);
  // Block until the current packet has finished transmitting (chip back in IDLE).
  void wait_tx_done_();

  // --- Receive (Phase 6): listen for the physical remote's commands ---
  void enter_rx_();                       // flush RX FIFO and start listening
  void poll_rx_();                        // called from loop(); reads a complete frame if present
  void process_packet_(const uint8_t *buf);  // validate a 17-byte frame and act on it
  void read_fifo_burst_(uint8_t *buf, uint8_t len);

  // Transmit one queued brightness step if the pacing interval has elapsed.
  void service_brightness_queue_();

  GPIOPin *gdo0_pin_{nullptr};
  EmotionControlLight *light_{nullptr};
  EmotionFrameListener *listener_{nullptr};
  uint8_t remote_id_[4]{0x00, 0x00, 0x00, 0x00};
  bool has_remote_id_{false};

  bool receive_{true};
  bool transmitting_{false};
  // Demodulator frequency offset estimate for the last frame, logged as a diagnostic.
  // One LSB = XOSC/2^14 = 1.587 kHz at 26 MHz. Measured values turned out to be noise
  // (random, saturating at the +-BW/8 FOC limit both ways), not a crystal mismatch, so
  // there is deliberately no compensation written back into FSCTRL0.
  uint8_t last_freqest_{0};
  // Deduplicate a remote's repeat burst. Keyed on the sending ID too, so two
  // remotes pressed at once are never collapsed into one event.
  uint8_t rx_last_id_[4]{0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t rx_last_cmd_{0xFF};
  uint8_t rx_last_val_{0xFF};
  uint32_t rx_last_time_{0};

  // Paced brightness step queue (signed: + up, - down).
  int pending_brightness_steps_{0};
  uint32_t last_brightness_step_time_{0};
  uint32_t step_interval_ms_{200};
};

}  // namespace cc2500_emotion
}  // namespace esphome
