<script setup>
import { ref, onMounted, onUnmounted } from "vue";
import uPlot from "../lib/uplot/dist/uPlot.esm.js";
import { useNotification } from "../composables/useNotification";
import { parse, calculateMetadata, msToHumanTime, formatTimestamp, formatUid } from "../lib/energymeter";
import {
  wheelZoomPlugin,
  touchZoomPlugin,
  peakAnnotationsPlugin,
  violationVisibilityPlugin,
  downloadImage,
} from "../lib/uplotPlugins";

const notyf = useNotification();
const chartContainer = ref(null);
const batteryChartContainer = ref(null);
const selectedFiles = ref([]);
const selectedFile = ref("");
const chartData = ref(null);
const result = ref(null);
const powerLimit = ref(parseInt(localStorage.getItem("power-limit")) || 80);

let uplot = null;
let batteryPlot = null;
let resizeObserver = null;

const metadata = ref({
  boot: "N/A",
  logs: "N/A",
  duration: "N/A",
  uid: "N/A",
  energy: "N/A",
  power: "N/A",
  voltage: "N/A",
  current: "N/A",
  battery_remaining: "N/A",
  violation: "N/A",
  startup: "N/A",
  v_cal: "N/A",
  c_cal: "N/A",
});
const alerts = ref({ violations: [], warnings: [], errors: [] });

function splitRange(dMin, dMax) {
  if (dMin === dMax) {
    dMin *= 0.85;
    dMax *= 1.15;
  } else {
    const r = dMax - dMin;
    dMin -= r * 0.05;
    dMax += r * 0.05;
  }
  const step = (dMax - dMin) / 9;

  return {
    min: dMin,
    max: dMax,
    splits: Array.from({ length: 11 }, (_, i) => dMin + i * step),
  };
}

function initChart() {
  if (!chartContainer.value) {
    return;
  }
  const axis = { HV: {}, A: {}, kW: {}, LV: {}, C: {} };
  const scales = {};
  for (const k of Object.keys(axis)) {
    scales[k] = {
      range: (u, dMin, dMax) => {
        if (dMin === null && dMax === null) return [null, null];
        axis[k] = splitRange(dMin, dMax);
        return [axis[k].min, axis[k].max];
      },
    };
  }
  const fmt = (v) => {
    if (!v) return "-";
    const d = new Date(v),
      p = (n) => String(n).padStart(2, "0");
    return `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}.${String(d.getMilliseconds()).padStart(3, "0")}`;
  };

  uplot = new uPlot(
    {
      width: chartContainer.value.clientWidth - 32,
      height: 500,
      ms: true,
      series: [
        { value: (_, v) => fmt(v) },
        {
          label: "HV",
          scale: "HV",
          stroke: "red",
          value: (_, v) => (v ?? "-") + "V",
        },
        {
          label: "HV Amp",
          scale: "A",
          stroke: "dodgerblue",
          value: (_, v) => (v ?? "-") + "A",
        },
        {
          label: "HV Power",
          scale: "kW",
          stroke: "mediumorchid",
          value: (_, v) => (v?.toFixed(1) ?? "-") + "kW",
        },
        {
          label: "LV",
          scale: "LV",
          stroke: "green",
          value: (_, v) => (v ?? "-") + "V",
          show: false,
        },
        {
          label: "Temp",
          scale: "C",
          stroke: "orange",
          value: (_, v) => (v ?? "-") + "°C",
          show: false,
        },
        {
          label: "100ms",
          scale: "kW",
          points: {
            show: true,
            size: 6,
            fill: "dimgray",
            stroke: "dimgray",
            width: 2,
          },
        },
        {
          label: "500ms",
          scale: "kW",
          points: {
            show: true,
            size: 6,
            fill: "darkgray",
            stroke: "darkgray",
            width: 2,
          },
        },
      ],
      axes: [
        {
          stroke: "#999",
          values: (_, t) =>
            t.map((v) => {
              const d = new Date(v),
                p = (n) => String(n).padStart(2, "0");
              return `${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}\n${String(d.getMilliseconds()).padStart(3, "0")}`;
            }),
        },
        {
          scale: "A",
          stroke: "dodgerblue",
          values: (_, t) => t.map((v) => v.toFixed(1) + "A"),
          splits: () => axis.A.splits,
          size: 55,
        },
        {
          scale: "HV",
          stroke: "red",
          values: (_, t) => t.map((v) => v.toFixed(1) + "V"),
          splits: () => axis.HV.splits,
          size: 55,
        },
        {
          scale: "kW",
          stroke: "mediumorchid",
          values: (_, t) => t.map((v) => v.toFixed(1) + "kW"),
          side: 1,
          splits: () => axis.kW.splits,
          size: 60,
        },
        {
          scale: "LV",
          stroke: "green",
          values: (_, t) => t.map((v) => v.toFixed(1) + "V"),
          side: 1,
          splits: () => axis.LV.splits,
          size: 55,
        },
        {
          scale: "C",
          stroke: "orange",
          values: (_, t) => t.map((v) => v.toFixed(1) + "°C"),
          side: 1,
          splits: () => axis.C.splits,
          size: 55,
        },
      ],
      scales,
      plugins: [
        touchZoomPlugin(),
        wheelZoomPlugin({ factor: 0.75 }),
        peakAnnotationsPlugin(result),
        violationVisibilityPlugin(),
        lapStartPlugin(),
      ],
    },
    null,
    chartContainer.value,
  );
}

