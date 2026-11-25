(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  if (root) root.MiniDBCpuScheduling = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const ALGORITHMS = [
    { id: 'fcfs', name: 'FCFS', detail: 'First Come First Served · Non-preemptive' },
    { id: 'sjf', name: 'SJF', detail: 'Shortest Job First · Non-preemptive' },
    { id: 'srtf', name: 'SRTF', detail: 'Shortest Remaining Time First · Preemptive' },
    { id: 'priority-np', name: 'Priority (Non-preemptive)', detail: 'Lower priority number = higher priority' },
    { id: 'priority-p', name: 'Priority (Preemptive)', detail: 'A higher-priority arrival can preempt the running process' },
    { id: 'rr', name: 'Round Robin', detail: 'Time quantum based · Preemptive' },
    { id: 'hrrn', name: 'HRRN', detail: 'Highest Response Ratio Next · Non-preemptive' },
    { id: 'mlq', name: 'Multilevel Queue (MLQ)', detail: 'Fixed queue assignment · Queue 0 has highest precedence' },
    { id: 'mlfq', name: 'Multilevel Feedback Queue (MLFQ)', detail: 'Dynamic demotion · 2/4/8 quanta · periodic priority boost' }
  ];

  function normalize(input) {
    if (!Array.isArray(input) || input.length === 0) {
      throw new Error('Add at least one process.');
    }
    if (input.length > 12) throw new Error('Maximum 12 processes are supported.');
    const seen = new Set();
    return input.map((raw, index) => {
      const pid = String(raw.pid ?? `P${index + 1}`).trim();
      const arrival = Number(raw.arrival);
      const burst = Number(raw.burst);
      const priority = Number(raw.priority ?? 0);
      const queue = Number(raw.queue ?? 1);
      if (!pid) throw new Error(`Process at row ${index + 1} needs a name.`);
      if (seen.has(pid)) throw new Error(`Duplicate process name: ${pid}`);
      seen.add(pid);
      if (!Number.isSafeInteger(arrival) || arrival < 0 || arrival > 1000000) throw new Error(`${pid}: arrival time must be a whole number from 0 to 1,000,000.`);
      if (!Number.isSafeInteger(burst) || burst <= 0 || burst > 1000000) throw new Error(`${pid}: burst time must be a whole number from 1 to 1,000,000.`);
      if (!Number.isSafeInteger(priority)) throw new Error(`${pid}: priority must be a whole number.`);
      if (!Number.isSafeInteger(queue) || queue < 0 || queue > 2) throw new Error(`${pid}: queue must be 0, 1 or 2.`);
      return { pid, arrival, burst, priority, queue, queueLevel: queue, index, remaining: burst, firstStart: null, completion: null, sliceRemaining: 0 };
    }).reduce((all, process) => {
      all.push(process);
      if (all.reduce((sum, p) => sum + p.burst, 0) > 50000) throw new Error('Total CPU burst is too large for a responsive browser simulation (maximum 50,000 time units).');
      return all;
    }, []);
  }

  function addSegment(segments, pid, start, end) {
    if (end <= start) return;
    const previous = segments[segments.length - 1];
    if (previous && previous.pid === pid && previous.end === start) previous.end = end;
    else segments.push({ pid, start, end, duration: end - start });
  }

  function chooseReady(ready, algorithm, currentPid, time) {
    const sorted = ready.slice();
    sorted.sort((a, b) => {
      let delta = 0;
      if (algorithm === 'sjf') delta = a.burst - b.burst;
      else if (algorithm === 'srtf') delta = a.remaining - b.remaining;
      else if (algorithm === 'priority-np' || algorithm === 'priority-p') delta = a.priority - b.priority;
      else if (algorithm === 'hrrn') {
        const waitA = time - a.arrival;
        const waitB = time - b.arrival;
        const ratioA = (waitA + a.burst) / a.burst;
        const ratioB = (waitB + b.burst) / b.burst;
        delta = ratioB - ratioA;
      }
      if (delta !== 0) return delta;
      // On equal SRTF/priority keys, keep the current process to avoid artificial preemption.
      if ((algorithm === 'srtf' || algorithm === 'priority-p') && currentPid) {
        if (a.pid === currentPid) return -1;
        if (b.pid === currentPid) return 1;
      }
      return a.arrival - b.arrival || a.index - b.index;
    });
    return sorted[0];
  }

  function simulateNonPreemptive(processes, algorithm) {
    const pending = processes.slice().sort((a, b) => a.arrival - b.arrival || a.index - b.index);
    const completed = [];
    const segments = [];
    let time = 0;
    while (completed.length < processes.length) {
      const ready = pending.filter(p => p.completion === null && p.arrival <= time);
      if (!ready.length) {
        const next = pending.find(p => p.completion === null);
        addSegment(segments, 'IDLE', time, next.arrival);
        time = next.arrival;
        continue;
      }
      const process = algorithm === 'fcfs'
        ? ready.sort((a, b) => a.arrival - b.arrival || a.index - b.index)[0]
        : chooseReady(ready, algorithm, null, time);
      process.firstStart = time;
      const start = time;
      time += process.burst;
      process.remaining = 0;
      process.completion = time;
      addSegment(segments, process.pid, start, time);
      completed.push(process);
    }
    return segments;
  }

  function simulatePreemptive(processes, algorithm) {
    const segments = [];
    let time = 0;
    let completeCount = 0;
    let currentPid = null;
    while (completeCount < processes.length) {
      const ready = processes.filter(p => p.arrival <= time && p.remaining > 0);
      if (!ready.length) {
        const nextArrival = Math.min(...processes.filter(p => p.remaining > 0).map(p => p.arrival));
        addSegment(segments, 'IDLE', time, nextArrival);
        time = nextArrival;
        currentPid = null;
        continue;
      }
      const process = chooseReady(ready, algorithm, currentPid, time);
      if (process.firstStart === null) process.firstStart = time;
      addSegment(segments, process.pid, time, time + 1);
      process.remaining -= 1;
      time += 1;
      currentPid = process.remaining > 0 ? process.pid : null;
      if (process.remaining === 0) {
        process.completion = time;
        completeCount += 1;
      }
    }
    return segments;
  }

  function simulateRoundRobin(processes, quantum) {
    if (!Number.isSafeInteger(quantum) || quantum <= 0) throw new Error('Round Robin quantum must be a whole number greater than 0.');
    const arrivals = processes.slice().sort((a, b) => a.arrival - b.arrival || a.index - b.index);
    const queue = [];
    const segments = [];
    let next = 0;
    let time = 0;
    let completed = 0;
    const enqueueArrivals = () => {
      while (next < arrivals.length && arrivals[next].arrival <= time) queue.push(arrivals[next++]);
    };

    while (completed < processes.length) {
      enqueueArrivals();
      if (!queue.length) {
        const nextArrival = arrivals[next].arrival;
        addSegment(segments, 'IDLE', time, nextArrival);
        time = nextArrival;
        enqueueArrivals();
      }
      const process = queue.shift();
      if (process.firstStart === null) process.firstStart = time;
      const runFor = Math.min(quantum, process.remaining);
      addSegment(segments, process.pid, time, time + runFor);
      process.remaining -= runFor;
      time += runFor;
      enqueueArrivals();
      if (process.remaining > 0) queue.push(process);
      else {
        process.completion = time;
        completed += 1;
      }
    }
    return segments;
  }

  function simulateMultilevel(processes, mode) {
    const mlfq = mode === 'mlfq';
    const queueQuantums = mlfq ? [2, 4, 8] : [2, 4, Number.POSITIVE_INFINITY];
    const ready = [[], [], []];
    const arrivals = processes.slice().sort((a, b) => a.arrival - b.arrival || a.index - b.index);
    const segments = [];
    let nextArrival = 0;
    let time = 0;
    let completed = 0;
    let current = null;

    const enqueueArrivals = () => {
      while (nextArrival < arrivals.length && arrivals[nextArrival].arrival <= time) {
        const process = arrivals[nextArrival++];
        if (mlfq) process.queueLevel = 0;
        ready[process.queueLevel].push(process);
      }
    };

    while (completed < processes.length) {
      // MLFQ variant: global priority boost every 30 time units to reduce starvation.
      if (mlfq && time > 0 && time % 30 === 0) {
        const boosted = [];
        if (current) boosted.push(current);
        for (let level = 0; level < 3; level += 1) boosted.push(...ready[level]);
        ready[0] = [];
        ready[1] = [];
        ready[2] = [];
        boosted.forEach(process => {
          process.queueLevel = 0;
          process.sliceRemaining = 0;
          ready[0].push(process);
        });
        current = null;
      }

      enqueueArrivals();

      // Any process in a higher-priority queue preempts a lower-queue process.
      if (current && ready.slice(0, current.queueLevel).some(q => q.length > 0)) {
        ready[current.queueLevel].unshift(current);
        current = null;
      }

      if (!current) {
        const level = ready.findIndex(q => q.length > 0);
        if (level < 0) {
          const nextTime = arrivals[nextArrival].arrival;
          addSegment(segments, 'IDLE', time, nextTime);
          time = nextTime;
          continue;
        }
        current = ready[level].shift();
        if (current.sliceRemaining <= 0) current.sliceRemaining = queueQuantums[level];
        current.queueLevel = level;
      }

      if (current.firstStart === null) current.firstStart = time;
      addSegment(segments, current.pid, time, time + 1);
      current.remaining -= 1;
      if (Number.isFinite(current.sliceRemaining)) current.sliceRemaining -= 1;
      time += 1;

      if (current.remaining === 0) {
        current.completion = time;
        completed += 1;
        current = null;
      } else if (current.sliceRemaining <= 0) {
        if (mlfq) current.queueLevel = Math.min(2, current.queueLevel + 1);
        current.sliceRemaining = 0;
        ready[current.queueLevel].push(current);
        current = null;
      }
    }

    return segments;
  }

  function calculateMetrics(processes, segments) {
    const rows = processes.map(p => {
      const completion = p.completion;
      const turnaround = completion - p.arrival;
      const waiting = turnaround - p.burst;
      const response = p.firstStart - p.arrival;
      return {
        pid: p.pid, arrival: p.arrival, burst: p.burst, priority: p.priority, queue: p.queue,
        firstStart: p.firstStart, completion, turnaround, waiting, response
      };
    }).sort((a, b) => a.pid.localeCompare(b.pid, undefined, { numeric: true }));
    const count = rows.length;
    const sum = key => rows.reduce((total, row) => total + row[key], 0);
    const lastCompletion = Math.max(...rows.map(r => r.completion));
    // Time starts at t=0; initial idle time before the first arrival is part of elapsed time.
    const elapsed = lastCompletion;
    const busy = rows.reduce((total, r) => total + r.burst, 0);
    const processSegments = segments.filter(s => s.pid !== 'IDLE');
    let switches = 0;
    for (let i = 1; i < processSegments.length; i += 1) {
      if (processSegments[i - 1].pid !== processSegments[i].pid) switches += 1;
    }
    return {
      rows,
      averages: {
        waiting: sum('waiting') / count,
        turnaround: sum('turnaround') / count,
        response: sum('response') / count
      },
      totalTime: lastCompletion,
      elapsed,
      busyTime: busy,
      idleTime: Math.max(0, elapsed - busy),
      cpuUtilization: elapsed > 0 ? (busy / elapsed) * 100 : 100,
      throughput: elapsed > 0 ? count / elapsed : count,
      contextSwitches: switches,
      dispatches: processSegments.length
    };
  }

  function run(input, algorithm, quantum) {
    const processes = normalize(input);
    if (!ALGORITHMS.some(a => a.id === algorithm)) throw new Error(`Unknown CPU scheduling algorithm: ${algorithm}`);
    const segments = algorithm === 'fcfs' || algorithm === 'sjf' || algorithm === 'priority-np' || algorithm === 'hrrn'
      ? simulateNonPreemptive(processes, algorithm)
      : algorithm === 'rr'
        ? simulateRoundRobin(processes, Number(quantum))
        : algorithm === 'mlq' || algorithm === 'mlfq'
          ? simulateMultilevel(processes, algorithm)
          : simulatePreemptive(processes, algorithm);
    // Durations are derived from segment boundaries after compression.
    segments.forEach(s => { s.duration = s.end - s.start; });
    const metrics = calculateMetrics(processes, segments);
    return { algorithm, algorithmName: ALGORITHMS.find(a => a.id === algorithm).name, segments, ...metrics };
  }

  function compare(input, quantum) {
    return ALGORITHMS.map(algorithm => run(input, algorithm.id, quantum));
  }

  return { ALGORITHMS, normalize, run, compare };
});
