(() => {
  'use strict';
  const $ = (selector) => document.querySelector(selector);
  const palette = [
    ['#123b68', '#3e86ca', '#9acbff'],
    ['#3b245f', '#8351bd', '#d4b3ff'],
    ['#17483e', '#258b72', '#9debd3'],
    ['#55351d', '#ae7135', '#ffd09a'],
    ['#4b2834', '#a34f6e', '#ffb1c7'],
    ['#293e60', '#577eaf', '#c0d8ff'],
    ['#4c4822', '#96923d', '#f1e99c']
  ];
  const samples = [
    { pid: 'P1', arrival: 0, burst: 7, priority: 2, queue: 1 },
    { pid: 'P2', arrival: 2, burst: 4, priority: 1, queue: 0 },
    { pid: 'P3', arrival: 4, burst: 1, priority: 3, queue: 2 },
    { pid: 'P4', arrival: 5, burst: 4, priority: 2, queue: 1 }
  ];
  let processes = samples.map(p => ({ ...p }));
  let lastResult = null;

  function node(tag, className, text) {
    const el = document.createElement(tag);
    if (className) el.className = className;
    if (text !== undefined) el.textContent = String(text);
    return el;
  }

  function setStatus(message, isError = false) {
    const status = $('#cpu-sim-status');
    if (!status) return;
    status.textContent = message;
    status.className = isError ? 'muted-small status-error' : 'muted-small';
  }

  function renderProcessRows() {
    const tbody = $('#cpu-process-rows');
    if (!tbody) return;
    tbody.replaceChildren();
    processes.forEach((process, index) => {
      const row = node('tr');
      row.dataset.index = String(index);
      const fields = [
        ['pid', 'text', process.pid, 'cpu-cell-input cpu-cell-pid'],
        ['arrival', 'number', process.arrival, 'cpu-cell-input'],
        ['burst', 'number', process.burst, 'cpu-cell-input'],
        ['priority', 'number', process.priority, 'cpu-cell-input'],
        ['queue', 'number', process.queue ?? 1, 'cpu-cell-input']
      ];
      fields.forEach(([field, type, value, className]) => {
        const td = node('td');
        const input = node('input', className);
        input.type = type;
        input.value = value;
        input.dataset.field = field;
        input.setAttribute('aria-label', `${process.pid} ${field}`);
        if (type === 'number') {
          input.min = field === 'burst' ? '1' : '0';
          input.max = '1000000';
          input.step = '1';
        }
        td.appendChild(input);
        row.appendChild(td);
      });
      const action = node('td');
      const remove = node('button', 'cpu-remove-btn', 'Remove');
      remove.type = 'button';
      remove.dataset.removeProcess = 'true';
      remove.disabled = processes.length <= 1;
      remove.title = 'Remove this process';
      action.appendChild(remove);
      row.appendChild(action);
      tbody.appendChild(row);
    });
  }

  function readProcessRows() {
    const rows = [...$('#cpu-process-rows').querySelectorAll('tr')];
    return rows.map(row => {
      const value = field => row.querySelector(`[data-field="${field}"]`).value;
      return {
        pid: value('pid').trim(),
        arrival: Number(value('arrival')),
        burst: Number(value('burst')),
        priority: Number(value('priority')),
        queue: Number(value('queue'))
      };
    });
  }

  function format(value, digits = 2) {
    return Number(value).toFixed(digits).replace(/\.00$/, '');
  }

  function processColorIndex(pid) {
    let hash = 0;
    for (const char of String(pid)) hash = (hash * 31 + char.charCodeAt(0)) >>> 0;
    return hash % palette.length;
  }

  function renderStat(label, value) {
    const card = node('div', 'cpu-stat');
    card.append(node('span', '', label), node('strong', '', value));
    return card;
  }

  function renderGantt(result) {
    const shell = node('div', 'cpu-gantt-shell');
    shell.appendChild(node('div', 'cpu-gantt-title', 'Gantt Chart · CPU execution order'));
    const track = node('div', 'cpu-gantt-track');
    const timeTrack = node('div', 'cpu-gantt-times');
    result.segments.forEach((segment, index) => {
      const block = node('div', `cpu-gantt-segment${segment.pid === 'IDLE' ? ' idle' : ''}`);
      const colors = palette[processColorIndex(segment.pid)];
      block.style.setProperty('--grow', String(segment.duration));
      if (segment.pid !== 'IDLE') {
        block.style.setProperty('--seg-bg', colors[0]);
        block.style.setProperty('--seg-border', colors[1]);
        block.style.setProperty('--seg-text', colors[2]);
      }
      block.title = `${segment.pid}: ${segment.start} to ${segment.end} (${segment.duration} time units)`;
      block.append(node('strong', '', segment.pid), node('span', '', `${segment.start} – ${segment.end}`));
      track.appendChild(block);
      const tick = node('div', 'cpu-gantt-time', `${segment.start} → ${segment.end}`);
      tick.style.setProperty('--grow', String(segment.duration));
      timeTrack.appendChild(tick);
    });
    shell.append(track, timeTrack);
    return shell;
  }

  function renderDetailedTable(rows) {
    const wrap = node('div', 'cpu-result-table-wrap');
    const table = node('table', 'cpu-result-table');
    const thead = node('thead');
    const heading = node('tr');
    ['Process', 'AT', 'BT', 'Priority', 'Queue', 'CT', 'TAT', 'WT', 'RT'].forEach(label => heading.appendChild(node('th', '', label)));
    thead.appendChild(heading);
    const tbody = node('tbody');
    rows.forEach(row => {
      const tr = node('tr');
      [row.pid, row.arrival, row.burst, row.priority, row.queue, row.completion, row.turnaround, row.waiting, row.response]
        .forEach(value => tr.appendChild(node('td', '', value)));
      tbody.appendChild(tr);
    });
    table.append(thead, tbody);
    wrap.appendChild(table);
    return wrap;
  }

  function renderResult(result) {
    const root = $('#cpu-sim-result');
    root.replaceChildren();
    const heading = node('div', 'cpu-result-heading');
    const title = node('div');
    title.append(node('h3', '', `${result.algorithmName} · Result`), node('p', '', 'CT = Completion · TAT = CT − AT · WT = TAT − BT · RT = First Start − AT'));
    heading.appendChild(title);
    heading.appendChild(node('span', 'panel-tag', `${result.rows.length} PROCESSES`));
    root.appendChild(heading);

    const cards = node('div', 'cpu-stat-grid');
    cards.append(
      renderStat('Average Waiting', format(result.averages.waiting)),
      renderStat('Average Turnaround', format(result.averages.turnaround)),
      renderStat('Average Response', format(result.averages.response)),
      renderStat('CPU Utilization', `${format(result.cpuUtilization)}%`),
      renderStat('Throughput', `${format(result.throughput, 3)} proc/unit`),
      renderStat('CPU Idle Time', result.idleTime),
      renderStat('Process Switches', result.contextSwitches)
    );
    root.append(cards, renderGantt(result), renderDetailedTable(result.rows));
    root.appendChild(node('p', 'metric-help', 'Process switches count a change from one running process to another in this simulated schedule. It is not the same measurement as the actual Linux context-switch counter shown in the kernel experiment below. CPU utilization includes idle time from t = 0 until the final completion.'));
  }

  function renderComparison(results) {
    const root = $('#cpu-compare-result');
    root.replaceChildren();
    if (!results || !results.length) return;
    root.appendChild(node('h3', 'cpu-compare-title', 'Algorithm comparison · same process workload'));
    const bestWaiting = Math.min(...results.map(r => r.averages.waiting));
    const bestTurnaround = Math.min(...results.map(r => r.averages.turnaround));
    const bestResponse = Math.min(...results.map(r => r.averages.response));
    const wrap = node('div', 'cpu-result-table-wrap');
    const table = node('table', 'cpu-result-table');
    const thead = node('thead');
    const head = node('tr');
    ['Algorithm', 'Avg WT', 'Avg TAT', 'Avg RT', 'CPU Utilization', 'Throughput', 'Process Switches'].forEach(v => head.appendChild(node('th', '', v)));
    thead.appendChild(head);
    const tbody = node('tbody');
    results.forEach(result => {
      const tr = node('tr');
      const values = [
        result.algorithmName,
        format(result.averages.waiting),
        format(result.averages.turnaround),
        format(result.averages.response),
        `${format(result.cpuUtilization)}%`,
        format(result.throughput, 3),
        result.contextSwitches
      ];
      values.forEach((value, index) => {
        const cell = node('td', '', value);
        if ((index === 1 && result.averages.waiting === bestWaiting) ||
            (index === 2 && result.averages.turnaround === bestTurnaround) ||
            (index === 3 && result.averages.response === bestResponse)) cell.classList.add('cpu-best');
        tr.appendChild(cell);
      });
      tbody.appendChild(tr);
    });
    table.append(thead, tbody);
    wrap.appendChild(table);
    root.appendChild(wrap);
    root.appendChild(node('p', 'metric-help', 'Green values mark the lowest average for that metric in this workload. No single algorithm is best for every workload; results change with arrival, burst, priority and quantum values.'));
  }

  function runSelected() {
    try {
      processes = readProcessRows();
      const algorithm = $('#cpu-algorithm').value;
      const quantum = Number($('#cpu-quantum').value);
      const result = MiniDBCpuScheduling.run(processes, algorithm, quantum);
      lastResult = result;
      renderResult(result);
      $('#cpu-compare-result').replaceChildren();
      setStatus(`${result.algorithmName} completed · ${result.segments.length} Gantt segment(s)`);
    } catch (error) {
      $('#cpu-sim-result').replaceChildren(node('div', 'notice warning-note', error.message));
      setStatus(error.message, true);
    }
  }

  function compareAll() {
    try {
      processes = readProcessRows();
      const quantum = Number($('#cpu-quantum').value);
      const results = MiniDBCpuScheduling.compare(processes, quantum);
      renderComparison(results);
      const selected = results.find(r => r.algorithm === $('#cpu-algorithm').value) || results[0];
      lastResult = selected;
      renderResult(selected);
      setStatus(`Compared all ${results.length} algorithms · best metrics highlighted`);
    } catch (error) {
      $('#cpu-compare-result').replaceChildren(node('div', 'notice warning-note', error.message));
      setStatus(error.message, true);
    }
  }

  function updateQuantumVisibility() {
    const isRoundRobin = $('#cpu-algorithm').value === 'rr';
    $('#cpu-quantum-wrap').classList.toggle('cpu-hidden', !isRoundRobin);
  }

  document.addEventListener('DOMContentLoaded', () => {
    if (!$('#cpu-process-rows')) return;
    renderProcessRows();
    updateQuantumVisibility();
    $('#cpu-algorithm').addEventListener('change', updateQuantumVisibility);
    $('#cpu-run').addEventListener('click', runSelected);
    $('#cpu-compare').addEventListener('click', compareAll);
    $('#cpu-load-example').addEventListener('click', () => {
      processes = samples.map(p => ({ ...p }));
      renderProcessRows();
      $('#cpu-sim-result').replaceChildren(node('div', 'cpu-empty', 'Sample workload loaded. Select an algorithm and run it.'));
      $('#cpu-compare-result').replaceChildren();
      setStatus('Example workload loaded · 4 processes');
    });
    $('#cpu-add-process').addEventListener('click', () => {
      try { processes = readProcessRows(); } catch (_) { /* retain last valid state while editing */ }
      if (processes.length >= 12) {
        setStatus('Maximum 12 processes are supported in this teaching simulator.', true);
        return;
      }
      let next = 1;
      const existing = new Set(processes.map(p => String(p.pid)));
      while (existing.has(`P${next}`)) next += 1;
      processes.push({ pid: `P${next}`, arrival: 0, burst: 1, priority: next, queue: 1 });
      renderProcessRows();
      setStatus(`Added P${next} · ${processes.length} processes`);
    });
    $('#cpu-process-rows').addEventListener('click', event => {
      const remove = event.target.closest('[data-remove-process]');
      if (!remove || processes.length <= 1) return;
      const row = remove.closest('tr');
      const index = Number(row.dataset.index);
      try { processes = readProcessRows(); } catch (_) { /* remove still works with last valid values */ }
      processes.splice(index, 1);
      renderProcessRows();
      setStatus(`Process removed · ${processes.length} remaining`);
    });
    // Keep the example ready, but don't pretend a schedule has run until the user requests it.
  });

  // Expose last result for lightweight manual inspection in browser devtools.
  window.getMiniDBCpuSchedulingResult = () => lastResult;
})();