function initBatteryChart() {
  if (!batteryChartContainer.value) {
    return;
  }
  batteryPlot = new uPlot(
    {
      width: batteryChartContainer.value.clientWidth - 32,
      height: 250,
      ms: true,
      plugins: [
        lapStartPlugin(),
      ],
      series: [
        {},
        {
          label: "Battery %",
          stroke: "gold",
          value: (_, v) => (v?.toFixed(1) ?? "-") + "%",
        },
      ],
      axes: [
        {},
        {
          values: (_, t) => t.map((v) => v.toFixed(0) + "%"),
        },
      ],
    },
    [[], []],
    batteryChartContainer.value,
  );
}

function readLogFile(file) {
  return new Promise((resolve, reject) => {
    const ext = file.name.split(".").pop().toLowerCase();
    const reader = new FileReader();

    if (ext === "log") {
      reader.readAsArrayBuffer(file);

      reader.onload = (e) => {
        try {
          resolve(parse(new Uint8Array(e.target.result)));
        } catch (err) {
          reject(err);
        }
      };

      reader.onerror = reject;
    }
    else if (ext === "json") {
      reader.readAsText(file);

      reader.onload = (e) => {
        try {
          resolve(JSON.parse(e.target.result));
        } catch (err) {
          reject(err);
        }
      };

      reader.onerror = reject;
    }
    else if (ext === "csv") {
      reader.readAsText(file);

      reader.onload = (e) => {
        try {
          const lines = e.target.result.split("\n");
          const idx = lines.indexOf("original json data");

          if (idx === -1 || !lines[idx + 1]) {
            throw new Error("Cannot restore JSON from CSV");
          }

          resolve(JSON.parse(lines[idx + 1]));
        } catch (err) {
          reject(err);
        }
      };

      reader.onerror = reject;
    }
    else {
      reject(new Error("Unsupported file format"));
    }
  });
}

async function handleFileSelect(e) {
  const files = [...e.target.files];

  if (!files.length) return;

  selectedFiles.value = files;
  selectedFile.value = files[0].name;

  alerts.value = {
    violations: [],
    warnings: [],
    errors: [],
  };

  try {
    const results = await Promise.all(
      files.map(async (file) => ({
        fileName: file.name,
        result: await readLogFile(file),
      }))
    );
    results.sort((a, b) =>  a.result.header.datetime - b.result.header.datetime);

    const combined = combineResults(results);
    console.log(combined.raceBoundaries);
    result.value = combined;

    setChartData( calculateMetadata(result.value, powerLimit.value) );

    notyf.success(`${results.length} files loaded`);
  }
  catch (err) {
    alerts.value.errors = [err.message];
    notyf.error(err.message);
  }
}

function setChartData(data) {
  chartData.value = data;

  uplot?.setData(data.processed);
  batteryPlot?.setData([
    data.processed[0],
    data.processed[8],
  ]);
  displayMetadata(data);
}

