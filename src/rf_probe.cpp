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

// Fixed sampling interval, so a "high" count is a comparable occupancy figure
// rather than an artefact of how fast the polling loop happens to spin.
constexpr uint32_t kSampleIntervalUs = 500;

/// One channel's occupancy measurement: how many fixed-rate samples read high.
struct Occupancy
{
    uint16_t high;
    uint16_t samples;
};

/**
 * Measures RPD occupancy on one channel at a fixed sample rate.
 *
 * Counting raw SPI polls is not comparable between commands: the count scales
 * with loop speed, not with airtime. Sampling on a fixed 500us cadence and
 * reporting high-vs-total makes runs comparable to each other, which is the
 * whole point of an A/B against a control.
 *
 * The RPD latches, so CE is toggled after each sample to re-arm the
 * measurement; the settle delay is inside the fixed interval.
 */
Occupancy sampleChannel(uint8_t channel, uint16_t durationMs)
{
    radio.ceLow();
    radio.writeReg(Nrf24Raw::kRfCh, channel);
    radio.ceHigh();
    delayMicroseconds(200); // PLL settle before the first sample counts.

    Occupancy result = {0, 0};
    uint32_t deadline = millis() + durationMs;
    uint32_t nextSample = micros();
    while (static_cast<int32_t>(millis() - deadline) < 0)
    {
        while (static_cast<int32_t>(micros() - nextSample) < 0)
        {
            // Spin to the sample instant: pacing is what makes counts comparable.
        }

        nextSample += kSampleIntervalUs;
        if (radio.readReg(Nrf24Raw::kRpd) & 0x01)
        {
            ++result.high;
        }

        ++result.samples;
        radio.ceLow(); // Re-arm the latch for the next sample.
        radio.ceHigh();
        yield();
    }

    radio.ceLow();
    return result;
}

/**
 * Dwells on each channel in a range instead of sweeping past it.
 *
 * A full sweep listens to any one channel for ~0.3% of the time, so a short
 * burst fired on a trigger pull is almost certain to be missed. Dwelling trades
 * coverage for sensitivity, which is the right trade once a suspect range is
 * known.
 */
void commandWatch(uint8_t from, uint8_t to, uint16_t msPerChannel)
{
    configureForScan();
    Serial.printf("WATCH start from=%u to=%u ms=%u\n", from, to, msPerChannel);
    for (uint8_t ch = from; ch <= to; ++ch)
    {
        Occupancy o = sampleChannel(ch, msPerChannel);
        if (o.high > 0)
        {
            Serial.printf("WATCH ch=%u mhz=%u high=%u samples=%u pct=%u\n",
                          ch, 2400u + ch, o.high, o.samples,
                          o.samples ? (o.high * 100u) / o.samples : 0u);
        }

        if (ch == 125)
        {
            break; // Guard against uint8_t wrap when to == 125.
        }
    }

    Serial.println(F("WATCH done"));
}

/**
 * Camps on one channel and reports activity in 100ms buckets.
 *
 * Bucketed output is what makes trigger-pull correlation possible: a burst that
 * coincides with each shot shows up as isolated non-zero buckets, which ambient
 * WiFi does not produce.
 */
/// Maps a rate token to its RF_SETUP bits. Returns false if unrecognised.
bool rateBits(const String &token, uint8_t &bits)
{
    if (token == "1m")
    {
        bits = 0x00; // RF_DR_LOW=0, RF_DR_HIGH=0.
    }
    else if (token == "2m")
    {
        bits = 0x08; // RF_DR_HIGH.
    }
    else if (token == "250k")
    {
        bits = 0x20; // RF_DR_LOW.
    }
    else
    {
        return false;
    }

    return true;
}

/**
 * Promiscuous capture (Goodspeed): an illegal 2-byte address width with the
 * pseudo-address 0x00AA and CRC disabled makes the radio accept any burst whose
 * bits happen to match, spilling the real 5-byte address into the payload.
 *
 * Roughly one capture in twenty is a genuine packet; the rest is noise that the
 * host-side CRC check filters out. That yield is expected, not a fault. The air
 * data rate must match the target exactly - a 2Mbps transmitter is invisible to
 * a 1Mbps listener, which is the most common reason a session looks dead.
 */
