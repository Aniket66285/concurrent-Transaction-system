'use strict';
const assert = require('node:assert/strict');
const scheduler = require('../dashboard/js/cpu_scheduling.js');

const p = [
  { pid: 'P1', arrival: 0, burst: 5, priority: 2, queue: 2 },
  { pid: 'P2', arrival: 1, burst: 3, priority: 1, queue: 0 },
  { pid: 'P3', arrival: 2, burst: 1, priority: 3, queue: 1 }
];
const names = result => result.segments.map(s => s.pid);

// FCFS: first arrival remains on CPU until completion.
let r = scheduler.run(p, 'fcfs', 2);
assert.deepEqual(names(r), ['P1', 'P2', 'P3']);
assert.deepEqual(r.rows.map(x => x.completion), [5, 8, 9]);
assert.deepEqual(r.rows.map(x => x.waiting), [0, 4, 6]);
console.log('FCFS completion/waiting metrics: PASS');

// Non-preemptive SJF selects the shortest burst among ready processes.
r = scheduler.run(p, 'sjf', 2);
assert.deepEqual(names(r), ['P1', 'P3', 'P2']);
assert.equal(r.rows.find(x => x.pid === 'P3').completion, 6);
console.log('SJF non-preemptive selection: PASS');

// SRTF preempts the currently running process as shorter jobs arrive.
r = scheduler.run(p, 'srtf', 2);
assert.deepEqual(names(r), ['P1', 'P2', 'P3', 'P2', 'P1']);
assert.equal(r.rows.find(x => x.pid === 'P3').completion, 3);
console.log('SRTF preemption and remaining-time ordering: PASS');

// Lower numeric priority is higher priority; NP and P variants differ.
r = scheduler.run(p, 'priority-np', 2);
assert.deepEqual(names(r), ['P1', 'P2', 'P3']);
const priorityP = scheduler.run(p, 'priority-p', 2);
assert.deepEqual(names(priorityP), ['P1', 'P2', 'P1', 'P3']);
console.log('Priority preemptive/non-preemptive: PASS');

// Round Robin respects quantum and rotates unfinished processes.
r = scheduler.run(p, 'rr', 2);
assert.deepEqual(names(r), ['P1', 'P2', 'P3', 'P1', 'P2', 'P1']);
assert.equal(r.rows.length, 3);
console.log('Round Robin quantum and FIFO ready queue: PASS');

// HRRN finishes every process and reports non-negative waits.
r = scheduler.run(p, 'hrrn', 2);
assert.equal(r.rows.length, 3);
assert.ok(r.rows.every(x => x.waiting >= 0 && x.turnaround >= x.burst));
console.log('HRRN response-ratio scheduling: PASS');

// MLQ: a newly arrived higher queue preempts a lower queue.
const mlqInput = [
  { pid: 'LOW', arrival: 0, burst: 5, priority: 5, queue: 2 },
  { pid: 'HIGH', arrival: 1, burst: 2, priority: 1, queue: 0 }
];
r = scheduler.run(mlqInput, 'mlq', 2);
assert.deepEqual(names(r), ['LOW', 'HIGH', 'LOW']);
assert.equal(r.rows.find(x => x.pid === 'HIGH').firstStart, 1);
console.log('Multilevel Queue fixed queues and preemption: PASS');

// MLFQ: jobs start in Q0; higher queue arrivals preempt lower queues.
const mlfqInput = [
  { pid: 'LONG', arrival: 0, burst: 8, priority: 9, queue: 2 },
  { pid: 'NEW', arrival: 3, burst: 1, priority: 1, queue: 2 }
];
r = scheduler.run(mlfqInput, 'mlfq', 2);
assert.equal(r.rows.find(x => x.pid === 'NEW').firstStart, 3);
assert.equal(r.rows.find(x => x.pid === 'LONG').completion, 9);
const boostRun = scheduler.run([
  { pid: 'P1', arrival: 0, burst: 80, priority: 1, queue: 0 },
  { pid: 'P2', arrival: 0, burst: 80, priority: 1, queue: 0 }
], 'mlfq', 2);
const boostedP2Slice = boostRun.segments.find(x => x.pid === 'P2' && x.start >= 30);
assert.equal(boostedP2Slice.start, 32, 'the global boost at t=30 should promote the waiting process');
console.log('Multilevel Feedback Queue demotion/preemption/priority boost: PASS');

// Initial idle time is included in CPU utilization.
r = scheduler.run([{ pid: 'LATE', arrival: 3, burst: 2, priority: 1, queue: 0 }], 'fcfs', 2);
assert.equal(r.segments[0].pid, 'IDLE');
assert.equal(r.cpuUtilization, 40);
assert.equal(r.idleTime, 3);
console.log('Idle time and CPU utilization: PASS');

// Input/quantum validation and complete algorithm comparison.
assert.throws(() => scheduler.run(p, 'rr', 0), /quantum/);
assert.throws(() => scheduler.run([{ pid: 'X', arrival: -1, burst: 2 }], 'fcfs', 2), /arrival/);
assert.equal(scheduler.compare(p, 2).length, 9);
console.log('Validation and 9-algorithm comparison: PASS');

console.log('\nAll CPU scheduling simulator tests passed!');