function combineResults(runs) {
  if (!runs.length) return null;

  const combined = structuredClone(runs[0].result);

  combined.data = [];
  combined.raceBoundaries = [];

  for (let i = 0; i < runs.length; i++) {
    const run = runs[i];
    const r = run.result;

    combined.raceBoundaries.push({
        race: i + 1,
        file: run.fileName,
        timestamp: r.header.datetime,
        startIndex: combined.data.length,
    });

    combined.data.push(...r.data);
  }

  return combined;
}




function displayMetadata(logs) {
  metadata.value.boot = formatTimestamp(logs.header.datetime);
  metadata.value.logs = `${logs.data.length.toLocaleString()} (${logs.ok.toLocaleString()} valid / ${logs.error.length.toLocaleString()} error)`;
  const dur = logs.data[logs.data.length - 1].timestamp - logs.data[0].timestamp;
  metadata.value.duration = `${msToHumanTime(dur)} (${dur.toLocaleString()} ms)`;
  metadata.value.uid = formatUid(logs.header.uid);
  metadata.value.energy = `${logs.power.toFixed(2)} kWh`;
  metadata.value.power = `${logs.max_power.toFixed(1)} kW`;
  metadata.value.voltage = `${logs.max_voltage.toFixed(1)} V`;
  metadata.value.current = `${logs.max_current.toFixed(1)} A`;
  metadata.value.battery_remaining = `${logs.battery_remaining.toFixed(1)} %`;
  alerts.value.warnings =
    logs.header.datetime > Number(new Date(2099, 0))
      ? ["Invalid RTC date detected. Sync the clock in the Device configuration tab."]
      : [];
  alerts.value.errors = logs.error;
  alerts.value.violations = logs.violation
    .slice(0, 5)
    .map((x) => `#${x.index}: ${x.type} (${x.value.toFixed(2)} kW at ${formatTimestamp(x.timestamp).split(" ")[1]})`);
  if (logs.violation.length > 5) alerts.value.violations.push(`...and ${logs.violation.length - 5} more violations.`);

  metadata.value.violation = logs.violation.length;
  metadata.value.startup = `${logs.header.startup} ms`;
  if (logs.header.v_cal === 0.002 && logs.header.c_cal === 0) {
    metadata.value.v_cal = "Not Supported";
    metadata.value.c_cal = "Not Supported";
  } else {
    metadata.value.v_cal = `${logs.header.v_cal.toFixed(2)} V`;
    metadata.value.c_cal = `${logs.header.c_cal.toFixed(2)} A`;
  }
}

function reverseCurrent() {
  if (!result.value) return;
  result.value.data.forEach((l) => {
    if (l.type === "LOG_TYPE_RECORD") l.record.hv_current = -l.record.hv_current;
  });
  setChartData(calculateMetadata(result.value, powerLimit.value));
}
function togglePowerLimit() {
  powerLimit.value = powerLimit.value === 80 ? 10 : 80;
  localStorage.setItem("power-limit", powerLimit.value);
  if (result.value) setChartData(calculateMetadata(result.value, powerLimit.value));
}
function exportJson() {
  if (!result.value) return;
  download(JSON.stringify(result.value, null, 2), selectedFile.value.replace(/\.[^/.]+$/, "") + ".json", "text/plain");
  notyf.success("JSON exported");
}
function exportCsv() {
  if (!result.value || !uplot) return;
  const labels = ["timestamp", "hv_voltage", "hv_current", "hv_power", "lv_voltage", "temperature"];
  let csv = labels.join(",") + "\n";
  for (let i = 0; i < uplot._data[0].length; i++) csv += uplot._data.map((d) => d[i]).join(",") + "\n";
  csv += `\noriginal json data\n${JSON.stringify(result.value)}`;
  download(csv, selectedFile.value.replace(/\.[^/.]+$/, "") + ".csv", "text/plain");
  notyf.success("CSV exported");
}
async function exportGraph() {
  if (!result.value || !uplot) return;
  const filename = selectedFile.value ? selectedFile.value.replace(/\.[^/.]+$/, "") : "graph";
  await downloadImage(uplot, filename);
  notyf.success("Graph image exported");
}
function download(content, name, type) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([content], { type }));
  a.download = name;
  a.click();
}

