#include "IrFramer.h"

#include <Arduino.h>

namespace IrFramer {
namespace {

constexpr size_t RingSize = 512;  // ISR edge ring buffer
constexpr size_t FrameMax = 160;  // max edges in one frame (NEC ~67, gun 41)

uint8_t irPin = 0;
uint32_t gapUs = 50000;

// ISR-written ring buffer of edges, drained by poll()
volatile Edge ring[RingSize];
volatile size_t ringHead = 0; // ISR writes here
size_t ringTail = 0;          // poll() reads from here
volatile uint32_t lastEdgeUs = 0;
volatile uint32_t edgeCount = 0;

// Frame currently being assembled, returned by poll() when complete
Edge frame[FrameMax];
size_t frameLen = 0;

void IRAM_ATTR onEdge() {
  const uint32_t now = micros();
  const size_t h = ringHead;
  ring[h].durationUs = now - lastEdgeUs;
  // The line just changed, so the level that ENDED is the inverse of now
  ring[h].level = digitalRead(irPin) ? 0 : 1; // 1 = HIGH, 0 = LOW
  ringHead = (h + 1) % RingSize;
  lastEdgeUs = now;
  edgeCount++;
}

} // namespace

void begin(uint8_t pin, uint32_t frameGapUs) {
  irPin = pin;
  gapUs = frameGapUs;
  pinMode(pin, INPUT);
  lastEdgeUs = micros();
  attachInterrupt(digitalPinToInterrupt(pin), onEdge, CHANGE);
}

bool poll(const Edge **edges, size_t *count) {
  // Drain new ISR edges into the current frame. An edge that held a level for
  // longer than the gap is idle time between frames, not part of one.
  const size_t head = ringHead;
  while (ringTail != head) {
    const Edge e = {ring[ringTail].durationUs, ring[ringTail].level};
    ringTail = (ringTail + 1) % RingSize;

    if (e.durationUs >= gapUs) {
      if (frameLen >= 2) { // a stray single edge is noise, not a frame
        *edges = frame;
        *count = frameLen;
        frameLen = 0;
        return true;
      }
      frameLen = 0;
    } else if (frameLen < FrameMax) {
      frame[frameLen++] = e;
    }
  }

  // The closing idle edge only arrives when the NEXT frame starts, so also end
  // the frame by timeout once the line has been quiet long enough.
  if (frameLen >= 2 && (micros() - lastEdgeUs) > gapUs) {
    *edges = frame;
    *count = frameLen;
    frameLen = 0;
    return true;
  }

  return false;
}

uint32_t totalEdges() { return edgeCount; }

bool receiving() {
  return frameLen > 0 || (micros() - lastEdgeUs) < gapUs;
}

} // namespace IrFramer