void commandSniff(uint8_t channel, const String &rateToken, uint16_t seconds)
{
    uint8_t bits = 0;
    if (!rateBits(rateToken, bits))
    {
        Serial.println(F("SNIFF FAIL - rate must be 250k, 1m or 2m"));
        return;
    }

    radio.ceLow();
    radio.writeReg(Nrf24Raw::kEnAa, 0x00);      // Auto-ack off: it needs a real address.
    radio.writeReg(Nrf24Raw::kSetupRetr, 0x00); // No retransmit.
    radio.writeReg(Nrf24Raw::kSetupAw, 0x00);   // Illegal 2-byte width: the trick.
    radio.writeReg(Nrf24Raw::kRfSetup, bits);
    radio.writeReg(Nrf24Raw::kRfCh, channel);

    const uint8_t pseudoAddress[2] = {0xAA, 0x00}; // LSB first = 0x00AA.
    radio.writeReg(Nrf24Raw::kRxAddrP0, pseudoAddress, sizeof(pseudoAddress));
    radio.writeReg(Nrf24Raw::kRxPwP0, 32);
    radio.writeReg(Nrf24Raw::kEnRxaddr, 0x01);
    radio.writeReg(Nrf24Raw::kConfig, 0x03); // PWR_UP | PRIM_RX, EN_CRC cleared.
    delay(2);
    radio.cmd(Nrf24Raw::kFlushRx);
    radio.ceHigh();

    Serial.printf("SNIFF start ch=%u mhz=%u rate=%s secs=%u\n", channel, 2400u + channel,
                  rateToken.c_str(), seconds);
    uint32_t captured = 0;
    uint8_t payload[32];
    uint32_t deadline = millis() + (seconds * 1000u);
    while (static_cast<int32_t>(millis() - deadline) < 0 && !Serial.available())
    {
        if ((radio.readReg(Nrf24Raw::kStatus) & Nrf24Raw::kRxDr) == 0)
        {
            yield();
            continue;
        }

        radio.readPayload(payload, sizeof(payload));
        radio.writeReg(Nrf24Raw::kStatus, Nrf24Raw::kRxDr); // Write 1 to clear.

        Serial.printf("RF ch=%u rate=%s ts=%lu n=32 data=", channel, rateToken.c_str(), micros());
        for (uint8_t i = 0; i < sizeof(payload); ++i)
        {
            Serial.printf("%02X", payload[i]);
        }

        Serial.println();
        ++captured;

        // The RX FIFO holds three packets; flushing keeps the stream fresh
        // rather than replaying a backlog after a burst.
        radio.cmd(Nrf24Raw::kFlushRx);
    }

    while (Serial.available())
    {
        Serial.read();
    }

    radio.ceLow();
    Serial.printf("SNIFF done ch=%u captured=%lu\n", channel, captured);
}

void commandDwell(uint8_t channel, uint16_t seconds)
{
    configureForScan();
    Serial.printf("DWELL start ch=%u mhz=%u secs=%u - fire now\n", channel, 2400u + channel, seconds);
    uint16_t buckets = seconds * 10;
    uint32_t high = 0;
    uint32_t samples = 0;
    for (uint16_t i = 0; i < buckets; ++i)
    {
        Occupancy o = sampleChannel(channel, 100);
        high += o.high;
        samples += o.samples;
        if (o.high > 0)
        {
            Serial.printf("DWELL t=%ums high=%u/%u\n", i * 100u, o.high, o.samples);
        }
    }

    Serial.printf("DWELL done ch=%u high=%lu samples=%lu pct=%lu\n",
                  channel, high, samples, samples ? (high * 100u) / samples : 0u);
}
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    radio.begin(kCePin, kCsnPin);
    Serial.println(F("RF probe ready - commands: selftest, scan [sweeps], watch from= to= ms=, "
                     "dwell ch= secs=, sniff ch= rate= secs="));
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
    else if (line.startsWith("watch"))
    {
        int fromAt = line.indexOf("from=");
        int toAt = line.indexOf("to=");
        int msAt = line.indexOf("ms=");
        long from = fromAt >= 0 ? line.substring(fromAt + 5).toInt() : 0;
        long to = toAt >= 0 ? line.substring(toAt + 3).toInt() : 125;
        long ms = msAt >= 0 ? line.substring(msAt + 3).toInt() : 200;
        if (from < 0 || to > 125 || from > to || ms < 1)
        {
            Serial.println(F("usage: watch from=<0-125> to=<0-125> ms=<per channel>"));
        }
        else
        {
            commandWatch(static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint16_t>(ms));
        }
    }
    else if (line.startsWith("dwell"))
    {
        int chAt = line.indexOf("ch=");
        int secsAt = line.indexOf("secs=");
        long ch = chAt >= 0 ? line.substring(chAt + 3).toInt() : -1;
        long secs = secsAt >= 0 ? line.substring(secsAt + 5).toInt() : 10;
        if (ch < 0 || ch > 125 || secs < 1)
        {
            Serial.println(F("usage: dwell ch=<0-125> secs=<n>"));
        }
        else
        {
            commandDwell(static_cast<uint8_t>(ch), static_cast<uint16_t>(secs));
        }
    }
    else if (line.startsWith("sniff"))
    {
        int chAt = line.indexOf("ch=");
        int rateAt = line.indexOf("rate=");
        int secsAt = line.indexOf("secs=");
        long ch = chAt >= 0 ? line.substring(chAt + 3).toInt() : -1;
        long secs = secsAt >= 0 ? line.substring(secsAt + 5).toInt() : 15;
        String rate = rateAt >= 0 ? line.substring(rateAt + 5) : String("1m");
        int space = rate.indexOf(' ');
        if (space >= 0)
        {
            rate = rate.substring(0, space);
        }

        rate.trim();
        if (ch < 0 || ch > 125 || secs < 1)
        {
            Serial.println(F("usage: sniff ch=<0-125> rate=<250k|1m|2m> secs=<n>"));
        }
        else
        {
            commandSniff(static_cast<uint8_t>(ch), rate, static_cast<uint16_t>(secs));
        }
    }
    else if (line.length() > 0)
    {
        Serial.println(F("unknown command - try: selftest, scan [sweeps], watch from= to= ms=, "
                         "dwell ch= secs=, sniff ch= rate= secs="));
    }
}