function lapStartPlugin() {
  let elements = [];

  function clear() {
    elements.forEach(el => el.remove());
    elements = [];
  }

  function place(u) {
    clear();


    if (!result.value?.raceBoundaries?.length) return;
    if (!chartData.value) return;

    for (const lap of result.value.raceBoundaries) {

      const xValue = u.data[0][lap.startIndex];

      // 화면 밖이면 안 그림
      if (xValue < u.scales.x.min || xValue > u.scales.x.max)
        continue;

      const x = u.valToPos(xValue, "x");

      // 선
      const line = document.createElement("div");
      line.style.position = "absolute";
      line.style.left = `${x}px`;
      line.style.top = `${u.bbox.top}px`;
      line.style.width = "1px";
      line.style.height = `${u.over.clientHeight}px`;
      line.style.background = "#00bcd4";
      line.style.pointerEvents = "none";

      // 글자
      const label = document.createElement("div");
      label.textContent = `Lap ${lap.race}`;
      label.style.position = "absolute";
      label.style.left = `${x + 4}px`;
      label.style.top = `${u.bbox.top + 5}px`;
      label.style.color = "#00bcd4";
      label.style.fontSize = "12px";
      label.style.fontWeight = "bold";
      label.style.pointerEvents = "none";

      u.over.appendChild(line);
      u.over.appendChild(label);

      elements.push(line, label);
    }
  }

  return {
    hooks: {
      ready: [
        (u) => place(u),
      ],
      setScale: [
        (u) => place(u),
      ],
    },
  };
}


function handleResize() {
  if (!uplot || !chartContainer.value) return;
  const width = chartContainer.value.clientWidth - 32;
  uplot.setSize({ width, height: 500 });
}

onMounted(() => {
  initChart();
  initBatteryChart();
  resizeObserver = new ResizeObserver(() => {
    requestAnimationFrame(() => handleResize());
  });
  if (chartContainer.value) {
    resizeObserver.observe(chartContainer.value);
  }
});
onUnmounted(() => {
  if (resizeObserver) {
    resizeObserver.disconnect();
  }
  uplot?.destroy();
});
</script>

