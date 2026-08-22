#include "cc2500_emotion.h"
#include "emotion_light.h"
#include "esphome/core/log.h"

namespace esphome {
namespace cc2500_emotion {

static const char *const TAG = "cc2500_emotion";

// Register map captured from the L&S KZQ-8YKC-K remote (CC2500, 26 MHz crystal).
// Source: https://github.com/SergeyZh/emotion-control-re/blob/main/captures/PROTOCOL-FINDINGS.md
// Loaded verbatim:
// MSK, 250 kBaud, 2436.2 MHz, sync 0xD391, FEC on, whitening off, hardware CRC.
struct RegSetting {
  uint8_t addr;
  uint8_t value;
};

static const RegSetting CONFIG_REGISTERS[] = {
    {0x00, 0x29},  // IOCFG2
    {0x02, 0x06},  // IOCFG0
    {0x03, 0x07},  // FIFOTHR
    {0x04, 0xD3},  // SYNC1
    {0x05, 0x91},  // SYNC0
    // PKTLEN: maximum accepted length in variable-length mode. The remote's own value
    // is 0xFF, which is harmless for a transmitter but bad for us: on a false sync
    // detection (we sit at 2436.2 MHz, inside the Wi-Fi band) the chip takes the next
    // noise byte as a length and then waits for up to 255 bytes that never arrive,
    // going deaf until the FIFO overflows. Supported Emotion frames are 8 or 14
    // bytes, so this cap still accepts both and discards larger bogus lengths.
    {0x06, 0x0E},  // PKTLEN (deviates from the captured map on purpose - see above)
    {0x07, 0x04},  // PKTCTRL1
    {0x08, 0x05},  // PKTCTRL0  (variable length, hardware CRC, whitening off)
    {0x09, 0x01},  // ADDR
    {0x0A, 0x10},  // CHANNR    (channel 16)
    {0x0B, 0x09},  // FSCTRL1
    {0x0C, 0x00},  // FSCTRL0
    {0x0D, 0x5D},  // FREQ2
    {0x0E, 0x93},  // FREQ1
    {0x0F, 0xB1},  // FREQ0
    {0x10, 0x2D},  // MDMCFG4
    {0x11, 0x3B},  // MDMCFG3
    {0x12, 0x73},  // MDMCFG2   (MSK)
    {0x13, 0xA2},  // MDMCFG1   (FEC on)
    {0x14, 0xF8},  // MDMCFG0
    {0x15, 0x01},  // DEVIATN
    {0x16, 0x07},  // MCSM2
    {0x17, 0x30},  // MCSM1
    {0x18, 0x18},  // MCSM0
    {0x19, 0x1D},  // FOCCFG
    {0x1A, 0x1C},  // BSCFG
    {0x1B, 0xC7},  // AGCCTRL2
    {0x1C, 0x00},  // AGCCTRL1
    {0x1D, 0xB2},  // AGCCTRL0
    {0x1E, 0x87},  // WOREVT1
    {0x1F, 0x6B},  // WOREVT0
    {0x20, 0xF8},  // WORCTRL
    {0x21, 0xB6},  // FREND1
    {0x22, 0x10},  // FREND0
    {0x23, 0xEA},  // FSCAL3
    {0x24, 0x0A},  // FSCAL2
    {0x25, 0x00},  // FSCAL1
    {0x26, 0x11},  // FSCAL0
    {0x29, 0x59},  // FSTEST
    {0x2C, 0x88},  // TEST2
    {0x2D, 0x31},  // TEST1
    {0x2E, 0x0B},  // TEST0
};

static const uint8_t PATABLE_DATA[8] = {0xFF, 0xFE, 0xBB, 0xA9, 0x7F, 0x6E, 0x97, 0xC5};

void CC2500Emotion::setup() {
  ESP_LOGCONFIG(TAG, "Setting up CC2500 Emotion Control...");
  this->spi_setup();

  if (this->gdo0_pin_ != nullptr)
    this->gdo0_pin_->setup();

  this->reset_chip_();

  const uint8_t partnum = this->read_status_(CC2500_PARTNUM);
  const uint8_t version = this->read_status_(CC2500_VERSION);
  ESP_LOGCONFIG(TAG, "CC2500 PARTNUM=0x%02X VERSION=0x%02X", partnum, version);

  // A floating/unconnected bus reads all-zeros or all-ones. A live CC2500
  // returns PARTNUM=0x80 and a nonzero VERSION.
  if (version == 0x00 || version == 0xFF) {
    ESP_LOGE(TAG, "CC2500 not responding (VERSION=0x%02X). Check SPI wiring, CS pin and power.",
             version);
    this->mark_failed();
    return;
  }
  if (partnum != 0x80) {
    ESP_LOGW(TAG, "Unexpected PARTNUM=0x%02X (CC2500 should be 0x80) - continuing anyway.",
             partnum);
  }

  if (this->configure_radio_()) {
    ESP_LOGCONFIG(TAG, "Radio configured and calibrated.");
  } else {
    ESP_LOGE(TAG, "Radio register verification failed - see warnings above.");
    this->mark_failed();
    return;
  }

  if (this->receive_) {
    // The captured config has MCSM1 RXOFF_MODE=IDLE, so the chip stops after one
    // packet. Set RXOFF_MODE=RX (bits[3:2]=11 -> 0x3C) to keep listening.
    this->write_register_(0x17, 0x3C);
    this->enter_rx_();
    // MARCSTATE 0x0D = RX. Anything else means the chip did not actually start
    // listening, which is otherwise invisible until packets fail to arrive.
    const uint8_t marcstate = this->read_status_(CC2500_MARCSTATE) & 0x1F;
    ESP_LOGCONFIG(TAG, "Listening for remote commands (RX enabled, MARCSTATE=0x%02X).", marcstate);
    if (marcstate != 0x0D)
      ESP_LOGW(TAG, "Chip is not in RX state (MARCSTATE=0x%02X, expected 0x0D).", marcstate);
  }
}

void CC2500Emotion::loop() {
  // Paced brightness steps run regardless of receive mode.
  this->service_brightness_queue_();
  if (!this->receive_ || this->transmitting_)
    return;
  this->poll_rx_();
  if (this->light_ != nullptr)
    this->light_->update_ramp(millis());
}

void CC2500Emotion::service_brightness_queue_() {
  const uint32_t now = millis();

  if (this->is_compact_protocol()) {
    // The compact remote has no 0x11/0x21 step commands. Emulate one logical
    // step as a short hold: repeat 0x10/0x20 for step_interval, then send 0x40.
    if (this->compact_ramp_active_) {
      if (now - this->compact_ramp_start_time_ >= this->step_interval_ms_) {
        this->send_command_(0x40, 0x00);
        this->compact_ramp_active_ = false;
        this->compact_ramp_direction_ = 0;
        this->last_brightness_step_time_ = now;
      } else if (now - this->compact_ramp_last_tx_time_ >= 40) {
        this->send_command_(this->compact_ramp_direction_ > 0 ? 0x10 : 0x20, 0x00, 1);
        this->compact_ramp_last_tx_time_ = now;
      }
      return;
    }
    if (this->pending_brightness_steps_ == 0)
      return;
    this->compact_ramp_direction_ = this->pending_brightness_steps_ > 0 ? 1 : -1;
    this->pending_brightness_steps_ += this->compact_ramp_direction_ > 0 ? -1 : 1;
    this->compact_ramp_active_ = true;
    this->compact_ramp_start_time_ = now;
    this->compact_ramp_last_tx_time_ = now;
    this->send_command_(this->compact_ramp_direction_ > 0 ? 0x10 : 0x20, 0x00, 1);
    return;
  }

  if (this->pending_brightness_steps_ == 0)
    return;
  if (now - this->last_brightness_step_time_ < this->step_interval_ms_)
    return;
  if (this->pending_brightness_steps_ > 0) {
    this->send_command_(0x11, 0x00, 1);
    this->pending_brightness_steps_--;
  } else {
    this->send_command_(0x21, 0x00, 1);
    this->pending_brightness_steps_++;
  }
  this->last_brightness_step_time_ = now;
}

void CC2500Emotion::dump_config() {
  ESP_LOGCONFIG(TAG, "CC2500 Emotion Control:");
  if (this->has_remote_id_) {
    if (this->is_compact_protocol()) {
      ESP_LOGCONFIG(TAG, "  Remote route: %02X %02X %02X (compact addressed protocol)",
                    this->remote_id_[0], this->remote_id_[1], this->remote_id_[2]);
    } else {
      ESP_LOGCONFIG(TAG, "  Remote ID: %02X %02X %02X %02X (broadcast protocol)",
                    this->remote_id_[0], this->remote_id_[1], this->remote_id_[2],
                    this->remote_id_[3]);
    }
  } else {
    ESP_LOGCONFIG(TAG, "  Remote ID: not set - transmit disabled, listening only");
  }
  LOG_PIN("  CS Pin: ", this->cs_);
  LOG_PIN("  GDO0 Pin: ", this->gdo0_pin_);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication with CC2500 failed!");
  }
}

uint8_t CC2500Emotion::strobe_(uint8_t cmd) {
  this->enable();
  const uint8_t status = this->transfer_byte(cmd);
  this->disable();
  return status;
}

uint8_t CC2500Emotion::read_status_(uint8_t addr) {
  // Status registers (0x30-0x3D) overlap with command strobes, so they must be
  // accessed with both the read and burst bits set.
  this->enable();
  this->transfer_byte(addr | CC2500_READ_SINGLE | CC2500_BURST);
  const uint8_t value = this->transfer_byte(0x00);
  this->disable();
  return value;
}

uint8_t CC2500Emotion::read_register_(uint8_t addr) {
  // Configuration registers (0x00-0x2E): single read.
  this->enable();
  this->transfer_byte(addr | CC2500_READ_SINGLE);
  const uint8_t value = this->transfer_byte(0x00);
  this->disable();
  return value;
}

void CC2500Emotion::write_register_(uint8_t addr, uint8_t value) {
  this->enable();
  this->transfer_byte(addr);
  this->transfer_byte(value);
  this->disable();
}

void CC2500Emotion::write_burst_(uint8_t addr, const uint8_t *data, size_t len) {
  this->enable();
  this->transfer_byte(addr | CC2500_BURST);
  for (size_t i = 0; i < len; i++)
    this->transfer_byte(data[i]);
  this->disable();
}

bool CC2500Emotion::configure_radio_() {
  for (const auto &reg : CONFIG_REGISTERS)
    this->write_register_(reg.addr, reg.value);

  // Verify before calibration: SCAL overwrites the FSCAL registers with the
  // calibration result, so read-back must happen while they still hold what we wrote.
  bool ok = true;
  for (const auto &reg : CONFIG_REGISTERS) {
    const uint8_t got = this->read_register_(reg.addr);
    if (got != reg.value) {
      ESP_LOGW(TAG, "Register 0x%02X: wrote 0x%02X, read 0x%02X", reg.addr, reg.value, got);
      ok = false;
    }
  }
  ESP_LOGCONFIG(TAG, "Radio check: SYNC1=0x%02X MDMCFG2=0x%02X CHANNR=0x%02X",
                this->read_register_(0x04), this->read_register_(0x12), this->read_register_(0x0A));

  this->write_burst_(CC2500_PATABLE, PATABLE_DATA, sizeof(PATABLE_DATA));
  this->strobe_(CC2500_SCAL);
  delayMicroseconds(800);  // frequency synthesizer calibration takes ~720 us

  return ok;
}

void CC2500Emotion::send_command_(uint8_t cmd, uint8_t val, uint8_t repeats) {
  if (!this->has_remote_id_) {
    ESP_LOGE(TAG, "Cannot transmit: no remote_id configured (sniffer-only mode). "
                  "Set remote_id in the cc2500_emotion block.");
    return;
  }
  uint8_t packet[15]{};
  uint8_t payload_length;
  uint8_t checksum_index;
  if (this->is_compact_protocol()) {
    // [len=8][0x68][remote][driver][cmd][request=0][value][chk][type=0x16].
    // The driver replies with the endpoint bytes swapped and request flag set to 1.
    payload_length = 8;
    packet[0] = payload_length;
    packet[1] = this->remote_id_[0];
    packet[2] = this->remote_id_[1];
    packet[3] = this->remote_id_[2];
    packet[4] = cmd;
    packet[5] = 0x00;
    packet[6] = val;
    checksum_index = 7;
    packet[8] = 0x16;
  } else {
    // [len=14][ID:4][cmd][val][00 x6][chk][type=0x16].
    payload_length = 14;
    packet[0] = payload_length;
    packet[1] = this->remote_id_[0];
    packet[2] = this->remote_id_[1];
    packet[3] = this->remote_id_[2];
    packet[4] = this->remote_id_[3];
    packet[5] = cmd;
    packet[6] = val;
    checksum_index = 13;
    packet[14] = 0x16;
  }
  uint8_t checksum = 0x7E;
  for (uint8_t i = 1; i <= payload_length; i++) {
    if (i != checksum_index)
      checksum ^= packet[i];
  }
  packet[checksum_index] = checksum;

  ESP_LOGD(TAG, "TX %s cmd=0x%02X val=0x%02X chk=0x%02X (x%u)",
           this->is_compact_protocol() ? "compact" : "broadcast", cmd, val, checksum, repeats);

  this->transmitting_ = true;
  for (uint8_t rep = 0; rep < repeats; rep++) {
    this->strobe_(CC2500_SIDLE);
    this->strobe_(CC2500_SFTX);
    this->write_burst_(CC2500_TXFIFO, packet, payload_length + 1);
    this->strobe_(CC2500_STX);
    this->wait_tx_done_();
  }
  // Suppress echo of our own burst, then resume listening (if enabled).
  if (this->receive_) {
    for (uint8_t i = 0; i < 4; i++)
      this->rx_last_id_[i] = this->remote_id_[i];
    this->rx_last_id_length_ = this->remote_id_length_;
    this->rx_last_cmd_ = cmd;
    this->rx_last_val_ = val;
    this->rx_last_time_ = millis();
    this->enter_rx_();
  }
  this->transmitting_ = false;
}

void CC2500Emotion::send_on(uint8_t color_temp) {
  if (this->is_compact_protocol()) {
    // Compact ON carries zero in the request; set the absolute temperature with
    // the separate 0x50 command after the driver has been switched on.
    this->send_command_(0x01, 0x00);
    this->send_command_(0x50, color_temp);
  } else {
    this->send_command_(0x01, color_temp);
  }
}

void CC2500Emotion::send_brightness_up() {
  if (this->is_compact_protocol())
    this->pending_brightness_steps_++;
  else
    this->send_command_(0x11, 0x00, 1);
}

void CC2500Emotion::send_brightness_down() {
  if (this->is_compact_protocol())
    this->pending_brightness_steps_--;
  else
    this->send_command_(0x21, 0x00, 1);
}

void CC2500Emotion::brightness_step(int direction) {
  if (this->light_ != nullptr)
    this->light_->step_brightness(direction);
}

void CC2500Emotion::enter_rx_() {
  this->strobe_(CC2500_SIDLE);
  this->strobe_(CC2500_SFRX);
  this->rx_collected_ = 0;
  this->rx_expected_ = 0;
  this->rx_partial_since_ = 0;
  this->strobe_(CC2500_SRX);
}

void CC2500Emotion::read_fifo_burst_(uint8_t *buf, uint8_t len) {
  this->enable();
  this->transfer_byte(CC2500_RXFIFO | CC2500_READ_SINGLE | CC2500_BURST);
  for (uint8_t i = 0; i < len; i++)
    buf[i] = this->transfer_byte(0x00);
  this->disable();
}

void CC2500Emotion::poll_rx_() {
  // RXBYTES can be unstable during active RX (datasheet erratum); read until two
  // consecutive reads agree.
  const uint8_t b1 = this->read_status_(CC2500_RXBYTES);
  const uint8_t b2 = this->read_status_(CC2500_RXBYTES);
  if (b1 != b2)
    return;

  if (b2 & 0x80) {  // RX FIFO overflow -> flush and restart RX
    ESP_LOGD(TAG, "RX FIFO overflow, restarting RX");
    this->enter_rx_();
    return;
  }
  uint8_t available = b2 & 0x7F;

  // A maximum-length packet takes far less than 10 ms at 250 kBaud, even with
  // FEC. Only time out when the FIFO is still empty: logging a dense button-hold
  // burst can delay loop() for more than 10 ms while the final status byte has
  // already arrived and is waiting to be consumed.
  if (this->rx_collected_ != 0 && available == 0 &&
      (millis() - this->rx_partial_since_) > 10) {
    ESP_LOGD(TAG, "Incomplete RX frame timed out (%u/%u bytes), restarting RX",
             this->rx_collected_, this->rx_expected_);
    this->enter_rx_();
    return;
  }

  // Drain the FIFO as a stream of variable-length packets. A FIFO packet is
  // [length][payload...][RSSI][LQI]. The old fixed 17-byte read concatenated an
  // 11-byte packet (length 8) with the start of the next repeated packet.
  while (available != 0) {
    if (this->rx_collected_ == 0) {
      this->read_fifo_burst_(this->rx_buffer_, 1);
      available--;
      const uint8_t payload_length = this->rx_buffer_[0];
      if (payload_length > 0x0E) {
        ESP_LOGD(TAG, "RX length 0x%02X exceeds PKTLEN, restarting RX", payload_length);
        this->enter_rx_();
        return;
      }
      this->rx_collected_ = 1;
      this->rx_expected_ = payload_length + 3;  // length byte + payload + RSSI/LQI
      this->rx_partial_since_ = millis();
    }

    const uint8_t needed = this->rx_expected_ - this->rx_collected_;
    const uint8_t take = available < needed ? available : needed;
    if (take != 0) {
      this->read_fifo_burst_(this->rx_buffer_ + this->rx_collected_, take);
      this->rx_collected_ += take;
      available -= take;
    }

    if (this->rx_collected_ != this->rx_expected_)
      return;

    // Read while still in RX: this is the offset estimate for the packet we just
    // took out of the FIFO. Re-entering RX would invalidate it.
    this->last_freqest_ = this->read_status_(CC2500_FREQEST);
    this->process_packet_(this->rx_buffer_);
    this->rx_collected_ = 0;
    this->rx_expected_ = 0;
    this->rx_partial_since_ = 0;
  }
}

void CC2500Emotion::process_packet_(const uint8_t *buf) {
  // Every rejection is logged: without this, "the radio hears nothing" and "frames
  // arrive but fail a check" look identical from the log.
  const uint8_t payload_length = buf[0];
  const uint8_t rssi = buf[payload_length + 1];
  const uint8_t lqi = buf[payload_length + 2];
  if (!(lqi & 0x80)) {  // CRC_OK is bit 7 of the appended LQI status byte
    ESP_LOGD(TAG,
             "Frame rejected: length 0x%02X, CRC failed (LQI=%u, freq offset %+d = %+d kHz)",
             payload_length, lqi & 0x7F, static_cast<int8_t>(this->last_freqest_),
             static_cast<int8_t>(this->last_freqest_) * 1587 / 1000);
    return;
  }
  const uint8_t *p = buf + 1;
  uint8_t compact_id[3];
  const uint8_t *id = nullptr;
  uint8_t id_length = 0;
  uint8_t cmd = 0;
  uint8_t val = 0;

  if (payload_length == 0x08) {
    uint8_t expect = 0x7E;
    for (uint8_t i = 0; i < payload_length; i++) {
      if (i != 6)
        expect ^= p[i];
    }
    if (p[0] != 0x68 || p[7] != 0x16 || p[6] != expect) {
      ESP_LOGD(TAG,
               "Compact frame rejected: header=0x%02X type=0x%02X chk=0x%02X (want 0x%02X)",
               p[0], p[7], p[6], expect);
      return;
    }
    // flag=0 is the request from the physical remote; flag=1 is the paired
    // driver's response and carries the authoritative color-temperature value.
    // Process only the response, then normalize its swapped route back to the
    // request order [0x68, remote address, driver address].
    if (p[4] != 0x01)
      return;
    compact_id[0] = p[0];
    compact_id[1] = p[2];
    compact_id[2] = p[1];
    id = compact_id;
    id_length = 3;
    cmd = p[3];
    val = p[5];
  } else if (payload_length == 0x0E) {
    id = p;
    id_length = 4;
    cmd = p[4];
    val = p[5];
    const uint8_t expect = 0x7E ^ p[0] ^ p[1] ^ p[2] ^ p[3] ^ cmd ^ val ^ 0x16;
    if (p[13] != 0x16 || p[12] != expect) {
      ESP_LOGD(TAG, "Frame rejected: type 0x%02X (want 0x16), chk 0x%02X (want 0x%02X)", p[13],
               p[12], expect);
      return;
    }
  } else {
    if (this->listener_ != nullptr) {
      this->listener_->on_unsupported_emotion_frame(p, payload_length, rssi, lqi);
    } else {
      ESP_LOGD(TAG, "CRC-valid frame uses unsupported length 0x%02X (expected 0x08 or 0x0E)",
               payload_length);
    }
    return;
  }

  // Collapse repeats into one event. A remote sends each press as a short burst
  // (~32 ms spacing) and brightness commands sometimes arrive duplicated. A sliding
  // window keyed on ID+cmd+val merges these, while distinct presses (>150 ms apart)
  // stay separate. Held brightness sends 0x10/0x20 (ramp), not a stream of 0x11/0x21,
  // so there are no legitimately fast repeats to lose. Color-temp rotation changes val
  // and is therefore never merged. Keyed on ID so two remotes never merge, and so the
  // echo suppression in send_command_ covers it.
  const uint32_t now = millis();
  bool same_id = id_length == this->rx_last_id_length_;
  for (uint8_t i = 0; same_id && i < id_length; i++)
    same_id = id[i] == this->rx_last_id_[i];
  const bool repeat = same_id && cmd == this->rx_last_cmd_ && val == this->rx_last_val_ &&
                      (now - this->rx_last_time_) < 150;
  if (!repeat) {
    for (uint8_t i = 0; i < 4; i++)
      this->rx_last_id_[i] = i < id_length ? id[i] : 0x00;
    this->rx_last_id_length_ = id_length;
    this->rx_last_cmd_ = cmd;
    this->rx_last_val_ = val;
  }
  this->rx_last_time_ = now;

  // The sniffer is a wire-level tool, so it sees every frame - including all copies
  // of a burst; the number that survive per press is how you tell a healthy link from
  // a marginal one. It gets the repeat flag so it can log everything while firing
  // automations once per press: a held button sends ~85 frames/s, which would flood
  // the Home Assistant event bus.
  if (this->listener_ != nullptr)
    this->listener_->on_emotion_frame(id, id_length, cmd, val, rssi, lqi, p, payload_length,
                                      repeat);

  if (repeat)
    return;  // control path acts once per press

  if (!this->has_remote_id_)
    return;  // sniffer-only: nothing to match against
  if (id_length != this->remote_id_length_)
    return;
  for (uint8_t i = 0; i < id_length; i++) {
    if (id[i] != this->remote_id_[i])
      return;  // a different remote/route
  }

  ESP_LOGI(TAG, "RX from remote: cmd=0x%02X val=0x%02X", cmd, val);
  if (this->light_ != nullptr)
    this->light_->on_remote_command(cmd, val);
}

void CC2500Emotion::wait_tx_done_() {
  const uint32_t start = millis();
  if (this->gdo0_pin_ != nullptr) {
    // IOCFG0=0x06: GDO0 asserts when the sync word is sent, de-asserts at end of packet.
    while (!this->gdo0_pin_->digital_read() && (millis() - start) < 5) {
    }
    while (this->gdo0_pin_->digital_read() && (millis() - start) < 10) {
    }
  } else {
    // Fall back to polling MARCSTATE until the chip returns to IDLE (0x01).
    while ((this->read_status_(CC2500_MARCSTATE) & 0x1F) != 0x01 && (millis() - start) < 10) {
    }
  }
}

void CC2500Emotion::reset_chip_() {
  // Manual reset (CC2500 datasheet, section 19.1.2): pulse CSn to put the chip in
  // a clean SPI state, then issue the SRES strobe. CS is driven by enable()/disable().
  this->enable();  // CSn low
  delayMicroseconds(1);
  this->disable();  // CSn high
  delayMicroseconds(41);  // hold >40 us before reset
  this->strobe_(CC2500_SRES);
  delay(1);  // allow the crystal oscillator and reset to settle
}

}  // namespace cc2500_emotion
}  // namespace esphome
