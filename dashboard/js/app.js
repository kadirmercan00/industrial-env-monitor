/**
 * ============================================================
 * MERCAN STUDIO — SCADA & Telemetry Dashboard Controller
 * Author: Kadir Mercan (Mercan Studio)
 * ============================================================
 */

document.addEventListener('DOMContentLoaded', () => {

  // ── 1. CONFIGURATION & STATE ──────────────────────────────
  const state = {
    isSimulating: true,
    simulationInterval: null,
    updateRateMs: 3000,
    bufferSize: 30,
    eventCount: 0,
    alarmActive: false,
    spikeActive: false,
    
    // Telemetry Buffer
    labels: [],
    coSeries: [],
    co2Series: [],
    tvocSeries: [],
    soundSeries: [],

    // Running Stats
    coSum: 0,
    coCount: 0,
    tvocSum: 0,
    tvocCount: 0,
    peakCo2: 0,
    peakSound: 0,

    // AWS Endpoint Config
    awsEndpoint: localStorage.getItem('scada_aws_endpoint') || '',
    facilityId: localStorage.getItem('scada_facility_id') || 'PLANT_01',
    stationId: localStorage.getItem('scada_station_id') || 'ENV_STATION_001'
  };

  // ── 2. DOM ELEMENT REFERENCES ─────────────────────────────
  const el = {
    valCo: document.getElementById('valCo'),
    barCo: document.getElementById('barCo'),
    statusCo: document.getElementById('statusCo'),
    avgCo: document.getElementById('avgCo'),
    cardCo: document.getElementById('cardCo'),

    valCo2: document.getElementById('valCo2'),
    barCo2: document.getElementById('barCo2'),
    statusCo2: document.getElementById('statusCo2'),
    peakCo2: document.getElementById('peakCo2'),
    cardCo2: document.getElementById('cardCo2'),

    valTvoc: document.getElementById('valTvoc'),
    barTvoc: document.getElementById('barTvoc'),
    statusTvoc: document.getElementById('statusTvoc'),
    avgTvoc: document.getElementById('avgTvoc'),

    valSound: document.getElementById('valSound'),
    barSound: document.getElementById('barSound'),
    statusSound: document.getElementById('statusSound'),
    peakSound: document.getElementById('peakSound'),
    cardSound: document.getElementById('cardSound'),

    valTemp: document.getElementById('valTemp'),
    valHum: document.getElementById('valHum'),

    alarmBanner: document.getElementById('alarmBanner'),
    alarmTitle: document.getElementById('alarmTitle'),
    alarmDesc: document.getElementById('alarmDesc'),
    systemStatusDot: document.getElementById('systemStatusDot'),
    systemStatusText: document.getElementById('systemStatusText'),

    eventLogStream: document.getElementById('eventLogStream'),
    logCount: document.getElementById('logCount'),
    toggleSimBtn: document.getElementById('toggleSimBtn'),
    toggleAwsBtn: document.getElementById('toggleAwsBtn'),
    awsModal: document.getElementById('awsModal'),
    awsApiUrl: document.getElementById('awsApiUrl')
  };

  // ── 3. CHART.JS INITIALIZATION ────────────────────────────
  // Global Chart Defaults
  Chart.defaults.color = '#9ca3af';
  Chart.defaults.font.family = "'JetBrains Mono', monospace";
  Chart.defaults.font.size = 11;

  // Chart 1: Multi-Gas Dynamics
  const ctxGas = document.getElementById('gasChart').getContext('2d');
  const gasChart = new Chart(ctxGas, {
    type: 'line',
    data: {
      labels: state.labels,
      datasets: [
        {
          label: 'Carbon Monoxide (PPM)',
          data: state.coSeries,
          borderColor: '#ef4444',
          backgroundColor: 'rgba(239, 68, 68, 0.1)',
          borderWidth: 2,
          yAxisID: 'yCO',
          tension: 0.3,
          pointRadius: 0
        },
        {
          label: 'Air Quality eCO2 (PPM)',
          data: state.co2Series,
          borderColor: '#3b82f6',
          borderWidth: 2,
          yAxisID: 'yGas',
          tension: 0.3,
          pointRadius: 0
        },
        {
          label: 'Total VOC (PPB)',
          data: state.tvocSeries,
          borderColor: '#10b981',
          borderWidth: 1.5,
          borderDash: [4, 4],
          yAxisID: 'yGas',
          tension: 0.3,
          pointRadius: 0
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 300 },
      interaction: { mode: 'index', intersect: false },
      plugins: {
        legend: {
          position: 'top',
          labels: { boxWidth: 12, usePointStyle: true }
        },
        tooltip: {
          backgroundColor: 'rgba(17, 24, 39, 0.95)',
          titleFont: { family: "'Plus Jakarta Sans', sans-serif", weight: 'bold' }
        }
      },
      scales: {
        x: {
          grid: { color: 'rgba(255, 255, 255, 0.04)' }
        },
        yCO: {
          type: 'linear',
          position: 'left',
          title: { display: true, text: 'CO (PPM)', color: '#ef4444' },
          min: 0,
          suggestedMax: 20,
          grid: { color: 'rgba(255, 255, 255, 0.05)' }
        },
        yGas: {
          type: 'linear',
          position: 'right',
          title: { display: true, text: 'eCO2 (PPM) / TVOC (PPB)', color: '#3b82f6' },
          min: 350,
          suggestedMax: 1500,
          grid: { drawOnChartArea: false }
        }
      }
    }
  });

  // Chart 2: Acoustic Noise & Safety Limits
  const ctxSound = document.getElementById('soundChart').getContext('2d');
  const soundChart = new Chart(ctxSound, {
    type: 'line',
    data: {
      labels: state.labels,
      datasets: [
        {
          label: 'Noise Level (dBA)',
          data: state.soundSeries,
          borderColor: '#f59e0b',
          backgroundColor: 'rgba(245, 158, 11, 0.12)',
          fill: true,
          borderWidth: 2,
          tension: 0.3,
          pointRadius: 0
        },
        {
          label: 'OSHA 85 dBA Action Limit',
          data: [],
          borderColor: 'rgba(239, 68, 68, 0.65)',
          borderWidth: 1.5,
          borderDash: [6, 4],
          pointRadius: 0,
          fill: false
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 300 },
      plugins: {
        legend: {
          position: 'top',
          labels: { boxWidth: 12, usePointStyle: true }
        }
      },
      scales: {
        x: { grid: { color: 'rgba(255, 255, 255, 0.04)' } },
        y: {
          min: 40,
          suggestedMax: 95,
          title: { display: true, text: 'Sound Pressure (dBA)' },
          grid: { color: 'rgba(255, 255, 255, 0.05)' }
        }
      }
    }
  });

  // ── 4. TELEMETRY INGESTION & UI BINDING ────────────────────
  function processIncomingTelemetry(packet) {
    const timeStr = new Date(packet.timestamp * 1000).toLocaleTimeString();
    const m = packet.metrics;

    // Buffer Maintenance
    if (state.labels.length >= state.bufferSize) {
      state.labels.shift();
      state.coSeries.shift();
      state.co2Series.shift();
      state.tvocSeries.shift();
      state.soundSeries.shift();
    }

    state.labels.push(timeStr);
    state.coSeries.push(m.co_ppm);
    state.co2Series.push(m.eco2_ppm);
    state.tvocSeries.push(m.tvoc_ppb);
    state.soundSeries.push(m.sound_dba);

    // Update OSHA Limit Reference Line
    soundChart.data.datasets[1].data = state.labels.map(() => 85.0);

    // Compute Stats
    state.coSum += m.co_ppm;
    state.coCount++;
    state.tvocSum += m.tvoc_ppb;
    state.tvocCount++;
    if (m.eco2_ppm > state.peakCo2) state.peakCo2 = m.eco2_ppm;
    if (m.sound_dba > state.peakSound) state.peakSound = m.sound_dba;

    // Render Metric Cards
    el.valCo.textContent = m.co_ppm.toFixed(1);
    el.barCo.style.width = Math.min((m.co_ppm / 50) * 100, 100) + '%';
    el.avgCo.textContent = (state.coSum / state.coCount).toFixed(1);

    el.valCo2.textContent = m.eco2_ppm;
    el.barCo2.style.width = Math.min((m.eco2_ppm / 2500) * 100, 100) + '%';
    el.peakCo2.textContent = state.peakCo2;

    el.valTvoc.textContent = m.tvoc_ppb;
    el.barTvoc.style.width = Math.min((m.tvoc_ppb / 600) * 100, 100) + '%';
    el.avgTvoc.textContent = Math.round(state.tvocSum / state.tvocCount);

    el.valSound.textContent = m.sound_dba.toFixed(1);
    el.barSound.style.width = Math.min(((m.sound_dba - 40) / 60) * 100, 100) + '%';
    el.peakSound.textContent = state.peakSound.toFixed(1);

    el.valTemp.textContent = m.temperature_c.toFixed(1);
    el.valHum.textContent = m.humidity_rh.toFixed(1);

    // Compliance Evaluation
    evaluateSafetyCompliance(m);

    // Refresh Visual Charts
    gasChart.update();
    soundChart.update();
  }

  function evaluateSafetyCompliance(m) {
    let hasCriticalAlarm = false;

    // 1. Carbon Monoxide Check (OSHA 50 ppm limit, 25 ppm warning)
    if (m.co_ppm >= 50.0) {
      hasCriticalAlarm = true;
      el.cardCo.className = 'metric-card danger';
      el.barCo.style.backgroundColor = '#ef4444';
      el.statusCo.className = 'sensor-status danger';
      el.statusCo.textContent = '● CRITICAL: Toxic CO Overexposure';
      triggerAlarm('OSHA CRITICAL: TOXIC CO LEVEL BREACH', `Carbon Monoxide reading (${m.co_ppm} ppm) exceeds 50 ppm limit! Evacuation & ventilation required.`);
    } else if (m.co_ppm >= 25.0) {
      el.cardCo.className = 'metric-card warning';
      el.barCo.style.backgroundColor = '#f59e0b';
      el.statusCo.className = 'sensor-status warning';
      el.statusCo.textContent = '● WARNING: Elevated CO Concentration';
    } else {
      el.cardCo.className = 'metric-card';
      el.barCo.style.backgroundColor = '#10b981';
      el.statusCo.className = 'sensor-status nominal';
      el.statusCo.textContent = '● Safe Exposure Zone';
    }

    // 2. Sound Level Check (OSHA 85 dBA action, 90 dBA mandatory protection)
    if (m.sound_dba >= 90.0) {
      el.cardSound.className = 'metric-card danger';
      el.statusSound.className = 'sensor-status danger';
      el.statusSound.textContent = '● CRITICAL: Ear Defense Required';
    } else if (m.sound_dba >= 85.0) {
      el.cardSound.className = 'metric-card warning';
      el.statusSound.className = 'sensor-status warning';
      el.statusSound.textContent = '● WARNING: Exceeds 85 dBA Action Level';
    } else {
      el.cardSound.className = 'metric-card';
      el.statusSound.className = 'sensor-status nominal';
      el.statusSound.textContent = '● Safe Ambient Noise';
    }

    // System Health Dot
    if (hasCriticalAlarm) {
      el.systemStatusDot.className = 'status-indicator danger';
      el.systemStatusText.textContent = 'HAZARD ALARM ACTIVE';
    } else if (!state.alarmActive) {
      el.systemStatusDot.className = 'status-indicator nominal';
      el.systemStatusText.textContent = 'SYSTEM NOMINAL';
    }

    // Append to Event Log
    appendEventLog(m, hasCriticalAlarm);
  }

  function triggerAlarm(title, desc) {
    state.alarmActive = true;
    el.alarmTitle.textContent = title;
    el.alarmDesc.textContent = desc;
    el.alarmBanner.classList.remove('hidden');
  }

  window.dismissAlarm = function() {
    state.alarmActive = false;
    state.spikeActive = false;
    el.alarmBanner.classList.add('hidden');
    el.systemStatusDot.className = 'status-indicator nominal';
    el.systemStatusText.textContent = 'SYSTEM NOMINAL';
  };

  function appendEventLog(m, isDanger) {
    state.eventCount++;
    el.logCount.textContent = `${state.eventCount} events`;

    const logEntry = document.createElement('div');
    const timeStr = new Date().toLocaleTimeString();
    
    if (isDanger) {
      logEntry.className = 'log-entry danger';
      logEntry.innerHTML = `<span class="log-time">[${timeStr}]</span> <span class="log-msg">🚨 OSHA HAZARD DISPATCH: CO ${m.co_ppm} ppm, Noise ${m.sound_dba} dBA. SNS alert pushed!</span>`;
    } else {
      logEntry.className = 'log-entry nominal';
      logEntry.innerHTML = `<span class="log-time">[${timeStr}]</span> <span class="log-msg">AWS Ingest OK: T:${m.temperature_c}°C, eCO2:${m.eco2_ppm} ppm, TVOC:${m.tvoc_ppb} ppb</span>`;
    }

    el.eventLogStream.prepend(logEntry);
    if (el.eventLogStream.children.length > 40) {
      el.eventLogStream.removeChild(el.eventLogStream.lastChild);
    }
  }

  // ── 5. INDUSTRIAL HARDWARE EMULATOR ───────────────────────
  function generateSimulatedPacket() {
    // If spike active, simulate toxic hazard event
    const isSpike = state.spikeActive;
    
    const co = isSpike 
      ? 54.2 + (Math.random() * 8.0) 
      : 1.2 + (Math.random() * 1.4);

    const co2 = isSpike 
      ? 2650 + Math.floor(Math.random() * 200) 
      : 580 + Math.floor(Math.random() * 140);

    const tvoc = isSpike 
      ? 640 + Math.floor(Math.random() * 90) 
      : 70 + Math.floor(Math.random() * 35);

    const sound = isSpike 
      ? 91.5 + (Math.random() * 4.0) 
      : 66.0 + (Math.random() * 7.5);

    const temp = 23.5 + (Math.random() * 0.8);
    const hum = 46.5 + (Math.random() * 2.0);

    return {
      station_id: state.stationId,
      facility_id: state.facilityId,
      timestamp: Math.floor(Date.now() / 1000),
      metrics: {
        co_ppm: parseFloat(co.toFixed(2)),
        eco2_ppm: co2,
        tvoc_ppb: tvoc,
        sound_dba: parseFloat(sound.toFixed(1)),
        temperature_c: parseFloat(temp.toFixed(1)),
        humidity_rh: parseFloat(hum.toFixed(1))
      },
      status: {
        alarm_active: isSpike,
        sht35_healthy: true,
        ccs811_healthy: true
      }
    };
  }

  function startSimulation() {
    if (state.simulationInterval) clearInterval(state.simulationInterval);
    state.simulationInterval = setInterval(() => {
      const packet = generateSimulatedPacket();
      processIncomingTelemetry(packet);
    }, state.updateRateMs);
  }

  window.toggleSimulation = function() {
    state.isSimulating = !state.isSimulating;
    if (state.isSimulating) {
      el.toggleSimBtn.classList.add('active');
      startSimulation();
    } else {
      el.toggleSimBtn.classList.remove('active');
      clearInterval(state.simulationInterval);
    }
  };

  window.injectGasSpike = function() {
    state.spikeActive = true;
    const packet = generateSimulatedPacket();
    processIncomingTelemetry(packet);
  };

  window.resetTelemetryBuffer = function() {
    state.labels.length = 0;
    state.coSeries.length = 0;
    state.co2Series.length = 0;
    state.tvocSeries.length = 0;
    state.soundSeries.length = 0;
    gasChart.update();
    soundChart.update();
    window.dismissAlarm();
  };

  // ── 6. AWS MODAL & LIVE FETCH ─────────────────────────────
  window.openAwsModal = function() {
    el.awsApiUrl.value = state.awsEndpoint;
    el.awsModal.classList.remove('hidden');
  };

  window.closeAwsModal = function() {
    el.awsModal.classList.add('hidden');
  };

  window.saveAwsConfig = function() {
    state.awsEndpoint = el.awsApiUrl.value.trim();
    localStorage.setItem('scada_aws_endpoint', state.awsEndpoint);
    closeAwsModal();

    if (state.awsEndpoint) {
      el.toggleAwsBtn.classList.add('active');
      el.toggleSimBtn.classList.remove('active');
      state.isSimulating = false;
      clearInterval(state.simulationInterval);
      pollAwsEndpoint();
    }
  };

  async function pollAwsEndpoint() {
    if (!state.awsEndpoint) return;
    try {
      const res = await fetch(state.awsEndpoint);
      if (res.ok) {
        const data = await res.json();
        processIncomingTelemetry(data);
      }
    } catch (err) {
      console.warn('[AWS POLL ERROR]', err);
    }
    setTimeout(pollAwsEndpoint, state.updateRateMs);
  }

  // ── 7. INITIAL BOOTSTRAP ──────────────────────────────────
  // Preload with 15 baseline points for instant visual delight
  for (let i = 14; i >= 0; i--) {
    const pastTime = new Date(Date.now() - (i * 3000));
    const timeStr = pastTime.toLocaleTimeString();
    state.labels.push(timeStr);
    state.coSeries.push(parseFloat((1.4 + Math.random() * 0.8).toFixed(1)));
    state.co2Series.push(620 + Math.floor(Math.random() * 80));
    state.tvocSeries.push(78 + Math.floor(Math.random() * 25));
    state.soundSeries.push(parseFloat((67.0 + Math.random() * 5.0).toFixed(1)));
  }
  gasChart.update();
  soundChart.update();

  // Run Real-Time Loop
  startSimulation();
});