<template>
  <div class="data-viewer">
    <div class="card">
      <div class="card-header">
        <h3><i class="fas fa-file-import"></i> File Management</h3>
      </div>
      <div class="card-body">
        <div class="file-info">
          <span class="label">FILE:</span>
          <span class="value">{{ selectedFile || "No file loaded" }}</span>
        </div>
        <div class="file-info">
          <span class="label">FILES:</span>
          <span class="value">{{ selectedFiles.length }}</span>
        </div>

        <div
          v-if="selectedFiles.length"
          class="selected-files"
        >
          <strong>Selected Files</strong>

          <div
            v-for="(file, index) in selectedFiles"
            :key="file.name"
          >
            {{ index + 1 }}. {{ file.name }}
          </div>
        </div>
        <div class="button-group">
          <label class="btn btn-success"
            ><i class="fas fa-file"></i>Select File<input
              type="file"
              accept=".log,.json,.csv"
              multiple
              @change="handleFileSelect"
              style="display: none"
          /></label>
          <button class="btn btn-ghost" :disabled="!result" @click="exportJson">
            <i class="fas fa-file-code"></i>Export JSON
          </button>
          <button class="btn btn-ghost" :disabled="!result" @click="exportCsv">
            <i class="fas fa-file-csv"></i>Export CSV
          </button>
        </div>
      </div>
    </div>

    <div class="card" style="animation-delay: 0.1s">
      <div class="card-header">
        <h3><i class="fas fa-chart-line"></i> Graph Viewer</h3>
      </div>
      <div class="card-body">
        <div v-if="alerts.violations.length" class="alert alert-danger">
          <div v-for="(v, i) in alerts.violations" :key="i">{{ v }}</div>
        </div>
        <div v-if="alerts.warnings.length" class="alert alert-warning">
          <div v-for="(w, i) in alerts.warnings" :key="i">{{ w }}</div>
        </div>
        <div v-if="alerts.errors.length" class="alert alert-danger">
          <div v-for="(e, i) in alerts.errors" :key="i">{{ e }}</div>
        </div>
        <div class="stats-grid">
          <div class="stats-card">
            <table class="stats-table"><tbody>
              <tr>
                <td>Boot</td>
                <td>{{ metadata.boot }}</td>
              </tr>
              <tr>
                <td>Logs</td>
                <td>{{ metadata.logs }}</td>
              </tr>
              <tr>
                <td>Duration</td>
                <td>{{ metadata.duration }}</td>
              </tr>
              <tr>
                <td>Device ID</td>
                <td>{{ metadata.uid }}</td>
              </tr>
            </tbody></table>
          </div>
          <div class="stats-card">
            <table class="stats-table"><tbody>
              <tr>
                <td>Total Energy</td>
                <td>{{ metadata.energy }}</td>
              </tr>
              <tr>
                <td>Peak Power</td>
                <td>{{ metadata.power }}</td>
              </tr>
              <tr>
                <td>Peak Voltage</td>
                <td>{{ metadata.voltage }}</td>
              </tr>
              <tr>
                <td>Peak Current</td>
                <td>{{ metadata.current }}</td>
              </tr>
              <tr>
                <td>Battery Remaining</td>
                <td>{{ metadata.battery_remaining }}</td>
              </tr>
            </tbody></table>
          </div>
          <div class="stats-card">
            <table class="stats-table"><tbody>
              <tr>
                <td>Violation</td>
                <td>{{ metadata.violation }}</td>
              </tr>
              <tr>
                <td>Startup Delay</td>
                <td>{{ metadata.startup }}</td>
              </tr>
              <tr>
                <td>Voltage Offset</td>
                <td>{{ metadata.v_cal }}</td>
              </tr>
              <tr>
                <td>Current Offset</td>
                <td>{{ metadata.c_cal }}</td>
              </tr>
            </tbody></table>
          </div>
        </div>
        <div ref="chartContainer" class="chart-container"></div>
        <h4 style="margin-top: 2rem; margin-bottom: 1rem;">
          Battery Remaining (%)
        </h4>
        <div ref="batteryChartContainer" class="chart-container"></div>
        <div class="chart-hint"><i class="fas fa-info-circle"></i> Drag or scroll to zoom, double click to reset.</div>
        <div class="button-group center">
          <button class="btn btn-warning" :disabled="!result" @click="togglePowerLimit">
            <i class="fas fa-car-battery"></i>Power Limit: {{ powerLimit }} kW
          </button>
          <button class="btn btn-ghost" :disabled="!result" @click="reverseCurrent">
            <i class="fas fa-arrow-rotate-left"></i>Reverse Current
          </button>
          <button class="btn btn-ghost" :disabled="!result" @click="exportGraph">
            <i class="fas fa-download"></i>Export Graph
          </button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.data-viewer {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.file-info {
  margin-bottom: 1rem;
  font-size: 0.875rem;
}

.file-info .label {
  color: var(--text-secondary);
  margin-right: 0.5rem;
}

.file-info .value {
  font-family: "JetBrains Mono", monospace;
  color: var(--text-primary);
}

.button-group {
  display: flex;
  gap: 0.75rem;
  flex-wrap: wrap;
}

.button-group.center {
  justify-content: center;
  margin-top: 1.5rem;
}

.stats-grid {
  display: grid;
  grid-template-columns: 1fr;
  gap: 1.5rem;
  margin-bottom: 1.5rem;
}

@media (min-width: 768px) {
  .stats-grid {
    grid-template-columns: 4fr 3fr 3fr;
  }
}

.stats-card {
  background: var(--bg-secondary);
  border-radius: 12px;
  padding: 1rem;
  border: 1px solid var(--border-color);
}

.chart-container {
  background: var(--bg-secondary);
  border-radius: 12px;
  padding: 1rem;
  border: 1px solid var(--border-color);
  overflow-x: auto;
}

.chart-hint {
  text-align: center;
  font-size: 0.8125rem;
  color: var(--text-tertiary);
  margin-top: 1rem;
}

.chart-hint i {
  margin-right: 0.5rem;
}
.selected-files {
  margin-top: 12px;
  padding: 12px;
  border: 1px solid var(--border-color);
  border-radius: 10px;
  background: var(--bg-secondary);
  max-height: 180px;
  overflow-y: auto;
}

.selected-files div {
  padding: 4px 0;
  font-family: "JetBrains Mono", monospace;
  font-size: 0.9rem;
}
</style>
