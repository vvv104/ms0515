// audio-worklet.js — the speaker's PCM, frame by frame, on the audio thread.
//
// The page posts each frame's samples (a Float32Array at the context's
// rate); the processor queues them and plays them back to back, silence
// when the queue runs dry (the page fell behind) and dropping the oldest
// when it swells (the page ran ahead) so the lag never grows past ~100 ms.
class Ms0515Speaker extends AudioWorkletProcessor {
  constructor() {
    super();
    this.queue = [];
    this.queued = 0;
    this.offset = 0;
    this.stats = { received: 0, dropped: 0, starved: 0, played: 0 };   // chunks, chunks, quanta, quanta
    this.port.onmessage = (e) => {
      this.queue.push(e.data);
      this.queued += e.data.length;
      ++this.stats.received;
      while (this.queued > sampleRate / 10 && this.queue.length > 1) {
        this.queued -= this.queue[0].length - this.offset;
        this.queue.shift();
        this.offset = 0;
        ++this.stats.dropped;
      }
    };
  }

  process(inputs, outputs) {
    const out = outputs[0][0];
    let i = 0;
    if (this.queue.length) ++this.stats.played; else ++this.stats.starved;
    if ((this.stats.played + this.stats.starved) % 375 === 0) this.port.postMessage(this.stats);   // ~once a second at 48 kHz
    while (i < out.length && this.queue.length) {
      const chunk = this.queue[0];
      const n = Math.min(out.length - i, chunk.length - this.offset);
      out.set(chunk.subarray(this.offset, this.offset + n), i);
      i += n;
      this.offset += n;
      this.queued -= n;
      if (this.offset >= chunk.length) { this.queue.shift(); this.offset = 0; }
    }
    for (; i < out.length; ++i) out[i] = 0;
    return true;
  }
}

registerProcessor("ms0515-speaker", Ms0515Speaker);
