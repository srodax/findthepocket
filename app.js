(function () {
  "use strict";

  const bpmInput = document.getElementById("bpm");
  const bpmValue = document.getElementById("bpmValue");
  const thresholdInput = document.getElementById("threshold");
  const thresholdValue = document.getElementById("thresholdValue");
  const inputDeviceSelect = document.getElementById("inputDevice");
  const refreshDevicesButton = document.getElementById("refreshDevices");
  const deviceInfo = document.getElementById("deviceInfo");
  const diagnosticsEl = document.getElementById("diagnostics");
  const startButton = document.getElementById("startButton");
  const stopButton = document.getElementById("stopButton");
  const calibrateButton = document.getElementById("calibrateButton");
  const resetScoreButton = document.getElementById("resetScoreButton");
  const calibrationInfo = document.getElementById("calibrationInfo");
  const beatFlash = document.getElementById("beatFlash");
  const statusText = document.getElementById("statusText");
  const inputLevelBar = document.getElementById("inputLevelBar");
  const inputLevelValue = document.getElementById("inputLevelValue");
  const accuracyBar = document.getElementById("accuracyBar");
  const lastErrorEl = document.getElementById("lastError");
  const lastPointsEl = document.getElementById("lastPoints");
  const totalScoreEl = document.getElementById("totalScore");
  const hitCountEl = document.getElementById("hitCount");

  let audioContext = null;
  let isRunning = false;
  let beatTimer = null;
  let flashTimeout = null;
  let nextBeatTimeSec = 0;
  let beatIntervalMs = getBeatIntervalMs();
  let beatTimes = [];

  let mediaStream = null;
  let micSource = null;
  let analyser = null;
  let micData = null;
  let analysisRaf = null;
  let activeDeviceId = "";
  let lastDetectedAt = 0;

  let totalScore = 0;
  let hitCount = 0;
  let smoothedLevel = 0;
  let calibrationOffsetMs = 0;
  let calibrationJitterMs = 5;
  let isCalibrating = false;
  let calibrationSamples = [];
  const CALIBRATION_SAMPLE_COUNT = 16;
  const SCHEDULE_AHEAD_SEC = 0.12;
  const SCHEDULER_LOOKAHEAD_MS = 25;

  function getBeatIntervalMs() {
    const bpm = Number(bpmInput.value);
    return (60 / bpm) * 1000;
  }

  function updateBpmUI() {
    bpmValue.value = bpmInput.value;
    beatIntervalMs = getBeatIntervalMs();
  }

  function updateThresholdUI() {
    thresholdValue.value = Number(thresholdInput.value).toFixed(2);
  }

  function setStatus(message, isError) {
    statusText.textContent = message;
    statusText.classList.toggle("bad", Boolean(isError));
  }

  function setDeviceInfo(message) {
    deviceInfo.textContent = message;
  }

  function setCalibrationInfo(message) {
    calibrationInfo.textContent = message;
  }

  function formatError(error) {
    if (!error) {
      return "Unknown error";
    }
    if (error.name && error.message) {
      return error.name + ": " + error.message;
    }
    if (error.message) {
      return error.message;
    }
    return String(error);
  }

  function updateDiagnostics(extra) {
    const lines = [
      "secureContext=" + String(window.isSecureContext),
      "protocol=" + window.location.protocol,
      "host=" + window.location.host,
      "topWindow=" + String(window.top === window),
      "visibility=" + document.visibilityState,
      "mediaDevices=" + String(Boolean(navigator.mediaDevices)),
      "getUserMedia=" + String(Boolean(navigator.mediaDevices && navigator.mediaDevices.getUserMedia)),
      "enumerateDevices=" +
        String(Boolean(navigator.mediaDevices && navigator.mediaDevices.enumerateDevices))
    ];
    if (extra) {
      lines.push(extra);
    }
    diagnosticsEl.textContent = "Context: " + lines.join(" | ");
  }

  function isSafariBrowser() {
    const ua = navigator.userAgent || "";
    const vendor = navigator.vendor || "";
    return /Safari/i.test(ua) && /Apple/i.test(vendor) && !/Chrome|CriOS|Edg|OPR|Firefox|FxiOS/i.test(ua);
  }

  function assertSupportedCaptureContext() {
    if (window.location.protocol === "file:" && isSafariBrowser()) {
      throw new Error("Safari blocks microphone on file://. Open via http://localhost:8000");
    }
    if (!window.isSecureContext) {
      throw new Error("Open via a secure context (http://localhost or https)");
    }
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      throw new Error("Microphone APIs are unavailable in this browser context");
    }
  }

  function resetScore() {
    totalScore = 0;
    hitCount = 0;
    totalScoreEl.textContent = "0";
    hitCountEl.textContent = "Hits: 0";
    lastErrorEl.textContent = "--";
    lastPointsEl.textContent = "--";
    accuracyBar.style.width = "0%";
  }

  function resetInputLevelUI() {
    smoothedLevel = 0;
    inputLevelBar.style.width = "0%";
    inputLevelValue.textContent = "0%";
  }

  async function ensureAudioContext() {
    if (!audioContext) {
      audioContext = new (window.AudioContext || window.webkitAudioContext)({
        latencyHint: "interactive"
      });
    }
    if (audioContext.state === "suspended") {
      await audioContext.resume();
    }
  }

  function playClick(timeSec) {
    const osc = audioContext.createOscillator();
    const gain = audioContext.createGain();

    osc.type = "square";
    osc.frequency.setValueAtTime(1600, timeSec);

    gain.gain.setValueAtTime(0.0001, timeSec);
    gain.gain.exponentialRampToValueAtTime(0.25, timeSec + 0.001);
    gain.gain.exponentialRampToValueAtTime(0.0001, timeSec + 0.04);

    osc.connect(gain);
    gain.connect(audioContext.destination);
    osc.start(timeSec);
    osc.stop(timeSec + 0.05);
  }

  function flashBeat() {
    beatFlash.classList.add("active");
    if (flashTimeout) {
      clearTimeout(flashTimeout);
    }
    flashTimeout = setTimeout(() => beatFlash.classList.remove("active"), 90);
  }

  function pushBeatTime(timeMs) {
    beatTimes.push(timeMs);
    if (beatTimes.length > 24) {
      beatTimes.shift();
    }
  }

  function scheduleVisualFlash(beatTimeSec) {
    const delayMs = Math.max(0, (beatTimeSec - audioContext.currentTime) * 1000);
    setTimeout(flashBeat, delayMs);
  }

  function scheduleBeatLoop() {
    beatTimer = setInterval(() => {
      while (nextBeatTimeSec < audioContext.currentTime + SCHEDULE_AHEAD_SEC) {
        const beatPerfTime =
          performance.now() + (nextBeatTimeSec - audioContext.currentTime) * 1000;
        playClick(nextBeatTimeSec);
        scheduleVisualFlash(nextBeatTimeSec);
        pushBeatTime(beatPerfTime);
        nextBeatTimeSec += beatIntervalMs / 1000;
      }
    }, SCHEDULER_LOOKAHEAD_MS);
  }

  function closestBeatErrorMs(hitTimeMs) {
    if (beatTimes.length === 0) {
      return null;
    }
    let closest = Infinity;
    for (let i = 0; i < beatTimes.length; i += 1) {
      const distance = Math.abs(hitTimeMs - beatTimes[i]);
      if (distance < closest) {
        closest = distance;
      }
    }
    return closest;
  }

  function scoreFromError(errorMs) {
    return Math.max(0, Math.round(100 - Math.abs(errorMs)));
  }

  function getCalibratedError(rawErrorMs) {
    const compensated = Math.abs(rawErrorMs - calibrationOffsetMs);
    return Math.max(0, compensated - calibrationJitterMs);
  }

  function updateFeedback(rawErrorMs) {
    const correctedError = getCalibratedError(rawErrorMs);
    const points = scoreFromError(correctedError);

    lastErrorEl.textContent = String(Math.round(correctedError));
    lastPointsEl.textContent = String(points);

    const widthPercent = Math.max(0, Math.min(100, points));
    accuracyBar.style.width = widthPercent + "%";

    totalScore += points;
    hitCount += 1;
    totalScoreEl.textContent = String(totalScore);
    hitCountEl.textContent = "Hits: " + hitCount;
  }

  function startCalibration() {
    if (!isRunning) {
      setStatus("Start the metronome first, then press Calibrate", true);
      return;
    }
    isCalibrating = true;
    calibrationSamples = [];
    setStatus("Calibrating... make steady test hits for 16 beats");
    setCalibrationInfo("Calibration: collecting samples (0/" + CALIBRATION_SAMPLE_COUNT + ")");
  }

  function finalizeCalibration() {
    const sorted = calibrationSamples.slice().sort((a, b) => a - b);
    const mid = Math.floor(sorted.length / 2);
    const median =
      sorted.length % 2 === 0 ? (sorted[mid - 1] + sorted[mid]) / 2 : sorted[mid];

    const deviations = sorted
      .map((value) => Math.abs(value - median))
      .sort((a, b) => a - b);
    const devMid = Math.floor(deviations.length / 2);
    const mad =
      deviations.length % 2 === 0
        ? (deviations[devMid - 1] + deviations[devMid]) / 2
        : deviations[devMid];

    calibrationOffsetMs = median;
    calibrationJitterMs = Math.max(5, Math.min(25, Math.round(mad * 2)));
    isCalibrating = false;
    calibrationSamples = [];

    setStatus("Calibration applied");
    setCalibrationInfo(
      "Calibration: offset " +
        Math.round(calibrationOffsetMs) +
        " ms, jitter tolerance " +
        calibrationJitterMs +
        " ms"
    );
  }

  function collectCalibrationSample(rawErrorMs) {
    if (!isCalibrating) {
      return;
    }
    calibrationSamples.push(rawErrorMs);
    setCalibrationInfo(
      "Calibration: collecting samples (" +
        calibrationSamples.length +
        "/" +
        CALIBRATION_SAMPLE_COUNT +
        ")"
    );
    if (calibrationSamples.length >= CALIBRATION_SAMPLE_COUNT) {
      finalizeCalibration();
    }
  }

  function startAudioAnalysisLoop() {
    const cooldownMs = 90;
    if (analysisRaf) {
      return;
    }

    function loop() {
      if (!analyser) {
        analysisRaf = null;
        return;
      }

      analyser.getByteTimeDomainData(micData);

      let sumSquare = 0;
      for (let i = 0; i < micData.length; i += 1) {
        const normalized = (micData[i] - 128) / 128;
        sumSquare += normalized * normalized;
      }
      const rms = Math.sqrt(sumSquare / micData.length);
      const sensitivity = Number(thresholdInput.value);
      const gainMultiplier = 1 + sensitivity * 4;
      const boostedRms = rms * gainMultiplier;
      smoothedLevel = smoothedLevel * 0.78 + rms * 0.22;
      const boostedLevel = smoothedLevel * gainMultiplier;
      const levelPercent = Math.max(0, Math.min(100, Math.round(boostedLevel * 300)));
      inputLevelBar.style.width = levelPercent + "%";
      inputLevelValue.textContent = levelPercent + "%";

      if (isRunning) {
        const now = performance.now();
        // Sensitivity 0.00 = strict hit detection, 1.00 = very sensitive.
        const detectionThreshold = 0.22 - sensitivity * 0.215;
        if (boostedRms > detectionThreshold && now - lastDetectedAt > cooldownMs) {
          lastDetectedAt = now;
          const rawError = closestBeatErrorMs(now);
          if (rawError !== null) {
            collectCalibrationSample(rawError);
            updateFeedback(rawError);
          }
        }
      }

      analysisRaf = requestAnimationFrame(loop);
    }

    analysisRaf = requestAnimationFrame(loop);
  }

  function stopAudioAnalysisLoop() {
    if (analysisRaf) {
      cancelAnimationFrame(analysisRaf);
      analysisRaf = null;
    }
  }

  async function setupMicInput() {
    const selectedDeviceId = inputDeviceSelect.value;
    const normalizedDeviceId = selectedDeviceId || "default";
    if (mediaStream && analyser && activeDeviceId === normalizedDeviceId) {
      return;
    }
    cleanupMic();

    const baseAudioConstraints = {
      echoCancellation: false,
      noiseSuppression: false,
      autoGainControl: false,
      channelCount: 1,
      latency: 0
    };

    const preferredConstraints = {
      audio: selectedDeviceId
        ? Object.assign({}, baseAudioConstraints, {
            deviceId: { exact: selectedDeviceId }
          })
        : baseAudioConstraints
    };

    try {
      mediaStream = await navigator.mediaDevices.getUserMedia(preferredConstraints);
    } catch (error) {
      const fallbackConstraints = {
        audio: selectedDeviceId
          ? Object.assign({}, baseAudioConstraints, {
              deviceId: { ideal: selectedDeviceId }
            })
          : true
      };
      mediaStream = await navigator.mediaDevices.getUserMedia(fallbackConstraints);
    }

    micSource = audioContext.createMediaStreamSource(mediaStream);
    analyser = audioContext.createAnalyser();
    analyser.fftSize = 512;
    analyser.smoothingTimeConstant = 0;
    micData = new Uint8Array(analyser.fftSize);
    micSource.connect(analyser);
    activeDeviceId = normalizedDeviceId;
    startAudioAnalysisLoop();
  }

  async function requestMicAccessForDeviceLabels() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      throw new Error("Microphone API unavailable");
    }

    let tempStream = null;
    try {
      tempStream = await navigator.mediaDevices.getUserMedia({ audio: true });
    } finally {
      if (tempStream) {
        tempStream.getTracks().forEach((track) => track.stop());
      }
    }
  }

  function cleanupMic() {
    stopAudioAnalysisLoop();
    if (micSource) {
      micSource.disconnect();
      micSource = null;
    }
    analyser = null;
    micData = null;
    if (mediaStream) {
      mediaStream.getTracks().forEach((track) => track.stop());
      mediaStream = null;
    }
    activeDeviceId = "";
  }

  async function startInputPreview() {
    try {
      assertSupportedCaptureContext();
      await ensureAudioContext();
      await setupMicInput();
      if (!isRunning) {
        setStatus("Input preview active - press Start to play");
      }
      updateDiagnostics("previewDevice=" + (activeDeviceId || "none"));
    } catch (error) {
      const errorText = formatError(error);
      setStatus("Input preview error: " + errorText, true);
      updateDiagnostics("lastError=" + errorText);
      cleanupMic();
      resetInputLevelUI();
    }
  }

  async function refreshInputDevices(askPermissionFirst) {
    try {
      assertSupportedCaptureContext();
      if (!navigator.mediaDevices || !navigator.mediaDevices.enumerateDevices) {
        throw new Error("MediaDevices.enumerateDevices API unavailable");
      }
      if (document.visibilityState !== "visible") {
        throw new Error("Tab must be visible to enumerate devices");
      }
      if (askPermissionFirst) {
        setStatus("Requesting microphone permission...");
        await requestMicAccessForDeviceLabels();
      }

      const previousSelection = inputDeviceSelect.value;
      const devices = await navigator.mediaDevices.enumerateDevices();
      const audioInputs = devices.filter((d) => d.kind === "audioinput");
      inputDeviceSelect.innerHTML = "";

      if (audioInputs.length === 0) {
        const option = document.createElement("option");
        option.value = "";
        option.textContent = "No microphone found";
        inputDeviceSelect.appendChild(option);
        inputDeviceSelect.disabled = true;
        setDeviceInfo("No audio inputs detected by Safari.");
        return;
      }

      inputDeviceSelect.disabled = false;
      audioInputs.forEach((device, index) => {
        const option = document.createElement("option");
        option.value = device.deviceId;
        if (device.label) {
          option.textContent = device.label;
        } else if (device.deviceId === "default") {
          option.textContent = "Default System Input";
        } else {
          option.textContent = "Input " + (index + 1) + " (label hidden)";
        }
        inputDeviceSelect.appendChild(option);
      });
      const hasPrevious = audioInputs.some((d) => d.deviceId === previousSelection);
      if (hasPrevious) {
        inputDeviceSelect.value = previousSelection;
      }
      setDeviceInfo(
        "Detected " +
          audioInputs.length +
          " audio input" +
          (audioInputs.length === 1 ? "" : "s") +
          "."
      );
      updateDiagnostics("audioInputs=" + String(audioInputs.length));
      if (audioInputs.length > 0) {
        await startInputPreview();
      }
    } catch (error) {
      const errorText = formatError(error);
      setStatus("Unable to list input devices: " + errorText, true);
      setDeviceInfo("Permission denied, blocked, or unsupported browser context.");
      updateDiagnostics("lastError=" + errorText);
    }
  }

  async function start() {
    if (isRunning) {
      return;
    }

    try {
      assertSupportedCaptureContext();

      await ensureAudioContext();
      setStatus("Requesting microphone permission...");
      await setupMicInput();
      await refreshInputDevices(false);

      isRunning = true;
      beatTimes = [];
      nextBeatTimeSec = audioContext.currentTime + 0.02;
      beatIntervalMs = getBeatIntervalMs();
      lastDetectedAt = 0;

      startButton.disabled = true;
      stopButton.disabled = false;
      setStatus("Running - tap, clap, or strike to score");

      scheduleBeatLoop();
    } catch (error) {
      const errorText = formatError(error);
      setStatus("Mic permission/device error: " + errorText, true);
      cleanupMic();
      resetInputLevelUI();
      updateDiagnostics("lastError=" + errorText);
    }
  }

  function stop() {
    if (!isRunning) {
      return;
    }

    isRunning = false;
    if (beatTimer) {
      clearInterval(beatTimer);
      beatTimer = null;
    }
    if (flashTimeout) {
      clearTimeout(flashTimeout);
      flashTimeout = null;
    }

    // Keep the selected mic preview active so input level remains visible.
    startInputPreview();

    startButton.disabled = false;
    stopButton.disabled = true;
    beatFlash.classList.remove("active");
    setStatus("Stopped");
  }

  bpmInput.addEventListener("input", updateBpmUI);
  thresholdInput.addEventListener("input", updateThresholdUI);
  refreshDevicesButton.addEventListener("click", () => refreshInputDevices(true));
  inputDeviceSelect.addEventListener("change", startInputPreview);
  startButton.addEventListener("click", start);
  stopButton.addEventListener("click", stop);
  calibrateButton.addEventListener("click", startCalibration);
  resetScoreButton.addEventListener("click", resetScore);

  if (navigator.mediaDevices && navigator.mediaDevices.addEventListener) {
    navigator.mediaDevices.addEventListener("devicechange", () => refreshInputDevices(false));
  }

  if (navigator.mediaDevices && navigator.mediaDevices.enumerateDevices) {
    refreshInputDevices(false);
  }

  updateBpmUI();
  updateThresholdUI();
  resetScore();
  resetInputLevelUI();
  setCalibrationInfo("Calibration: none");
  setDeviceInfo("Click Detect Inputs to request access and list devices.");
  updateDiagnostics();
  if (window.location.protocol === "file:" && isSafariBrowser()) {
    setStatus("Open this app via http://localhost:8000 (Safari blocks file:// mic access)", true);
  } else {
    setStatus("Ready - click Start and allow microphone access");
  }
})();
