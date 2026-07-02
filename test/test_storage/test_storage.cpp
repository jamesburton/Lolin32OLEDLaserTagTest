/*
 * Native unit tests for the Storage library's WAV parser.
 * Run with: pio test -e native -f test_storage
 */

#include <WavFile.h>
#include <unity.h>
#include <string.h>

using namespace Storage;

void setUp() {}
void tearDown() {}

// Builds a minimal valid 16kHz/16-bit/mono WAV into `buf`, returns its length.
// `sampleCount` int16 samples of value 0 follow the header.
size_t buildValidWav(uint8_t *buf, size_t bufCap, uint32_t rate, uint16_t bits,
                      uint16_t channels, size_t sampleCount) {
  size_t dataBytes = sampleCount * 2;
  size_t total = 44 + dataBytes;
  TEST_ASSERT_TRUE(total <= bufCap);

  memcpy(buf, "RIFF", 4);
  uint32_t riffSize = (uint32_t)(total - 8);
  memcpy(buf + 4, &riffSize, 4);
  memcpy(buf + 8, "WAVE", 4);

  memcpy(buf + 12, "fmt ", 4);
  uint32_t fmtSize = 16;
  memcpy(buf + 16, &fmtSize, 4);
  uint16_t audioFormat = 1;
  memcpy(buf + 20, &audioFormat, 2);
  memcpy(buf + 22, &channels, 2);
  memcpy(buf + 24, &rate, 4);
  uint32_t byteRate = rate * channels * (bits / 8);
  memcpy(buf + 28, &byteRate, 4);
  uint16_t blockAlign = (uint16_t)(channels * (bits / 8));
  memcpy(buf + 32, &blockAlign, 2);
  memcpy(buf + 34, &bits, 2);

  memcpy(buf + 36, "data", 4);
  uint32_t dataSize = (uint32_t)dataBytes;
  memcpy(buf + 40, &dataSize, 4);
  memset(buf + 44, 0, dataBytes);

  return total;
}

void test_parses_valid_16k_16bit_mono() {
  uint8_t buf[128];
  size_t len = buildValidWav(buf, sizeof(buf), 16000, 16, 1, 10);

  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_TRUE(parseWav(buf, len, view, err));
  TEST_ASSERT_EQUAL_UINT32(16000, view.sampleRate);
  TEST_ASSERT_EQUAL_UINT8(16, view.bitsPerSample);
  TEST_ASSERT_EQUAL_UINT8(1, view.channels);
  TEST_ASSERT_EQUAL_UINT32(10, (uint32_t)view.sampleCount);
  TEST_ASSERT_NOT_NULL(view.pcm);
}

void test_rejects_wrong_sample_rate() {
  uint8_t buf[128];
  size_t len = buildValidWav(buf, sizeof(buf), 44100, 16, 1, 10);

  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, len, view, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_rejects_wrong_bit_depth() {
  uint8_t buf[128];
  size_t len = buildValidWav(buf, sizeof(buf), 16000, 8, 1, 10);

  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, len, view, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_rejects_stereo() {
  uint8_t buf[128];
  size_t len = buildValidWav(buf, sizeof(buf), 16000, 16, 2, 10);

  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, len, view, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_rejects_missing_data_chunk() {
  // Valid RIFF/WAVE + fmt chunk only, no data chunk.
  uint8_t buf[64];
  memcpy(buf, "RIFF", 4);
  uint32_t riffSize = 28;
  memcpy(buf + 4, &riffSize, 4);
  memcpy(buf + 8, "WAVE", 4);
  memcpy(buf + 12, "fmt ", 4);
  uint32_t fmtSize = 16;
  memcpy(buf + 16, &fmtSize, 4);
  uint16_t audioFormat = 1, channels = 1, bits = 16;
  uint32_t rate = 16000, byteRate = 32000;
  uint16_t blockAlign = 2;
  memcpy(buf + 20, &audioFormat, 2);
  memcpy(buf + 22, &channels, 2);
  memcpy(buf + 24, &rate, 4);
  memcpy(buf + 28, &byteRate, 4);
  memcpy(buf + 32, &blockAlign, 2);
  memcpy(buf + 34, &bits, 2);
  size_t len = 36;

  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, len, view, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_rejects_truncated_buffer() {
  uint8_t buf[4] = {'R', 'I', 'F', 'F'};
  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, sizeof(buf), view, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_rejects_non_riff_buffer() {
  uint8_t buf[16] = {0};
  memcpy(buf, "JUNK", 4);
  WavView view;
  const char *err = nullptr;
  TEST_ASSERT_FALSE(parseWav(buf, sizeof(buf), view, err));
  TEST_ASSERT_NOT_NULL(err);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_valid_16k_16bit_mono);
  RUN_TEST(test_rejects_wrong_sample_rate);
  RUN_TEST(test_rejects_wrong_bit_depth);
  RUN_TEST(test_rejects_stereo);
  RUN_TEST(test_rejects_missing_data_chunk);
  RUN_TEST(test_rejects_truncated_buffer);
  RUN_TEST(test_rejects_non_riff_buffer);
  return UNITY_END();
}
