#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace cc2500_emotion {

// --- SPI header bits (CC2500 datasheet, section 10) ---
static const uint8_t CC2500_READ_SINGLE = 0x80;  // read bit
static const uint8_t CC2500_BURST = 0x40;        // burst bit

// --- Command strobes ---
static const uint8_t CC2500_SRES = 0x30;   // reset chip
static const uint8_t CC2500_SCAL = 0x33;   // calibrate frequency synthesizer
static const uint8_t CC2500_STX = 0x35;    // enable TX
static const uint8_t CC2500_SIDLE = 0x36;  // exit RX/TX, go to IDLE
static const uint8_t CC2500_SFTX = 0x3B;   // flush TX FIFO
static const uint8_t CC2500_SNOP = 0x3D;   // no-op (read status byte)

// --- Status registers (require read + burst bit, 0xC0 | addr) ---
static const uint8_t CC2500_PARTNUM = 0x30;    // expected 0x80 for CC2500
static const uint8_t CC2500_VERSION = 0x31;    // chip revision (nonzero)
static const uint8_t CC2500_MARCSTATE = 0x35;  // main radio control state (0x01 = IDLE)

// --- TX FIFO ---
static const uint8_t CC2500_TXFIFO = 0x3F;

// --- PATABLE (output power table) ---
static const uint8_t CC2500_PATABLE = 0x3E;

class CC2500Emotion : public Component,
                      public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                            spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_1MHZ> {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_gdo0_pin(GPIOPin *pin) { this->gdo0_pin_ = pin; }
  void set_remote_id(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    this->remote_id_[0] = a;
    this->remote_id_[1] = b;
    this->remote_id_[2] = c;
    this->remote_id_[3] = d;
  }

  // Emotion Control commands (each transmitted 5x, like the original remote).
  void send_on(uint8_t color_temp) { this->send_command_(0x01, color_temp); }
  void send_off() { this->send_command_(0x02, 0x00); }
  void send_brightness_up() { this->send_command_(0x11, 0x00); }
  void send_brightness_down() { this->send_command_(0x21, 0x00); }
  void send_color_temp(uint8_t value) { this->send_command_(0x50, value); }
  void send_pair() { this->send_command_(0x60, 0x00); }

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

  // Build the 14-byte on-air packet for one command and transmit it 5 times.
  void send_command_(uint8_t cmd, uint8_t val);
  // Block until the current packet has finished transmitting (chip back in IDLE).
  void wait_tx_done_();

  GPIOPin *gdo0_pin_{nullptr};
  uint8_t remote_id_[4]{0x68, 0x04, 0x9F, 0x25};
};

}  // namespace cc2500_emotion
}  // namespace esphome
