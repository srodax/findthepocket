(function () {
  "use strict";

  const bpmInput = document.getElementById("bpm");
  const bpmValue = document.getElementById("bpmValue");
  const thresholdInput = document.getElementById("threshold");
  const thresholdValue = document.getElementById("thresholdValue");
  const pocketModeSelect = document.getElementById("pocketMode");
  const runDurationSelect = document.getElementById("runDuration");
  const inputDeviceSelect = document.getElementById("inputDevice");
  const refreshDevicesButton = document.getElementById("refreshDevices");
  const deviceInfo = document.getElementById("deviceInfo");
  const diagnosticsEl = document.getElementById("diagnostics");
  const startButton = document.getElementById("startButton");
  const stopButton = document.getElementById("stopButton");
  const calibrateButton = document.getElementById("calibrateButton");
  const calibrationInfo = document.getElementById("calibrationInfo");
  const beatFlash = document.getElementById("beatFlash");
  const statusText = document.getElementById("statusText");
  const inputLevelBar = document.getElementById("inputLevelBar");
  const inputLevelValue = document.getElementById("inputLevelValue");
  const waveformGraph = document.getElementById("waveformGraph");
  const zoomOutButton = document.getElementById("zoomOutButton");
  const zoomInButton = document.getElementById("zoomInButton");
  const zoomLabel = document.getElementById("zoomLabel");
  const waveformYZoomOutButton = document.getElementById("waveformYZoomOutButton");
  const waveformYZoomInButton = document.getElementById("waveformYZoomInButton");
  const waveformYZoomLabel = document.getElementById("waveformYZoomLabel");
  const waveformAutoZoomButton = document.getElementById("waveformAutoZoomButton");
  const liveTempoValue = document.getElementById("liveTempoValue");
  const tempoGraph = document.getElementById("tempoGraph");
  const accuracyBar = document.getElementById("accuracyBar");
  const lastErrorEl = document.getElementById("lastError");
  const lastPointsEl = document.getElementById("lastPoints");
  const totalScoreEl = document.getElementById("totalScore");
  const hitCountEl = document.getElementById("hitCount");
  const timeLeftValueEl = document.getElementById("timeLeftValue");
  const hitsLeftValueEl = document.getElementById("hitsLeftValue");
  const accuracyScoreEl = document.getElementById("accuracyScore");
  const consistencyScoreEl = document.getElementById("consistencyScore");
  const stabilityScoreEl = document.getElementById("stabilityScore");
  const sessionAccuracyEl = document.getElementById("sessionAccuracy");
  const sessionConsistencyEl = document.getElementById("sessionConsistency");
  const sessionStabilityEl = document.getElementById("sessionStability");
  const sessionPocketEl = document.getElementById("sessionPocket");

  let audioContext = null;
  let isRunning = false;
  let isCalibrationSession = false;
  let runDurationTimeout = null;
  let sessionStatsTimer = null;
  let sessionEndTimeMs = 0;
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
  let graphTimer = null;
  let activeDeviceId = "";
  let lastDetectedAt = 0;

  let totalScore = 0;
  let hitCount = 0;
  let sessionBeatCount = 0;
  let smoothedLevel = 0;
  let lastScoredBeatTimeMs = 0;
  let offsetSamples = [];
  let recentErrors = [];
  let waveformSamples = [];
  let clickMarkers = [];
  let sessionSumAccuracy = 0;
  let sessionSumConsistency = 0;
  let sessionSumStability = 0;
  let sessionSumPocket = 0;
  let groove = 0.5;
  let runStartTimeMs = 0;
  let runEndTimeMs = 0;
  let historyViewportStartMs = 0;
  let historyScrollTargetStartMs = 0;
  let historyScrollVelocityMs = 0;
  let historyScrollRaf = null;
  let lastWheelDirection = 0;
  let hoverTimeMs = null;
  let historyViewWindowMs = 15000;
  let waveformYZoom = 1;
  let waveformQualifiedPeakAbsMax = 0;
  let calibrationOffsetMs = 0;
  let calibrationJitterMs = 5;
  let isCalibrating = false;
  let calibrationSamples = [];
  const CALIBRATION_SAMPLE_COUNT = 16;
  const SCHEDULE_AHEAD_SEC = 0.12;
  const SCHEDULER_LOOKAHEAD_MS = 25;
  const SCORING_WINDOW_HITS = 12;
  const HISTORY_VIEW_WINDOW_MIN_MS = 3000;
  const HISTORY_VIEW_WINDOW_MAX_MS = 90000;
  const WAVEFORM_Y_ZOOM_MIN = 0.35;
  const WAVEFORM_Y_ZOOM_MAX = 10;
  const WAVEFORM_VERTICAL_FRACTION = 0.5;
  const TIGHT_MS = 5;
  const BORDERLINE_MS = 10;
  const LOOSE_MS = 20;

  function getBeatIntervalMs() {
    const bpm = Number(bpmInput.value);
    return (60 / bpm) * 1000;
  }

  function updateBpmUI() {
    bpmValue.value = bpmInput.value;
    beatIntervalMs = getBeatIntervalMs();
    drawTempoGraph(performance.now());
    updateSessionStats();
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

  function getRunDurationMs() {
    const durationMinutes = Number(runDurationSelect.value);
    if (!Number.isFinite(durationMinutes) || durationMinutes <= 0) {
      return 60 * 1000;
    }
    return durationMinutes * 60 * 1000;
  }

  function clearRunDurationTimeout() {
    if (runDurationTimeout) {
      clearTimeout(runDurationTimeout);
      runDurationTimeout = null;
    }
  }

  function clearSessionStatsTimer() {
    if (sessionStatsTimer) {
      clearInterval(sessionStatsTimer);
      sessionStatsTimer = null;
    }
  }

  function formatMsToClock(ms) {
    const safeMs = Math.max(0, ms);
    const totalSeconds = Math.ceil(safeMs / 1000);
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = totalSeconds % 60;
    return String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0");
  }

  function updateSessionStats() {
    if (!isRunning) {
      timeLeftValueEl.textContent = "--:--";
      hitsLeftValueEl.textContent = "--";
      return;
    }
    const remainingMs = Math.max(0, sessionEndTimeMs - performance.now());
    const hitsLeft = Math.max(0, Math.ceil(remainingMs / beatIntervalMs));
    timeLeftValueEl.textContent = formatMsToClock(remainingMs);
    hitsLeftValueEl.textContent = String(hitsLeft);
  }

  function updateTotalScoreDisplay() {
    const normalizedScore = sessionBeatCount > 0 ? (totalScore / sessionBeatCount) * 100 : 0;
    totalScoreEl.textContent = normalizedScore.toFixed(1);
  }

  function updateZoomLabel() {
    zoomLabel.textContent = "Window: " + (historyViewWindowMs / 1000).toFixed(1) + "s";
  }

  function updateWaveformYZoomLabel() {
    waveformYZoomLabel.textContent = "Y: " + waveformYZoom.toFixed(2) + "x";
  }

  function changeWaveformYZoom(factor) {
    waveformYZoom = clamp(waveformYZoom * factor, WAVEFORM_Y_ZOOM_MIN, WAVEFORM_Y_ZOOM_MAX);
    updateWaveformYZoomLabel();
    drawWaveformGraph(performance.now());
  }

  function registerQualifiedWaveformSpike(peakAbsRaw) {
    if (!Number.isFinite(peakAbsRaw) || peakAbsRaw <= 0) {
      return;
    }
    waveformQualifiedPeakAbsMax = Math.max(waveformQualifiedPeakAbsMax, peakAbsRaw);
  }

  function autoWaveformYZoom() {
    if (waveformQualifiedPeakAbsMax <= 0.001) {
      setStatus("Auto Y needs in-beat spikes to measure", true);
      return;
    }
    // Scale so the strongest qualified spike uses most of available vertical range.
    const targetZoom = clamp(
      1 / (waveformQualifiedPeakAbsMax * WAVEFORM_VERTICAL_FRACTION),
      WAVEFORM_Y_ZOOM_MIN,
      WAVEFORM_Y_ZOOM_MAX
    );
    waveformYZoom = targetZoom;
    updateWaveformYZoomLabel();
    drawWaveformGraph(performance.now());
    setStatus("Auto Y aligned to strongest in-beat spike");
  }

  function changeHistoryZoom(factor) {
    const previousSpan = historyViewWindowMs;
    const nextSpan = clamp(
      Math.round(previousSpan * factor),
      HISTORY_VIEW_WINDOW_MIN_MS,
      HISTORY_VIEW_WINDOW_MAX_MS
    );
    if (nextSpan === previousSpan) {
      return;
    }

    const nowMs = performance.now();
    const previousViewport = getViewportRange(nowMs);
    historyViewWindowMs = nextSpan;
    updateZoomLabel();

    if (hasRunHistory() && !isRunning && !isCalibrationSession) {
      const previousCenter = (previousViewport.startMs + previousViewport.endMs) / 2;
      const half = historyViewWindowMs / 2;
      const maxStart = Math.max(runStartTimeMs, runEndTimeMs - historyViewWindowMs);
      historyViewportStartMs = clamp(previousCenter - half, runStartTimeMs, maxStart);
      historyScrollTargetStartMs = historyViewportStartMs;
      historyScrollVelocityMs = 0;
    }

    drawWaveformGraph(nowMs);
    drawTempoGraph(nowMs);
  }

  function hasRunHistory() {
    return runEndTimeMs > runStartTimeMs;
  }

  function getViewportRange(nowMs) {
    if (isRunning || isCalibrationSession) {
      return {
        startMs: nowMs - historyViewWindowMs,
        endMs: nowMs
      };
    }
    if (hasRunHistory()) {
      const maxStart = Math.max(runStartTimeMs, runEndTimeMs - historyViewWindowMs);
      const startMs = clamp(historyViewportStartMs, runStartTimeMs, maxStart);
      return {
        startMs,
        endMs: startMs + historyViewWindowMs
      };
    }
    return {
      startMs: nowMs - historyViewWindowMs,
      endMs: nowMs
    };
  }

  function getHistoryTimeFromCanvasX(event, canvas) {
    const rect = canvas.getBoundingClientRect();
    const x = event.clientX - rect.left;
    const normalized = clamp(x / rect.width, 0, 1);
    const nowMs = performance.now();
    const viewport = getViewportRange(nowMs);
    return viewport.startMs + normalized * (viewport.endMs - viewport.startMs);
  }

  function handleGraphHoverMove(event, canvas) {
    hoverTimeMs = getHistoryTimeFromCanvasX(event, canvas);
    const nowMs = performance.now();
    drawWaveformGraph(nowMs);
    drawTempoGraph(nowMs);
  }

  function handleGraphHoverLeave() {
    hoverTimeMs = null;
    const nowMs = performance.now();
    drawWaveformGraph(nowMs);
    drawTempoGraph(nowMs);
  }

  function handleGraphWheel(event) {
    if (!hasRunHistory() || isRunning || isCalibrationSession) {
      return;
    }
    const span = runEndTimeMs - runStartTimeMs;
    if (span <= historyViewWindowMs) {
      return;
    }
    event.preventDefault();
    const dominantAxisDelta =
      Math.abs(event.deltaX) > Math.abs(event.deltaY) ? event.deltaX : event.deltaY;
    let deltaPixels = dominantAxisDelta;
    if (event.deltaMode === 1) {
      deltaPixels *= 16;
    } else if (event.deltaMode === 2) {
      deltaPixels *= 800;
    }
    if (Math.abs(deltaPixels) < 0.25) {
      return;
    }
    const wheelDirection = deltaPixels > 0 ? 1 : -1;
    if (lastWheelDirection !== 0 && wheelDirection !== lastWheelDirection && Math.abs(deltaPixels) < 1.5) {
      return;
    }
    lastWheelDirection = wheelDirection;

    const msPerPixel = historyViewWindowMs / 900;
    const shiftMs = deltaPixels * msPerPixel;
    const maxStart = runEndTimeMs - historyViewWindowMs;
    if (!Number.isFinite(historyScrollTargetStartMs) || historyScrollTargetStartMs === 0) {
      historyScrollTargetStartMs = historyViewportStartMs;
    }
    historyScrollTargetStartMs = clamp(
      historyScrollTargetStartMs + shiftMs,
      runStartTimeMs,
      maxStart
    );
    if (historyScrollVelocityMs !== 0 && Math.sign(shiftMs) !== Math.sign(historyScrollVelocityMs)) {
      historyScrollVelocityMs *= 0.35;
    }
    historyScrollVelocityMs += shiftMs * 0.12;
    startHistoryScrollAnimation();
  }

  function startHistoryScrollAnimation() {
    if (historyScrollRaf) {
      return;
    }
    function frame() {
      historyScrollRaf = null;
      if (!hasRunHistory() || isRunning || isCalibrationSession) {
        historyScrollVelocityMs = 0;
        return;
      }
      const maxStart = Math.max(runStartTimeMs, runEndTimeMs - historyViewWindowMs);
      historyScrollTargetStartMs = clamp(historyScrollTargetStartMs, runStartTimeMs, maxStart);

      const distance = historyScrollTargetStartMs - historyViewportStartMs;
      historyScrollVelocityMs += distance * 0.2;
      historyScrollVelocityMs *= 0.7;
      const proposedStart = clamp(
        historyViewportStartMs + historyScrollVelocityMs,
        runStartTimeMs,
        maxStart
      );
      const remainingAfterStep = historyScrollTargetStartMs - proposedStart;
      if (distance !== 0 && Math.sign(distance) !== Math.sign(remainingAfterStep)) {
        historyViewportStartMs = historyScrollTargetStartMs;
        historyScrollVelocityMs = 0;
      } else {
        historyViewportStartMs = proposedStart;
      }

      const nowMs = performance.now();
      drawWaveformGraph(nowMs);
      drawTempoGraph(nowMs);

      const remaining = historyScrollTargetStartMs - historyViewportStartMs;
      if (Math.abs(remaining) > 0.15 || Math.abs(historyScrollVelocityMs) > 0.15) {
        historyScrollRaf = requestAnimationFrame(frame);
      } else {
        historyViewportStartMs = historyScrollTargetStartMs;
        historyScrollVelocityMs = 0;
        drawWaveformGraph(nowMs);
        drawTempoGraph(nowMs);
      }
    }
    historyScrollRaf = requestAnimationFrame(frame);
  }

  function clamp(value, min, max) {
    return Math.max(min, Math.min(max, value));
  }

  function lowerBoundByTime(items, timeMs) {
    let lo = 0;
    let hi = items.length;
    while (lo < hi) {
      const mid = Math.floor((lo + hi) / 2);
      if (items[mid].timeMs < timeMs) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return lo;
  }

  function median(values) {
    if (!values.length) {
      return 0;
    }
    const sorted = values.slice().sort((a, b) => a - b);
    const mid = Math.floor(sorted.length / 2);
    return sorted.length % 2 === 0 ? (sorted[mid - 1] + sorted[mid]) / 2 : sorted[mid];
  }

  function mad(values) {
    if (!values.length) {
      return 0;
    }
    const center = median(values);
    const deviations = values.map((value) => Math.abs(value - center));
    return median(deviations);
  }

  function percentile(values, p) {
    if (!values.length) {
      return 0;
    }
    const sorted = values.slice().sort((a, b) => a - b);
    const idx = clamp(Math.floor((sorted.length - 1) * p), 0, sorted.length - 1);
    return sorted[idx];
  }

  function getPocketTargetMs() {
    const mode = pocketModeSelect.value;
    if (mode === "laidback") {
      return 20;
    }
    if (mode === "push") {
      return -20;
    }
    return 0;
  }

  function updateSubScoreUI(accuracy, grooveValue, multiplier) {
    accuracyScoreEl.textContent = (accuracy * 100).toFixed(1) + "%";
    consistencyScoreEl.textContent = (grooveValue * 100).toFixed(0) + "%";
    stabilityScoreEl.textContent = multiplier.toFixed(3) + "x";
  }

  function updateSessionBreakdownUI() {
    sessionAccuracyEl.textContent = sessionSumAccuracy.toFixed(2);
    sessionConsistencyEl.textContent = sessionSumConsistency.toFixed(3);
    sessionStabilityEl.textContent = (sessionSumStability * 100).toFixed(0) + "%";
    sessionPocketEl.textContent = sessionSumPocket.toFixed(3) + "x";
  }

  function getMusicalScore(correctedSignedError) {
    recentErrors.push(correctedSignedError);
    if (recentErrors.length > SCORING_WINDOW_HITS) {
      recentErrors.shift();
    }

    const absCurrentError = Math.abs(correctedSignedError);
    const normalized = absCurrentError / LOOSE_MS;
    const accuracy = clamp(1 - normalized * normalized, 0, 1);

    if (absCurrentError < TIGHT_MS) {
      groove = clamp(groove + 0.08, 0, 1);
    } else if (absCurrentError < BORDERLINE_MS) {
      groove = clamp(groove + 0.02, 0, 1);
    } else if (absCurrentError < LOOSE_MS) {
      groove = clamp(groove - 0.05, 0, 1);
    } else {
      groove = clamp(groove - 0.12, 0, 1);
    }

    const multiplier = 0.5 + 0.5 * groove * groove;
    const noteScore = accuracy * multiplier;

    return {
      points: noteScore,
      accuracy,
      groove,
      multiplier,
      isScorable: accuracy > 0
    };
  }

  function refreshSubScoresFromRecent() {
    if (!recentErrors.length) {
      const multiplier = 0.5 + 0.5 * groove * groove;
      updateSubScoreUI(0, groove, multiplier);
      lastPointsEl.textContent = "0.000";
      return;
    }
    const lastError = Math.abs(recentErrors[recentErrors.length - 1]);
    const normalized = lastError / LOOSE_MS;
    const accuracy = clamp(1 - normalized * normalized, 0, 1);
    const multiplier = 0.5 + 0.5 * groove * groove;
    const noteScore = accuracy * multiplier;
    lastPointsEl.textContent = noteScore.toFixed(3);
    updateSubScoreUI(accuracy, groove, multiplier);
  }

  function valueToY(value, minValue, maxValue, graphHeight) {
    const normalized = (value - minValue) / (maxValue - minValue);
    return graphHeight - normalized * graphHeight;
  }

  function getGraphErrorRange(samples) {
    return {
      min: -35,
      max: 35
    };
  }

  function drawTempoGraph(nowMs) {
    if (!tempoGraph || !tempoGraph.getContext) {
      return;
    }

    const ctx = tempoGraph.getContext("2d");
    if (!ctx) {
      return;
    }

    const width = tempoGraph.width;
    const height = tempoGraph.height;
    const grooveBarLeft = 8;
    const grooveBarWidth = 6;
    const grooveToLabelGap = 24;
    const leftPad = grooveBarLeft + grooveBarWidth + grooveToLabelGap + 28;
    const rightPad = 8;
    const range = getGraphErrorRange(offsetSamples);
    const viewport = getViewportRange(nowMs);
    const windowStart = viewport.startMs;
    const viewportSpanMs = Math.max(1, viewport.endMs - viewport.startMs);

    ctx.clearRect(0, 0, width, height);
    ctx.font = "10px -apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica, Arial, sans-serif";
    ctx.textAlign = "right";
    ctx.textBaseline = "middle";

    const grooveTrackTop = 8;
    const grooveTrackBottom = height - 8;
    const grooveTrackHeight = grooveTrackBottom - grooveTrackTop;
    ctx.fillStyle = "rgba(170, 180, 198, 0.22)";
    ctx.fillRect(grooveBarLeft, grooveTrackTop, grooveBarWidth, grooveTrackHeight);
    const grooveFillHeight = grooveTrackHeight * clamp(groove, 0, 1);
    ctx.fillStyle = "rgba(82, 158, 255, 0.9)";
    ctx.fillRect(
      grooveBarLeft,
      grooveTrackBottom - grooveFillHeight,
      grooveBarWidth,
      grooveFillHeight
    );
    ctx.strokeStyle = "rgba(196, 205, 220, 0.45)";
    ctx.lineWidth = 1;
    ctx.strokeRect(grooveBarLeft - 0.5, grooveTrackTop - 0.5, grooveBarWidth + 1, grooveTrackHeight + 1);

    function fillBand(minMs, maxMs, color) {
      const top = valueToY(maxMs, range.min, range.max, height);
      const bottom = valueToY(minMs, range.min, range.max, height);
      const bandTop = Math.min(top, bottom);
      const bandHeight = Math.abs(bottom - top);
      ctx.fillStyle = color;
      ctx.fillRect(leftPad, bandTop, width - leftPad - rightPad, bandHeight);
    }

    // Sloppy regions (subdued red): [-20,-10] and [10,20]
    fillBand(-LOOSE_MS, -BORDERLINE_MS, "rgba(210, 86, 86, 0.16)");
    fillBand(BORDERLINE_MS, LOOSE_MS, "rgba(210, 86, 86, 0.16)");
    // Borderline regions (subdued yellow): [-10,-5] and [5,10]
    fillBand(-BORDERLINE_MS, -TIGHT_MS, "rgba(230, 182, 82, 0.14)");
    fillBand(TIGHT_MS, BORDERLINE_MS, "rgba(230, 182, 82, 0.14)");
    // Tight region (subdued green): [-5,5]
    fillBand(-TIGHT_MS, TIGHT_MS, "rgba(98, 194, 142, 0.16)");

    // Draw guide lines and labels after region fills.
    ctx.strokeStyle = "rgba(255, 255, 255, 0.12)";
    ctx.lineWidth = 1;
    const thresholdLines = [-30, -20, -10, -5, 0, 5, 10, 20, 30];
    for (let i = 0; i < thresholdLines.length; i += 1) {
      const errorMs = thresholdLines[i];
      const y = valueToY(errorMs, range.min, range.max, height);
      if (errorMs === 0) {
        ctx.strokeStyle = "rgba(132, 236, 176, 0.95)";
        ctx.lineWidth = 2;
        ctx.setLineDash([]);
      } else if (Math.abs(errorMs) === 30) {
        ctx.strokeStyle = "rgba(245, 247, 252, 0.28)";
        ctx.lineWidth = 1.4;
        ctx.setLineDash([5, 4]);
      } else {
        ctx.strokeStyle = "rgba(255, 255, 255, 0.12)";
        ctx.lineWidth = 1;
        ctx.setLineDash([]);
      }
      ctx.beginPath();
      ctx.moveTo(leftPad, y);
      ctx.lineTo(width - rightPad, y);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = Math.abs(errorMs) === 30 ? "rgba(245, 247, 252, 0.8)" : "rgba(245, 247, 252, 0.68)";
      const rounded = Math.round(errorMs);
      const label = rounded > 0 ? "+" + rounded + " ms" : rounded + " ms";
      ctx.fillText(label, leftPad - 4, y);
    }

    // Zone labels on negative side, bottom-right in each band.
    ctx.font = "10px -apple-system, BlinkMacSystemFont, Segoe UI, Roboto, Helvetica, Arial, sans-serif";
    ctx.textAlign = "left";
    ctx.textBaseline = "alphabetic";
    const zoneLabelX = leftPad + 6;
    ctx.fillStyle = "rgba(210, 86, 86, 0.72)";
    ctx.fillText("loose", zoneLabelX, valueToY(-LOOSE_MS, range.min, range.max, height) - 4);
    ctx.fillStyle = "rgba(198, 202, 210, 0.62)";
    ctx.fillText("sloppy", zoneLabelX, valueToY(-30, range.min, range.max, height) - 4);
    ctx.fillStyle = "rgba(230, 182, 82, 0.62)";
    ctx.fillText(
      "borderline",
      zoneLabelX,
      valueToY(-BORDERLINE_MS, range.min, range.max, height) - 4
    );
    ctx.fillStyle = "rgba(98, 194, 142, 0.52)";
    ctx.fillText("tight", zoneLabelX, valueToY(-TIGHT_MS, range.min, range.max, height) - 4);

    if (offsetSamples.length > 0) {
      const startIdx = Math.max(0, lowerBoundByTime(offsetSamples, viewport.startMs) - 1);
      const yTopSloppy = valueToY(30, range.min, range.max, height);
      const yBottomSloppy = valueToY(-30, range.min, range.max, height);
      ctx.save();
      ctx.beginPath();
      ctx.rect(leftPad, Math.min(yTopSloppy, yBottomSloppy), width - leftPad - rightPad, Math.abs(yBottomSloppy - yTopSloppy));
      ctx.clip();
      ctx.strokeStyle = "rgba(255, 205, 84, 0.98)";
      ctx.lineWidth = 2.5;
      ctx.beginPath();
      let hasLinePoint = false;
      for (let i = startIdx; i < offsetSamples.length; i += 1) {
        const sample = offsetSamples[i];
        const isPastViewport = sample.timeMs > viewport.endMs;
        const x =
          leftPad +
          ((sample.timeMs - windowStart) / viewportSpanMs) * (width - leftPad - rightPad);
        const y = valueToY(sample.errorMs, range.min, range.max, height);
        if (!hasLinePoint) {
          ctx.moveTo(x, y);
          hasLinePoint = true;
        } else {
          ctx.lineTo(x, y);
        }
        if (isPastViewport) {
          break;
        }
      }
      if (hasLinePoint) {
        ctx.stroke();
      }
      ctx.restore();

      ctx.fillStyle = "rgba(255, 205, 84, 1)";
      for (let i = startIdx; i < offsetSamples.length; i += 1) {
        const sample = offsetSamples[i];
        if (sample.timeMs > viewport.endMs) {
          break;
        }
        if (!sample.showPoint) {
          continue;
        }
        const x =
          leftPad +
          ((sample.timeMs - windowStart) / viewportSpanMs) * (width - leftPad - rightPad);
        const y = valueToY(sample.errorMs, range.min, range.max, height);
        ctx.beginPath();
        ctx.arc(x, y, 2.2, 0, Math.PI * 2);
        ctx.fill();
      }
    }

    if (hoverTimeMs !== null && hoverTimeMs >= viewport.startMs && hoverTimeMs <= viewport.endMs) {
      const crossX =
        leftPad + ((hoverTimeMs - windowStart) / viewportSpanMs) * (width - leftPad - rightPad);
      ctx.strokeStyle = "rgba(230, 232, 236, 0.46)";
      ctx.lineWidth = 1.1;
      ctx.setLineDash([]);
      ctx.beginPath();
      ctx.moveTo(crossX, 0);
      ctx.lineTo(crossX, height);
      ctx.stroke();
    }
  }

  function resetTempoGraph() {
    offsetSamples = [];
    liveTempoValue.textContent = "--";
    drawTempoGraph(performance.now());
  }

  function addClickMarker(timeMs) {
    clickMarkers.push(timeMs);
  }

  function getWaveChunkTiming(nowMs) {
    if (!audioContext || !micData || micData.length < 2) {
      return null;
    }
    const sampleRate = audioContext.sampleRate || 44100;
    const spanMs = (micData.length / sampleRate) * 1000;
    return {
      spanMs,
      startMs: nowMs - spanMs
    };
  }

  function getOutputLatencyMs() {
    if (!audioContext) {
      return 0;
    }
    if (Number.isFinite(audioContext.outputLatency) && audioContext.outputLatency > 0) {
      return audioContext.outputLatency * 1000;
    }
    return 0;
  }

  function addWaveformSamples(nowMs) {
    if (!micData || !audioContext) {
      return;
    }
    const timing = getWaveChunkTiming(nowMs);
    if (!timing) {
      return;
    }
    const stride = 8;
    for (let i = 0; i < micData.length; i += stride) {
      const value = (micData[i] - 128) / 128;
      const t = timing.startMs + (i / (micData.length - 1)) * timing.spanMs;
      waveformSamples.push({ timeMs: t, value });
    }
  }

  function drawWaveformGraph(nowMs) {
    if (!waveformGraph || !waveformGraph.getContext) {
      return;
    }
    const ctx = waveformGraph.getContext("2d");
    if (!ctx) {
      return;
    }
    const width = waveformGraph.width;
    const height = waveformGraph.height;
    const leftPad = 8;
    const rightPad = 8;
    const topPad = 8;
    const bottomPad = 8;
    const drawWidth = width - leftPad - rightPad;
    const drawHeight = height - topPad - bottomPad;
    const centerY = topPad + drawHeight / 2;
    const viewport = getViewportRange(nowMs);
    const windowStart = viewport.startMs;
    const viewportSpanMs = Math.max(1, viewport.endMs - viewport.startMs);

    ctx.clearRect(0, 0, width, height);
    ctx.strokeStyle = "rgba(255, 255, 255, 0.12)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(leftPad, centerY);
    ctx.lineTo(width - rightPad, centerY);
    ctx.stroke();

    ctx.strokeStyle = "rgba(125, 205, 255, 0.9)";
    ctx.lineWidth = 1.8;
    for (let i = 0; i < clickMarkers.length; i += 1) {
      // Shift visual click markers by learned calibration offset so
      // calibrated "on-beat" spikes line up with marker positions.
      const calibratedMarkerTime = clickMarkers[i] + calibrationOffsetMs;
      if (calibratedMarkerTime < viewport.startMs || calibratedMarkerTime > viewport.endMs) {
        continue;
      }
      const x = leftPad + ((calibratedMarkerTime - windowStart) / viewportSpanMs) * drawWidth;
      ctx.beginPath();
      ctx.moveTo(x, topPad);
      ctx.lineTo(x, height - bottomPad);
      ctx.stroke();
    }

    if (waveformSamples.length > 0) {
      const startIdx = lowerBoundByTime(waveformSamples, viewport.startMs);
      ctx.strokeStyle = "rgba(255, 206, 110, 0.98)";
      ctx.lineWidth = 1.4;
      ctx.beginPath();
      let hasLinePoint = false;
      for (let i = startIdx; i < waveformSamples.length; i += 1) {
        const point = waveformSamples[i];
        if (point.timeMs > viewport.endMs) {
          break;
        }
        const x = leftPad + ((point.timeMs - windowStart) / viewportSpanMs) * drawWidth;
        const y = centerY - point.value * (drawHeight * WAVEFORM_VERTICAL_FRACTION * waveformYZoom);
        if (!hasLinePoint) {
          ctx.moveTo(x, y);
          hasLinePoint = true;
        } else {
          ctx.lineTo(x, y);
        }
      }
      if (hasLinePoint) {
        ctx.stroke();
      }
    }

    if (hoverTimeMs !== null && hoverTimeMs >= viewport.startMs && hoverTimeMs <= viewport.endMs) {
      const crossX = leftPad + ((hoverTimeMs - windowStart) / viewportSpanMs) * drawWidth;
      ctx.strokeStyle = "rgba(230, 232, 236, 0.46)";
      ctx.lineWidth = 1.1;
      ctx.beginPath();
      ctx.moveTo(crossX, 0);
      ctx.lineTo(crossX, height);
      ctx.stroke();
    }
  }

  function resetWaveformGraph() {
    waveformSamples = [];
    clickMarkers = [];
    waveformQualifiedPeakAbsMax = 0;
    drawWaveformGraph(performance.now());
  }

  function detectHitFromWaveformChunk(nowMs, sensitivity, gainMultiplier) {
    const timing = getWaveChunkTiming(nowMs);
    if (!timing || !micData) {
      return null;
    }

    // Transient detection inspired by standard onset pipelines:
    // 1) smoothed amplitude envelope
    // 2) positive first-difference novelty
    // 3) adaptive thresholding on novelty + amplitude gate
    // 4) backtrack from novelty peak to attack onset crossing
    const length = micData.length;
    const rawAbs = new Array(length);
    const envelope = new Array(length);
    const novelty = new Array(length);
    const smoothing = 0.35;
    let prevEnv = 0;
    for (let i = 0; i < length; i += 1) {
      const absValue = Math.abs((micData[i] - 128) / 128);
      rawAbs[i] = absValue;
      const boosted = absValue * gainMultiplier;
      const env = prevEnv * (1 - smoothing) + boosted * smoothing;
      envelope[i] = env;
      novelty[i] = i === 0 ? 0 : Math.max(0, env - prevEnv);
      prevEnv = env;
    }

    const envMedian = median(envelope);
    const envMad = mad(envelope);
    const envPeak = Math.max.apply(null, envelope);
    const amplitudeGate = Math.max(
      0.04,
      envMedian + envMad * (1.8 - sensitivity * 0.8)
    );
    if (envPeak < amplitudeGate) {
      return null;
    }

    const noveltyMedian = median(novelty);
    const noveltyMad = mad(novelty);
    const noveltyGate = noveltyMedian + noveltyMad * 2.8;
    let noveltyPeak = 0;
    let peakIdx = -1;
    for (let i = 1; i < length; i += 1) {
      if (envelope[i] < amplitudeGate) {
        continue;
      }
      if (novelty[i] > noveltyPeak) {
        noveltyPeak = novelty[i];
        peakIdx = i;
      }
    }
    if (peakIdx < 0 || noveltyPeak < noveltyGate) {
      return null;
    }

    const baseline = percentile(envelope, 0.2);
    const onsetLevel = baseline + Math.max(0.01, (envelope[peakIdx] - baseline) * 0.12);
    let onsetIdx = peakIdx;
    for (let i = peakIdx; i >= 1; i -= 1) {
      if (envelope[i - 1] <= onsetLevel && envelope[i] >= onsetLevel) {
        onsetIdx = i;
        break;
      }
    }

    // Linear interpolation around crossing for sub-sample onset timing.
    const prev = envelope[Math.max(0, onsetIdx - 1)];
    const curr = envelope[onsetIdx];
    const denom = curr - prev;
    const frac = denom !== 0 ? clamp((onsetLevel - prev) / denom, 0, 1) : 0;
    const onsetSample = Math.max(0, onsetIdx - 1) + frac;

    return {
      hitTimeMs: timing.startMs + (onsetSample / (length - 1)) * timing.spanMs,
      peakAbsRaw: rawAbs[peakIdx]
    };
  }

  function addOffsetSample(hitTimeMs, correctedSignedErrorMs, showPoint) {
    const clamped = Math.max(-250, Math.min(250, correctedSignedErrorMs));
    offsetSamples.push({
      timeMs: hitTimeMs,
      errorMs: clamped,
      showPoint: Boolean(showPoint)
    });
    const rounded = Math.round(clamped);
    liveTempoValue.textContent = rounded > 0 ? "+" + rounded : String(rounded);
    drawTempoGraph(hitTimeMs);
  }

  function startGraphLoop() {
    if (graphTimer) {
      return;
    }
    graphTimer = setInterval(() => {
      drawTempoGraph(performance.now());
    }, 120);
  }

  function stopGraphLoop() {
    if (graphTimer) {
      clearInterval(graphTimer);
      graphTimer = null;
    }
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
    sessionBeatCount = 0;
    recentErrors = [];
    groove = 0.5;
    sessionSumAccuracy = 0;
    sessionSumConsistency = 0;
    sessionSumStability = 0;
    sessionSumPocket = 0;
    totalScoreEl.textContent = "0.0";
    hitCountEl.textContent = "Hits: 0";
    lastErrorEl.textContent = "--";
    lastPointsEl.textContent = "--";
    accuracyBar.style.width = "0%";
    updateSubScoreUI(0, groove, 0.5 + 0.5 * groove * groove);
    updateSessionBreakdownUI();
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
        const beatReferenceTime = beatPerfTime + getOutputLatencyMs();
        if (isRunning && sessionEndTimeMs > 0 && beatReferenceTime <= sessionEndTimeMs) {
          sessionBeatCount += 1;
          updateTotalScoreDisplay();
        }
        playClick(nextBeatTimeSec);
        scheduleVisualFlash(nextBeatTimeSec);
        pushBeatTime(beatReferenceTime);
        addClickMarker(beatReferenceTime);
        nextBeatTimeSec += beatIntervalMs / 1000;
      }
    }, SCHEDULER_LOOKAHEAD_MS);
  }

  function closestBeatMatch(hitTimeMs) {
    if (beatTimes.length === 0) {
      return null;
    }
    let closestDistance = Infinity;
    let closestBeatTime = beatTimes[0];
    for (let i = 0; i < beatTimes.length; i += 1) {
      const beatTime = beatTimes[i];
      const distance = Math.abs(hitTimeMs - beatTime);
      if (distance < closestDistance) {
        closestDistance = distance;
        closestBeatTime = beatTime;
      }
    }
    return {
      beatTimeMs: closestBeatTime,
      signedErrorMs: hitTimeMs - closestBeatTime,
      absErrorMs: closestDistance
    };
  }

  function getCalibratedSignedError(rawErrorMs) {
    const compensated = rawErrorMs - calibrationOffsetMs;
    const absCompensated = Math.abs(compensated);
    if (absCompensated <= calibrationJitterMs) {
      return 0;
    }
    const sign = compensated < 0 ? -1 : 1;
    return sign * (absCompensated - calibrationJitterMs);
  }

  function updateFeedback(rawErrorMs, hitTimeMs, peakAbsRaw) {
    const correctedSignedError = getCalibratedSignedError(rawErrorMs);
    const score = getMusicalScore(correctedSignedError);
    const points = score.points;
    updateSubScoreUI(score.accuracy, score.groove, score.multiplier);

    const roundedError = Math.round(correctedSignedError);
    lastErrorEl.textContent = roundedError > 0 ? "+" + roundedError : String(roundedError);
    lastPointsEl.textContent = points.toFixed(3);
    const isPlottable = Math.abs(correctedSignedError) <= 30;
    if (isPlottable) {
      registerQualifiedWaveformSpike(peakAbsRaw);
    }
    addOffsetSample(hitTimeMs, correctedSignedError, isPlottable);

    const widthPercent = clamp(points * 100, 0, 100);
    accuracyBar.style.width = widthPercent + "%";

    totalScore += points;
    hitCount += 1;
    sessionSumAccuracy += score.accuracy;
    sessionSumConsistency = totalScore / hitCount;
    sessionSumStability = score.groove;
    sessionSumPocket = score.multiplier;
    updateSessionBreakdownUI();
    updateTotalScoreDisplay();
    hitCountEl.textContent = "Hits: " + hitCount;
  }

  function startCalibration() {
    if (isCalibrationSession) {
      return;
    }
    if (isRunning) {
      stop();
    }
    startCalibrationSession();
  }

  function stopBeatEngine() {
    clearRunDurationTimeout();
    clearSessionStatsTimer();
    sessionEndTimeMs = 0;
    if (historyScrollRaf) {
      cancelAnimationFrame(historyScrollRaf);
      historyScrollRaf = null;
    }
    if (beatTimer) {
      clearInterval(beatTimer);
      beatTimer = null;
    }
    if (flashTimeout) {
      clearTimeout(flashTimeout);
      flashTimeout = null;
    }
    beatFlash.classList.remove("active");
  }

  async function startCalibrationSession() {
    try {
      assertSupportedCaptureContext();
      await ensureAudioContext();
      await setupMicInput();
      await refreshInputDevices(false);

      isCalibrationSession = true;
      isCalibrating = true;
      calibrationSamples = [];
      beatTimes = [];
      nextBeatTimeSec = audioContext.currentTime + 0.02;
      beatIntervalMs = getBeatIntervalMs();
      lastDetectedAt = 0;
      lastScoredBeatTimeMs = 0;
      resetTempoGraph();

      startButton.disabled = true;
      stopButton.disabled = true;
      calibrateButton.disabled = true;
      setStatus("Calibrating... play steady for 16 beats");
      setCalibrationInfo("Calibration: collecting samples (0/" + CALIBRATION_SAMPLE_COUNT + ")");

      scheduleBeatLoop();
      startGraphLoop();
    } catch (error) {
      const errorText = formatError(error);
      isCalibrationSession = false;
      isCalibrating = false;
      calibrateButton.disabled = false;
      setStatus("Calibration setup failed: " + errorText, true);
      updateDiagnostics("lastError=" + errorText);
    }
  }

  function stopCalibrationSession(completed) {
    isCalibrationSession = false;
    isCalibrating = false;
    stopBeatEngine();
    stopGraphLoop();
    drawTempoGraph(performance.now());
    startInputPreview();

    startButton.disabled = false;
    stopButton.disabled = true;
    calibrateButton.disabled = false;

    if (completed) {
      setStatus("Calibration complete - press Start to play");
    } else {
      setStatus("Calibration stopped");
    }
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

    calibrationOffsetMs = Math.max(-150, Math.min(150, median));
    calibrationJitterMs = Math.max(5, Math.min(25, Math.round(mad * 2)));
    calibrationSamples = [];

    setStatus("Calibration applied");
    setCalibrationInfo(
      "Calibration: offset " +
        Math.round(calibrationOffsetMs) +
        " ms, jitter tolerance " +
        calibrationJitterMs +
        " ms"
    );
    if (isCalibrationSession) {
      stopCalibrationSession(true);
    } else {
      isCalibrating = false;
    }
  }

  function collectCalibrationSample(rawErrorMs) {
    if (!isCalibrating) {
      return;
    }
    if (Math.abs(rawErrorMs) > beatIntervalMs * 0.45) {
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
      smoothedLevel = smoothedLevel * 0.78 + rms * 0.22;
      const boostedLevel = smoothedLevel * gainMultiplier;
      const levelPercent = Math.max(0, Math.min(100, Math.round(boostedLevel * 300)));
      inputLevelBar.style.width = levelPercent + "%";
      inputLevelValue.textContent = levelPercent + "%";
      const now = performance.now();
      addWaveformSamples(now);
      drawWaveformGraph(now);

      if (isRunning || isCalibrationSession) {
        const dynamicCooldownMs = Math.max(85, Math.min(190, beatIntervalMs * 0.35));
        const hit = detectHitFromWaveformChunk(now, sensitivity, gainMultiplier);
        if (hit !== null && hit.hitTimeMs - lastDetectedAt > dynamicCooldownMs) {
          lastDetectedAt = hit.hitTimeMs;
          const match = closestBeatMatch(hit.hitTimeMs);
          if (match !== null) {
            if (Math.abs(match.beatTimeMs - lastScoredBeatTimeMs) < 1) {
              analysisRaf = requestAnimationFrame(loop);
              return;
            }
            lastScoredBeatTimeMs = match.beatTimeMs;
            collectCalibrationSample(match.signedErrorMs);
            if (isRunning) {
              updateFeedback(match.signedErrorMs, hit.hitTimeMs, hit.peakAbsRaw);
            } else {
              const correctedSignedError = getCalibratedSignedError(match.signedErrorMs);
              const isPlottable = Math.abs(correctedSignedError) <= 30;
              if (isPlottable) {
                registerQualifiedWaveformSpike(hit.peakAbsRaw);
              }
              addOffsetSample(hit.hitTimeMs, correctedSignedError, isPlottable);
            }
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
    if (isRunning || isCalibrationSession) {
      return;
    }

    try {
      assertSupportedCaptureContext();

      await ensureAudioContext();
      setStatus("Requesting microphone permission...");
      await setupMicInput();
      await refreshInputDevices(false);

      isRunning = true;
      resetScore();
      hoverTimeMs = null;
      runStartTimeMs = performance.now();
      runEndTimeMs = 0;
      historyViewportStartMs = runStartTimeMs;
      historyScrollTargetStartMs = historyViewportStartMs;
      historyScrollVelocityMs = 0;
      lastWheelDirection = 0;
      beatTimes = [];
      nextBeatTimeSec = audioContext.currentTime + 0.02;
      beatIntervalMs = getBeatIntervalMs();
      lastDetectedAt = 0;
      lastScoredBeatTimeMs = 0;
      resetTempoGraph();
      resetWaveformGraph();

      startButton.disabled = true;
      stopButton.disabled = false;
      setStatus("Running - tap, clap, or strike to score");

      const runDurationMs = getRunDurationMs();
      sessionEndTimeMs = performance.now() + runDurationMs;
      scheduleBeatLoop();
      startGraphLoop();
      updateSessionStats();
      clearSessionStatsTimer();
      sessionStatsTimer = setInterval(updateSessionStats, 200);
      runDurationTimeout = setTimeout(() => {
        if (isRunning) {
          stop("finished");
        }
      }, runDurationMs);
    } catch (error) {
      const errorText = formatError(error);
      setStatus("Mic permission/device error: " + errorText, true);
      cleanupMic();
      resetInputLevelUI();
      updateDiagnostics("lastError=" + errorText);
    }
  }

  function stop(reason) {
    if (isCalibrationSession) {
      stopCalibrationSession(false);
      return;
    }
    if (!isRunning) {
      return;
    }

    isRunning = false;
    runEndTimeMs = performance.now();
    historyViewportStartMs = Math.max(runStartTimeMs, runEndTimeMs - historyViewWindowMs);
    historyScrollTargetStartMs = historyViewportStartMs;
    historyScrollVelocityMs = 0;
    lastWheelDirection = 0;
    stopBeatEngine();
    stopGraphLoop();
    cleanupMic();
    drawWaveformGraph(runEndTimeMs);
    drawTempoGraph(runEndTimeMs);

    startButton.disabled = false;
    stopButton.disabled = true;
    if (reason === "finished") {
      setStatus("Run finished - review your score");
    } else {
      setStatus("Stopped");
    }
    updateSessionStats();
  }

  bpmInput.addEventListener("input", updateBpmUI);
  thresholdInput.addEventListener("input", updateThresholdUI);
  refreshDevicesButton.addEventListener("click", () => refreshInputDevices(true));
  inputDeviceSelect.addEventListener("change", startInputPreview);
  pocketModeSelect.addEventListener("change", () => {
    refreshSubScoresFromRecent();
    setStatus("Pocket mode set to " + pocketModeSelect.options[pocketModeSelect.selectedIndex].text);
  });
  startButton.addEventListener("click", start);
  stopButton.addEventListener("click", stop);
  calibrateButton.addEventListener("click", startCalibration);
  zoomInButton.addEventListener("click", () => changeHistoryZoom(0.7));
  zoomOutButton.addEventListener("click", () => changeHistoryZoom(1.4));
  waveformYZoomInButton.addEventListener("click", () => changeWaveformYZoom(1.25));
  waveformYZoomOutButton.addEventListener("click", () => changeWaveformYZoom(0.8));
  waveformAutoZoomButton.addEventListener("click", autoWaveformYZoom);
  waveformGraph.addEventListener("mousemove", (event) => handleGraphHoverMove(event, waveformGraph));
  tempoGraph.addEventListener("mousemove", (event) => handleGraphHoverMove(event, tempoGraph));
  waveformGraph.addEventListener("mouseleave", handleGraphHoverLeave);
  tempoGraph.addEventListener("mouseleave", handleGraphHoverLeave);
  waveformGraph.addEventListener("wheel", handleGraphWheel, { passive: false });
  tempoGraph.addEventListener("wheel", handleGraphWheel, { passive: false });

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
  resetTempoGraph();
  resetWaveformGraph();
  updateZoomLabel();
  updateWaveformYZoomLabel();
  updateSessionStats();
  setCalibrationInfo("Calibration: none");
  setDeviceInfo("Click Detect Inputs to request access and list devices.");
  updateDiagnostics();
  if (window.location.protocol === "file:" && isSafariBrowser()) {
    setStatus("Open this app via http://localhost:8000 (Safari blocks file:// mic access)", true);
  } else {
    setStatus("Ready - click Start and allow microphone access");
  }
})();
