#include "Nrf24Raw.h"

#include <SPI.h>

namespace
{
// The nRF24L01+ tolerates 10 MHz; 8 MHz leaves margin for dupont wiring.
constexpr uint32_t kSpiHz = 8000000;
const SPISettings kSpi(kSpiHz, MSBFIRST, SPI_MODE0);
}

void Nrf24Raw::begin(uint8_t cePin, uint8_t csnPin)
{
    cePin_ = cePin;
    csnPin_ = csnPin;
    pinMode(cePin_, OUTPUT);
    pinMode(csnPin_, OUTPUT);
    digitalWrite(cePin_, LOW);
    digitalWrite(csnPin_, HIGH); // CSN idles high.
    SPI.begin();
    delay(5); // Power-on-reset settle.
}

void Nrf24Raw::select()
{
    SPI.beginTransaction(kSpi);
    digitalWrite(csnPin_, LOW);
}

void Nrf24Raw::deselect()
{
    digitalWrite(csnPin_, HIGH);
    SPI.endTransaction();
}

uint8_t Nrf24Raw::readReg(uint8_t reg)
{
    select();
    SPI.transfer(reg & 0x1F); // R_REGISTER is 0x00 | reg.
    uint8_t value = SPI.transfer(kNop);
    deselect();
    return value;
}

void Nrf24Raw::writeReg(uint8_t reg, uint8_t value)
{
    select();
    SPI.transfer(0x20 | (reg & 0x1F)); // W_REGISTER.
    SPI.transfer(value);
    deselect();
}

void Nrf24Raw::writeReg(uint8_t reg, const uint8_t *buf, uint8_t len)
{
    select();
    SPI.transfer(0x20 | (reg & 0x1F));
    for (uint8_t i = 0; i < len; ++i)
    {
        SPI.transfer(buf[i]);
    }

    deselect();
}

void Nrf24Raw::cmd(uint8_t command)
{
    select();
    SPI.transfer(command);
    deselect();
}

void Nrf24Raw::readPayload(uint8_t *buf, uint8_t len)
{
    select();
    SPI.transfer(kRRxPayload);
    for (uint8_t i = 0; i < len; ++i)
    {
        buf[i] = SPI.transfer(kNop);
    }

    deselect();
}

void Nrf24Raw::ceHigh()
{
    digitalWrite(cePin_, HIGH);
}

void Nrf24Raw::ceLow()
{
    digitalWrite(cePin_, LOW);
}
