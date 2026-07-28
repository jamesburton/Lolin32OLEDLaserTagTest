#pragma once

#include <Arduino.h>

/**
 * Register-level nRF24L01+ access.
 *
 * Deliberately not the RF24 Arduino library: RF24 clamps the address width to
 * 3-5 bytes, so it cannot express the 2-byte promiscuous-mode trick the RF
 * probe depends on. Everything here is raw register I/O, which also keeps the
 * driver portable to the ESP32 boards (only Arduino.h + SPI.h are used).
 *
 * See docs/superpowers/specs/2026-07-28-rf-protocol-analysis-design.md.
 */
class Nrf24Raw
{
public:
    // Registers.
    static constexpr uint8_t kConfig = 0x00;
    static constexpr uint8_t kEnAa = 0x01;
    static constexpr uint8_t kEnRxaddr = 0x02;
    static constexpr uint8_t kSetupAw = 0x03;
    static constexpr uint8_t kSetupRetr = 0x04;
    static constexpr uint8_t kRfCh = 0x05;
    static constexpr uint8_t kRfSetup = 0x06;
    static constexpr uint8_t kStatus = 0x07;
    static constexpr uint8_t kRpd = 0x09;
    static constexpr uint8_t kRxAddrP0 = 0x0A;
    static constexpr uint8_t kRxPwP0 = 0x11;
    static constexpr uint8_t kFifoStatus = 0x17;

    // Commands.
    static constexpr uint8_t kRRxPayload = 0x61;
    static constexpr uint8_t kFlushRx = 0xE2;
    static constexpr uint8_t kFlushTx = 0xE1;
    static constexpr uint8_t kNop = 0xFF;

    // STATUS bits.
    static constexpr uint8_t kRxDr = 0x40;

    /// Starts SPI and configures the CE/CSN pins. CE is left low (standby).
    void begin(uint8_t cePin, uint8_t csnPin);

    /// Reads a single-byte register.
    uint8_t readReg(uint8_t reg);

    /// Writes a single-byte register.
    void writeReg(uint8_t reg, uint8_t value);

    /// Writes a multi-byte register (e.g. an address), LSB first.
    void writeReg(uint8_t reg, const uint8_t *buf, uint8_t len);

    /// Issues a bare command byte (FLUSH_RX, FLUSH_TX, NOP).
    void cmd(uint8_t command);

    /// Reads len bytes of the top RX FIFO payload.
    void readPayload(uint8_t *buf, uint8_t len);

    void ceHigh();
    void ceLow();

private:
    uint8_t cePin_ = 0;
    uint8_t csnPin_ = 0;

    void select();
    void deselect();
};
