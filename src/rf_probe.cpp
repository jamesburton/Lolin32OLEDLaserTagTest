/**
 * RF probe firmware (ESP8266 + nRF24L01+).
 *
 * Serial command surface for 2.4 GHz reconnaissance of the Vatos kit's
 * data-sync link. Receive-only. See
 * docs/superpowers/specs/2026-07-28-rf-protocol-analysis-design.md and
 * docs/superpowers/plans/2026-07-28-rf-protocol-analysis.md.
 *
 * Wiring: CE=GPIO4 (D2), CSN=GPIO5 (D1), SCK=GPIO14 (D5), MOSI=GPIO13 (D7),
 * MISO=GPIO12 (D6), IRQ unconnected, VCC=3V3 with a 10uF cap at the module.
 * CE/CSN avoid GPIO15/GPIO2: both are boot-strapping pins and CSN idles high,
 * which can stop the board booting.
 */

#include <Arduino.h>

#include "Nrf24Raw.h"

namespace
{
constexpr uint8_t kCePin = 4;
constexpr uint8_t kCsnPin = 5;
constexpr uint8_t kChannels = 126; // Channels 0..125 = 2400..2525 MHz.

Nrf24Raw radio;

/// Reads back written registers so a wiring fault is visible, not inferred.
void commandSelfTest()
{
    // 0x2A and 0x03 are arbitrary but distinctive: neither is a reset default,
    // so reading them back proves a real write/read round trip rather than a
    // floating bus returning a plausible value.
    radio.writeReg(Nrf24Raw::kRfCh, 0x2A);
    radio.writeReg(Nrf24Raw::kSetupAw, 0x03);
    uint8_t ch = radio.readReg(Nrf24Raw::kRfCh);
    uint8_t aw = radio.readReg(Nrf24Raw::kSetupAw);
    uint8_t config = radio.readReg(Nrf24Raw::kConfig);
    uint8_t status = radio.readReg(Nrf24Raw::kStatus);

    Serial.printf("SELFTEST rfch=0x%02X(exp 0x2A) setupaw=0x%02X(exp 0x03) config=0x%02X status=0x%02X\n",
                  ch, aw, config, status);

    if (ch == 0x2A && aw == 0x03)
    {
        Serial.println(F("SELFTEST ok - radio is responding"));
    }
    else if ((ch == 0x00 && aw == 0x00) || (ch == 0xFF && aw == 0xFF))
    {
        Serial.println(F("SELFTEST FAIL - all 0x00/0xFF: check MISO/MOSI not swapped, CSN wiring, and 3V3 power"));
    }
    else
    {
        Serial.println(F("SELFTEST FAIL - readback mismatch: check SCK/CSN wiring and keep leads under 10cm"));
    }
}

/// Configures the radio as a bare receiver for RPD sampling.
void configureForScan()
{
    radio.ceLow();
    radio.writeReg(Nrf24Raw::kEnAa, 0x00);     // No auto-ack.
    radio.writeReg(Nrf24Raw::kEnRxaddr, 0x00); // No pipes: we only want RPD.
    radio.writeReg(Nrf24Raw::kRfSetup, 0x00);  // 1 Mbps, 0 dBm.
    radio.writeReg(Nrf24Raw::kConfig, 0x03);   // PWR_UP | PRIM_RX, CRC off.
    delay(2);                                  // Power-up settle (1.5ms).
}

/**
 * Sweeps every channel counting RPD trips.
 *
 * The RPD latches when input power exceeds roughly -64 dBm, so this finds
 * occupied channels but only at close range - a few metres at most.
 */
void commandScan(uint16_t sweeps)
{
    uint16_t hits[kChannels] = {0};
    configureForScan();

    for (uint16_t s = 0; s < sweeps; ++s)
    {
        for (uint8_t ch = 0; ch < kChannels; ++ch)
        {
            radio.writeReg(Nrf24Raw::kRfCh, ch);
            radio.ceHigh();
            delayMicroseconds(400); // 130us PLL settle + RPD integration.
            radio.ceLow();
            if (radio.readReg(Nrf24Raw::kRpd) & 0x01)
            {
                ++hits[ch];
            }
        }

        yield(); // Feed the soft WDT during long sweeps.
    }

    uint8_t active = 0;
    for (uint8_t ch = 0; ch < kChannels; ++ch)
    {
        if (hits[ch] > 0)
        {
            ++active;
            Serial.printf("SCAN ch=%u mhz=%u hits=%u\n", ch, 2400u + ch, hits[ch]);
        }
    }

    Serial.printf("SCAN done sweeps=%u active=%u\n", sweeps, active);
    if (active == 0)
    {
        Serial.println(F("SCAN empty - no channel exceeded the RPD threshold (~-64dBm). "
                         "This is NOT proof of silence: move within a couple of metres and retry."));
    }
}
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    radio.begin(kCePin, kCsnPin);
    Serial.println(F("RF probe ready - commands: selftest, scan [sweeps]"));
}

void loop()
{
    if (!Serial.available())
    {
        return;
    }

    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line == "selftest")
    {
        commandSelfTest();
    }
    else if (line == "scan" || line.startsWith("scan "))
    {
        long sweeps = line.length() > 5 ? line.substring(5).toInt() : 0;
        commandScan(sweeps > 0 ? static_cast<uint16_t>(sweeps) : 20);
    }
    else if (line.length() > 0)
    {
        Serial.println(F("unknown command - try: selftest, scan [sweeps]"));
    }
}
