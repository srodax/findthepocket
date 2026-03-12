class PocketOnsetProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.sampleCursor = 0;
    this.prevSample = 0;
    this.prevEnv = 0;
    this.noiseFloor = 0.01;
    this.lastOnsetSample = -1e12;
    this.cooldownSamples = Math.round(sampleRate * 0.12);
    this.sensitivity = 0.8;
    this.gainMultiplier = 1 + this.sensitivity * 4;
    this.detectEnabled = false;
    this.waveChunkSize = 1024;
    this.waveChunk = new Float32Array(this.waveChunkSize);
    this.waveChunkIndex = 0;
    this.waveChunkStartSample = 0;
    this.levelSampleCount = 0;
    this.levelSumSquare = 0;
    this.levelEmitSamples = Math.max(256, Math.round(sampleRate * 0.03));
    this.port.onmessage = (event) => {
      const data = event.data || {};
      if (data.type !== "config") {
        return;
      }
      if (Number.isFinite(data.sensitivity)) {
        this.sensitivity = Math.max(0, Math.min(1, data.sensitivity));
      }
      if (Number.isFinite(data.gainMultiplier)) {
        this.gainMultiplier = Math.max(0.5, Math.min(8, data.gainMultiplier));
      }
      if (typeof data.detectEnabled === "boolean") {
        this.detectEnabled = data.detectEnabled;
      }
      if (Number.isFinite(data.beatIntervalMs) && data.beatIntervalMs > 0) {
        const cooldownMs = Math.max(85, Math.min(190, data.beatIntervalMs * 0.35));
        this.cooldownSamples = Math.max(8, Math.round((cooldownMs / 1000) * sampleRate));
      }
    };
  }

  emitWaveChunk() {
    if (this.waveChunkIndex <= 0) {
      return;
    }
    const payload = new Float32Array(this.waveChunkIndex);
    payload.set(this.waveChunk.subarray(0, this.waveChunkIndex));
    this.port.postMessage(
      {
        type: "waveChunk",
        startAudioTimeSec: this.waveChunkStartSample / sampleRate,
        sampleRate,
        values: payload
      },
      [payload.buffer]
    );
    this.waveChunkIndex = 0;
  }

  process(inputs, outputs) {
    const input = inputs[0];
    const channel = input && input[0];
    const output = outputs[0];
    if (output) {
      for (let ch = 0; ch < output.length; ch += 1) {
        output[ch].fill(0);
      }
    }
    if (!channel || channel.length === 0) {
      return true;
    }

    if (this.waveChunkIndex === 0) {
      this.waveChunkStartSample = this.sampleCursor;
    }

    for (let i = 0; i < channel.length; i += 1) {
      const sample = channel[i];
      const absSample = Math.abs(sample);
      this.levelSumSquare += sample * sample;
      this.levelSampleCount += 1;

      this.waveChunk[this.waveChunkIndex] = sample;
      this.waveChunkIndex += 1;
      if (this.waveChunkIndex >= this.waveChunkSize) {
        this.emitWaveChunk();
        this.waveChunkStartSample = this.sampleCursor + i + 1;
      }

      if (this.detectEnabled) {
        const preEmphasis = sample - this.prevSample * 0.97;
        const boosted = Math.abs(preEmphasis) * this.gainMultiplier;
        const env = this.prevEnv * 0.65 + boosted * 0.35;
        const novelty = Math.max(0, env - this.prevEnv);
        this.noiseFloor = this.noiseFloor * 0.995 + env * 0.005;
        const noveltyGate = Math.max(
          0.004,
          this.noiseFloor * (1.65 + (1 - this.sensitivity) * 0.55)
        );
        const ampGate = Math.max(0.01, this.noiseFloor * 1.2);
        const absoluteSampleIndex = this.sampleCursor + i;
        if (
          novelty > noveltyGate &&
          env > ampGate &&
          absoluteSampleIndex - this.lastOnsetSample > this.cooldownSamples
        ) {
          const prevNovelty = Math.max(1e-8, this.prevEnv - this.noiseFloor);
          const denom = novelty + prevNovelty;
          const frac = denom > 0 ? Math.max(0, Math.min(1, novelty / denom)) : 0;
          const onsetSample = absoluteSampleIndex - 1 + frac;
          this.lastOnsetSample = absoluteSampleIndex;
          this.port.postMessage({
            type: "onset",
            audioTimeSec: onsetSample / sampleRate,
            peakAbsRaw: absSample
          });
        }
        this.prevEnv = env;
        this.prevSample = sample;
      } else {
        this.prevEnv = this.prevEnv * 0.98 + absSample * 0.02;
        this.prevSample = sample;
      }
    }

    this.sampleCursor += channel.length;

    if (this.levelSampleCount >= this.levelEmitSamples) {
      const rms = Math.sqrt(this.levelSumSquare / this.levelSampleCount);
      this.port.postMessage({ type: "level", rms });
      this.levelSampleCount = 0;
      this.levelSumSquare = 0;
    }

    return true;
  }
}

registerProcessor("pocket-onset-processor", PocketOnsetProcessor);
