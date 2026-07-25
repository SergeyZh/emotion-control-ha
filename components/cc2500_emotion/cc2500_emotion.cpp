#include "cc2500_emotion.h"
#include "esphome/core/log.h"

namespace esphome {
namespace cc2500_emotion {

static const char *const TAG = "cc2500_emotion";

// Register map captured from the L&S KZQ-8YKC-K remote (CC2500, 26 MHz crystal).
// Source: emotion-control-re/captures/PROTOCOL-FINDINGS.md. Loaded verbatim:
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
    {0x06, 0xFF},  // PKTLEN
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
    ESP_LOGCONFIG(TAG, "Listening for remote commands (RX enabled).");
  }
}

void CC2500Emotion::loop() {
  if (!this->receive_ || this->transmitting_)
    return;
  this->poll_rx_();
}

void CC2500Emotion::dump_config() {
  ESP_LOGCONFIG(TAG, "CC2500 Emotion Control:");
  ESP_LOGCONFIG(TAG, "  Remote ID: %02X %02X %02X %02X", this->remote_id_[0], this->remote_id_[1],
                this->remote_id_[2], this->remote_id_[3]);
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

void CC2500Emotion::send_command_(uint8_t cmd, uint8_t val) {
  // FIFO layout: [len=0x0E][ID:4][cmd][val][00 x6][chk][type=0x16]. The length
  // byte (0x0E = 14) is not transmitted; the other 14 bytes go on-air.
  uint8_t packet[15];
  packet[0] = 0x0E;
  packet[1] = this->remote_id_[0];
  packet[2] = this->remote_id_[1];
  packet[3] = this->remote_id_[2];
  packet[4] = this->remote_id_[3];
  packet[5] = cmd;
  packet[6] = val;
  for (uint8_t i = 7; i <= 12; i++)
    packet[i] = 0x00;
  // chk = 0x7E XOR (all on-air bytes except chk itself). The 6 reserved zeros
  // and the length byte do not contribute.
  packet[13] = 0x7E ^ this->remote_id_[0] ^ this->remote_id_[1] ^ this->remote_id_[2] ^
               this->remote_id_[3] ^ cmd ^ val ^ 0x16;
  packet[14] = 0x16;

  ESP_LOGD(TAG, "TX cmd=0x%02X val=0x%02X chk=0x%02X (x5)", cmd, val, packet[13]);

  this->transmitting_ = true;
  // Transmit 5 times, exactly like the original remote.
  for (uint8_t rep = 0; rep < 5; rep++) {
    this->strobe_(CC2500_SIDLE);
    this->strobe_(CC2500_SFTX);
    this->write_burst_(CC2500_TXFIFO, packet, sizeof(packet));
    this->strobe_(CC2500_STX);
    this->wait_tx_done_();
  }
  // Suppress echo of our own burst, then resume listening (if enabled).
  if (this->receive_) {
    this->rx_last_cmd_ = cmd;
    this->rx_last_val_ = val;
    this->rx_last_time_ = millis();
    this->enter_rx_();
  }
  this->transmitting_ = false;
}

void CC2500Emotion::enter_rx_() {
  this->strobe_(CC2500_SIDLE);
  this->strobe_(CC2500_SFRX);
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
    this->enter_rx_();
    return;
  }
  // A complete frame is 1 (length) + 14 (payload) + 2 (appended RSSI/LQI status).
  if ((b2 & 0x7F) < 17)
    return;

  uint8_t buf[17];
  this->read_fifo_burst_(buf, sizeof(buf));
  this->process_packet_(buf);
  // We only need one packet of the remote's 5x burst. Flush the rest and restart
  // RX so the FIFO can never overflow and the state stays clean.
  this->enter_rx_();
}

void CC2500Emotion::process_packet_(const uint8_t *buf) {
  if (buf[0] != 0x0E)  // length byte must be 14
    return;
  if (!(buf[16] & 0x80))  // CRC_OK is bit 7 of the appended LQI status byte
    return;

  const uint8_t *p = buf + 1;  // 14 on-air bytes: ID:4, cmd, val, 00x6, chk, type
  if (p[0] != this->remote_id_[0] || p[1] != this->remote_id_[1] || p[2] != this->remote_id_[2] ||
      p[3] != this->remote_id_[3])
    return;  // a different remote

  const uint8_t cmd = p[4];
  const uint8_t val = p[5];
  const uint8_t expect = 0x7E ^ this->remote_id_[0] ^ this->remote_id_[1] ^ this->remote_id_[2] ^
                         this->remote_id_[3] ^ cmd ^ val ^ 0x16;
  if (p[13] != 0x16 || p[12] != expect)
    return;  // wrong type byte or bad checksum

  // Collapse the remote's 5x repeat burst into one event.
  const uint32_t now = millis();
  if (cmd == this->rx_last_cmd_ && val == this->rx_last_val_ && (now - this->rx_last_time_) < 400) {
    this->rx_last_time_ = now;
    return;
  }
  this->rx_last_cmd_ = cmd;
  this->rx_last_val_ = val;
  this->rx_last_time_ = now;

  ESP_LOGI(TAG, "RX from remote: cmd=0x%02X val=0x%02X", cmd, val);
  // Phase 6b: reflect this into the light state (publish_state, no re-transmit).
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
