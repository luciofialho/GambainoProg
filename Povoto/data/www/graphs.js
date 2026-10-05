/* Browser charts. The CSV remains the single source of observations. */
(() => {
  'use strict';

  const status = document.getElementById('status');
  const chartHost = document.getElementById('chart');
  const chartScroll = document.getElementById('chart-scroll');
  const pan = document.getElementById('pan');
  const zoomRange = document.getElementById('zoom-range');
  const resetZoom = document.getElementById('reset-zoom');
  const chartTitle = document.getElementById('chart-title');
  const graphButtons = document.querySelectorAll('.graph-nav button[data-graph]');
  let chart = null;
  let historyData = null;
  let transitionBands = [];
  const GRAPHS = ['evolution', 'temperature', 'pressure', 'attenuation'];
  // /graphs/#temperature opens that tab (used by the /dashboard touch zones).
  let activeGraph = GRAPHS.includes(location.hash.slice(1)) ? location.hash.slice(1) : 'evolution';
  let originalGravity = null;
  let metaLoaded = false;
  let allMin = 0;
  let allMax = 0;

  // Povoto's epoch already has its configured local offset. UTC formatting here
  // displays those stored wall-clock numbers without applying the browser's
  // timezone a second time.
  function dateLabel(epoch, withYear = false) {
    const iso = new Date(epoch * 1000).toISOString();
    return withYear ? iso.slice(0, 16).replace('T', ' ') : iso.slice(5, 16).replace('T', ' ');
  }

  function number(cell) {
    if (cell === '') return null;
    const value = Number(cell.replace(',', '.'));
    return Number.isFinite(value) ? value : null;
  }

  function readCsv(csv) {
    const lines = csv.trimEnd().split(/\r?\n/);
    if (!lines.length || !lines[0].startsWith('epoch_local;')) {
      throw new Error('CSV header not recognized.');
    }
    const columns = lines[0].split(';');
    const field = name => columns.indexOf(name);
    const indexes = ['SG', 'TempC', 'TempSetpointC', 'PressureBar',
      'PressureSetpointBar', 'gCO2_L_d', 'ABV_percent'].map(field);
    if (indexes.some(index => index < 0)) throw new Error('CSV is missing a required series.');

    const flagsIndex = field('flags'); // optional in older exports

    // uPlot requires ascending, unique x-values. The last observation wins if
    // an older history contains repeated timestamps.
    const byEpoch = new Map();
    const transitionEpochs = new Set();
    for (let i = 1; i < lines.length; i++) {
      if (!lines[i]) continue;
      const cells = lines[i].split(';');
      const epoch = Number(cells[0]);
      if (!Number.isFinite(epoch) || epoch <= 0) continue;
      byEpoch.set(epoch, indexes.map(index => number(cells[index] || '')));
      const flags = flagsIndex < 0 ? 0 : Number(cells[flagsIndex] || 0);
      if (flags & TRANSITION_FLAG) transitionEpochs.add(epoch);
      else transitionEpochs.delete(epoch);
    }
    const epochs = Array.from(byEpoch.keys()).sort((a, b) => a - b);
    const data = [epochs, [], [], [], [], [], [], []];
    for (const epoch of epochs) {
      const values = byEpoch.get(epoch);
      for (let series = 0; series < indexes.length; series++) data[series + 1].push(values[series]);
    }
    return {data, bands: transitionRanges(epochs, transitionEpochs),
      imported: lines.length - 1, unique: epochs.length};
  }

  // gCO2/L/d transition (flag 2): the rate is not recorded, so the line is
  // interpolated across it. Each band runs from its first flagged point to the
  // next point, or one sample interval past the last flagged point when the
  // next one is missing or far away (device off).
  const TRANSITION_FLAG = 2;
  const SAMPLE_SECONDS = 15 * 60;
  function transitionRanges(epochs, flagged) {
    const bands = [];
    let start = null;
    for (let i = 0; i < epochs.length; i++) {
      const epoch = epochs[i];
      if (flagged.has(epoch)) {
        if (start === null) start = epoch;
        const next = epochs[i + 1];
        const nextFlagged = next !== undefined && flagged.has(next);
        const near = next !== undefined && next - epoch <= 2 * SAMPLE_SECONDS;
        if (!nextFlagged || !near) {
          bands.push([start, near ? next : epoch + SAMPLE_SECONDS]);
          start = null;
        }
      }
    }
    return bands;
  }

  // Shades the transition bands behind the series (Evolution graph only).
  function drawTransitionBands(u) {
    if (!transitionBands.length) return;
    const {left, top, width, height} = u.bbox;
    const ctx = u.ctx;
    ctx.save();
    ctx.beginPath();
    ctx.rect(left, top, width, height);
    ctx.clip();
    ctx.fillStyle = 'rgba(174, 185, 198, 0.13)';
    ctx.font = `${Math.round(11 * devicePixelRatio)}px sans-serif`;
    ctx.textBaseline = 'top';
    for (const [start, end] of transitionBands) {
      const x0 = u.valToPos(start, 'x', true);
      const x1 = u.valToPos(end, 'x', true);
      if (x1 < left || x0 > left + width) continue;
      ctx.fillRect(x0, top, x1 - x0, height);
      const label = 'transition';
      if (x1 - x0 > ctx.measureText(label).width + 8 * devicePixelRatio) {
        ctx.fillStyle = 'rgba(174, 185, 198, 0.55)';
        ctx.fillText(label, x0 + 4 * devicePixelRatio, top + 4 * devicePixelRatio);
        ctx.fillStyle = 'rgba(174, 185, 198, 0.13)';
      }
    }
    ctx.restore();
  }

  function axis(scale, side, color, decimals) {
    return {
      scale, side, stroke: color, size: 50,
      grid: {show: false},
      ticks: {stroke: '#4b5562'},
      values: (_u, ticks) => ticks.map(value => value.toFixed(decimals).replace('.', ',')),
    };
  }

  function updatePan() {
    if (!chart) return;
    const x = chart.scales.x;
    const span = x.max - x.min;
    const available = allMax - allMin - span;
    pan.disabled = available <= 0;
    pan.value = available <= 0 ? '1000' : String(Math.round(1000 * (x.min - allMin) / available));
  }

  function showRange(start, end) {
    if (!chart) return;
    chart.setScale('x', {min: start, max: end});
    updatePan();
  }

  function showAll() {
    zoomRange.value = 'all';
    showRange(allMin, allMax);
  }

  function draw(mode) {
    const previousRange = chart ? {min: chart.scales.x.min, max: chart.scales.x.max} : null;
    if (chart) {
      chart.destroy();
      chartHost.replaceChildren();
      chart = null;
    }
    allMin = historyData[0][0];
    allMax = historyData[0][historyData[0].length - 1];
    const temperatureOnly = mode === 'temperature';
    const pressureOnly = mode === 'pressure';
    const attenuationOnly = mode === 'attenuation';
    const data = temperatureOnly
      ? [historyData[0], historyData[2], historyData[3]]
      : pressureOnly
        ? [historyData[0], historyData[4], historyData[5]]
        : attenuationOnly
          ? [historyData[0], historyData[1],
              historyData[0].map(() => originalGravity), historyData[7]]
          : [historyData[0], historyData[1], historyData[2], historyData[4], historyData[6]];
    const xAxis = {stroke: '#aeb9c6', grid: {stroke: '#39414b'},
      values: (_u, ticks) => ticks.map(value => dateLabel(value))};
    const dateSeries = {label: ' ',
      value: (_u, value) => value == null ? '' : dateLabel(value, true)};
    const measuredTemp = {label: 'Temperature (°C)', scale: 'temp',
      stroke: '#7594ed', width: 2, spanGaps: true,
      value: (_u, value) => value == null ? '—' : value.toFixed(2).replace('.', ',')};
    const measuredPressure = {label: 'Pressure (bar)', scale: 'pressure',
      stroke: '#ff9657', width: 2, spanGaps: true,
      value: (_u, value) => value == null ? '—' : value.toFixed(3).replace('.', ',')};
    const axes = temperatureOnly
      ? [xAxis, axis('temp', 3, '#7594ed', 1)]
      : pressureOnly
        ? [xAxis, axis('pressure', 3, '#ff9657', 2)]
        : attenuationOnly
          ? [xAxis, axis('sg', 3, '#f06565', 3), axis('abv', 1, '#88c779', 1)]
        : [xAxis, axis('sg', 3, '#f06565', 3), axis('temp', 1, '#7594ed', 1),
            axis('pressure', 1, '#ff9657', 2), axis('rate', 3, '#88c779', 1)];
    const series = temperatureOnly
      ? [dateSeries, measuredTemp,
          {label: 'Target temperature (°C)', scale: 'temp', stroke: '#ffbd69',
            width: 2, dash: [7, 4], spanGaps: true,
            value: (_u, value) => value == null ? '—' : value.toFixed(2).replace('.', ',')}]
      : pressureOnly
        ? [dateSeries, measuredPressure,
            {label: 'Target pressure (bar)', scale: 'pressure', stroke: '#ffbd69',
              width: 2, dash: [7, 4], spanGaps: true,
              value: (_u, value) => value == null ? '—' : value.toFixed(3).replace('.', ',')}]
        : attenuationOnly
          ? [dateSeries,
              {label: 'SG', scale: 'sg', stroke: '#f06565', width: 2, spanGaps: true,
                value: (_u, value) => value == null ? '—' : value.toFixed(4).replace('.', ',')},
              {label: 'OG', scale: 'sg', stroke: '#ffbd69', width: 2,
                dash: [3, 5], spanGaps: true,
                value: (_u, value) => value == null ? '—' : value.toFixed(4).replace('.', ',')},
              {label: 'ABV (%)', scale: 'abv', stroke: '#88c779', width: 2,
                spanGaps: true,
                value: (_u, value) => value == null ? '—' : value.toFixed(2).replace('.', ',')}]
        : [dateSeries,
            {label: 'SG', scale: 'sg', stroke: '#f06565', width: 2, spanGaps: true,
              value: (_u, value) => value == null ? '—' : value.toFixed(4).replace('.', ',')},
            measuredTemp,
            measuredPressure,
            {label: 'CO2 evolution (gCO2/L/d)', scale: 'rate', stroke: '#88c779',
              width: 2, spanGaps: true,
              value: (_u, value) => value == null ? '—' : value.toFixed(3).replace('.', ',')}];
    const width = Math.max(850, chartScroll.clientWidth - 2);
    chart = new uPlot({
      width, height: 470,
      legend: {show: true, live: true},
      cursor: {drag: {x: true, y: false}},
      scales: {x: {time: false}},
      axes,
      series,
      hooks: {setScale: [updatePan],
        drawClear: mode === 'evolution' ? [drawTransitionBands] : []},
    }, data, chartHost);
    if (previousRange) chart.setScale('x', previousRange);
    updatePan();
  }

  function selectGraph(mode) {
    if (!historyData || !GRAPHS.includes(mode)) return;
    activeGraph = mode;
    if (location.protocol !== 'file:') history.replaceState(null, '', `#${mode}`);
    chartTitle.textContent = mode === 'temperature' ? 'Temperature'
      : mode === 'pressure' ? 'Pressure'
        : mode === 'attenuation' ? 'Attenuation' : 'Evolution';
    for (const button of graphButtons) {
      const selected = button.dataset.graph === mode;
      button.classList.toggle('selected', selected);
      if (selected) button.setAttribute('aria-current', 'page');
      else button.removeAttribute('aria-current');
    }
    draw(mode);
    updateOgStatus();
  }

  function updateOgStatus() {
    if (activeGraph === 'attenuation' && originalGravity === null) {
      status.textContent = metaLoaded ? 'OG unavailable from the device.' : 'Loading OG...';
      status.hidden = false;
      status.classList.toggle('error', metaLoaded);
    } else {
      status.textContent = '';
      status.hidden = true;
      status.classList.remove('error');
    }
  }

  for (const button of graphButtons) {
    button.addEventListener('click', () => selectGraph(button.dataset.graph));
  }

  zoomRange.addEventListener('change', () => {
    if (!chart) return;
    if (zoomRange.value === 'all') return showAll();
    const seconds = Number(zoomRange.value) * 86400;
    showRange(Math.max(allMin, allMax - seconds), allMax);
  });
  resetZoom.addEventListener('click', showAll);
  pan.addEventListener('input', () => {
    if (!chart) return;
    const span = chart.scales.x.max - chart.scales.x.min;
    const available = Math.max(0, allMax - allMin - span);
    const start = allMin + available * Number(pan.value) / 1000;
    showRange(start, start + span);
  });
  window.addEventListener('resize', () => {
    if (chart) chart.setSize({width: Math.max(850, chartScroll.clientWidth - 2), height: 470});
  });

  if (typeof uPlot !== 'function') {
    status.textContent = 'The local uPlot library is unavailable.';
    status.classList.add('error');
    return;
  }
  function loadCsv(csv) {
    try {
      const result = readCsv(csv);
      if (result.unique < 2) {
        status.textContent = 'At least two observations are needed to draw the chart.';
        status.hidden = false;
        return;
      }
      historyData = result.data;
      transitionBands = result.bands;
      selectGraph(activeGraph);
    } catch (error) {
      status.textContent = `Could not load graph: ${error.message}`;
      status.classList.add('error');
      status.hidden = false;
    }
  }

  // Cloud only: readOnly (public link) hides the menu and the CSV export;
  // batches fills the batch selector. The device sends neither.
  function applyCloudMeta(meta) {
    if (meta.readOnly) {
      document.querySelector('.back').hidden = true;
      document.getElementById('download-csv').hidden = true;
    }
    if (!Array.isArray(meta.batches) || !meta.batches.length) return;
    const select = document.getElementById('batch');
    for (const batch of meta.batches) {
      const option = document.createElement('option');
      option.value = String(batch.batch);
      option.textContent = batch.label;
      option.selected = batch.batch === meta.batch;
      select.append(option);
    }
    select.hidden = false;
    select.addEventListener('change', () => {
      location.search = `?batch=${encodeURIComponent(select.value)}`;
    });
  }

  if (location.protocol === 'file:') {
    document.getElementById('download-csv').href = 'http://192.168.13.180/graphs/data.csv';
    document.querySelector('.back').hidden = true;
    status.textContent = 'Loading CSV from Povoto at 192.168.13.180...';
  }
  // The cloud passes the batch in the query (?batch=160); the device ignores it.
  const url = location.protocol === 'file:'
    ? 'http://192.168.13.180/graphs/data.csv' : `data.csv${location.search}`;
  const metaUrl = location.protocol === 'file:'
    ? 'http://192.168.13.180/graphs/meta.json' : `meta.json${location.search}`;
  if (location.protocol !== 'file:') document.getElementById('download-csv').href = url;
  fetch(metaUrl, {cache: 'no-store', mode: 'cors'})
    .then(response => {
      if (!response.ok) throw new Error(`OG request failed (${response.status}).`);
      return response.json();
    })
    .then(meta => {
      applyCloudMeta(meta);
      originalGravity = typeof meta.og === 'number' && Number.isFinite(meta.og) && meta.og > 0
        ? meta.og : null;
      metaLoaded = true;
      if (historyData && activeGraph === 'attenuation') selectGraph('attenuation');
    })
    .catch(() => {
      metaLoaded = true;
      if (historyData && activeGraph === 'attenuation') updateOgStatus();
    });
  fetch(url, {cache: 'no-store', mode: 'cors'})
    .then(response => {
      if (!response.ok) throw new Error(`CSV request failed (${response.status}).`);
      return response.text();
    })
    .then(loadCsv)
    .catch(error => {
      status.textContent = location.protocol === 'file:'
        ? `Could not load CSV from 192.168.13.180 (${error.message}). You can still use Export CSV.`
        : `Could not load graph: ${error.message}`;
      status.classList.add('error');
      status.hidden = false;
    });
})();
