/**
 * MAX98357A Wokwi custom chip stub.
 *
 * This is a PIN-STUB ONLY. Wokwi's custom-chip C API has no I2S support
 * (open feature request wokwi-features #213), so this chip cannot decode
 * I2S frames or produce audio. It exists purely to:
 *   1. Carry the correct pin names so diagram.json wiring is validated.
 *   2. Optionally log when BCLK is toggling (indicating the firmware is
 *      actively clocking I2S data into the amp).
 *
 * Compile to WASM with wasi-sdk (see README.md):
 *   wasi-sdk/bin/clang max98357a.chip.c \
 *     -o max98357a.chip.wasm \
 *     --target=wasm32-wasip1 \
 *     -nostdlib \
 *     -Wl,--no-entry \
 *     -Wl,--export=chip_init \
 *     -I <wokwi-chip-api-include-path>
 *
 * Wokwi chip API reference: https://docs.wokwi.com/chips-api/getting-started
 */

#include "wokwi-api.h"
#include <stdint.h>
#include <stdbool.h>

/* One instance of chip state per placed part. */
typedef struct {
    pin_t bclk_pin;
    bool  audio_active_logged; /* latch: log "audio active" only once per run */
} chip_state_t;

/* BCLK edge callback. Logs "audio active" the first time BCLK toggles.
 * BCLK toggles continuously during a clip (~16 kHz * 2 channels * 32 bits =
 * ~1 MHz), so we latch after the first edge to avoid flooding the console. */
static void on_bclk_edge(void *user_data) {
    chip_state_t *state = (chip_state_t *)user_data;
    if (!state->audio_active_logged) {
        state->audio_active_logged = true;
        printf("[MAX98357A] BCLK toggling — I2S audio active\n");
    }
}

/* chip_init is called once per chip instance when the simulation starts.
 * Returns a pointer to instance state; Wokwi passes this back to callbacks. */
void *chip_init(void) {
    chip_state_t *state = (chip_state_t *)heap_alloc(sizeof(chip_state_t));
    state->audio_active_logged = false;

    /* Watch BCLK for any edge so we can detect when the firmware is clocking
     * audio data. BCLK is GPIO38 in the S3-Matrix profile. */
    state->bclk_pin = pin_init("BCLK", INPUT);
    pin_watch_config_t cfg = {
        .edge    = BOTH,
        .pin_change = on_bclk_edge,
        .user_data  = state,
    };
    pin_watch(state->bclk_pin, &cfg);

    /* DIN, LRC: inputs only — no action needed for the stub. */
    pin_init("LRC", INPUT);
    pin_init("DIN", INPUT);

    /* SD / GAIN: inputs, pulled up internally in real HW. No action. */
    pin_init("SD",   INPUT);
    pin_init("GAIN", INPUT);

    /* SPK+/SPK-: outputs of the real amp — no drive in the stub. */
    pin_init("SPK+", OUTPUT_HIGH);
    pin_init("SPK-", OUTPUT_LOW);

    /* VIN / GND: power pins — no action in the stub. */
    pin_init("VIN", INPUT);
    pin_init("GND", INPUT);

    return state;
}
