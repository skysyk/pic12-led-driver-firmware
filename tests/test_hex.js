// Targeted PIC14 instruction model for linked PWM ISR, copy and startup checks.
// Run after building: node tests/test_hex.js [build/led-controller]
const fs = require('fs'), assert = require('assert');
const imageBase = process.argv[2] || 'build/led-controller';
const map = fs.readFileSync(imageBase + '.map', 'utf8');
const symbols = new Map([...map.matchAll(/^(\S+)\s+\S+\s+([0-9A-F]+)\s*$/gm)]
  .map(m => [m[1], parseInt(m[2], 16)]));
const sym = n => { assert(symbols.has(n), n); return symbols.get(n); };
const code = new Uint8Array(65536); let base = 0;
for (const line of fs.readFileSync(imageBase + '.hex', 'utf8').trim().split(/\r?\n/)) {
  const b = Buffer.from(line.slice(1), 'hex'), addr = b.readUInt16BE(1);
  if (b[3] === 4) base = b.readUInt16BE(4) << 16;
  else if (b[3] === 0 && base + addr < code.length) code.set(b.subarray(4, 4 + b[0]), base + addr);
}
class CPU {
  constructor() { this.ram = new Uint8Array(512); this.w = 0; this.pc = 0; this.stack = []; this.done = false; this.cycles = 0; this.ccpCycles = 0; }
  canonical(a) { const f = a & 127; return [2,3,4,10,11].includes(f) || f >= 112 ? f : a; }
  address(f) { let a = f | ((this.ram[3] & 32) << 2); if (!f) a = this.ram[4] | ((this.ram[3] & 128) << 1); return this.canonical(a); }
  get(f) { const a = this.address(f); return a === 0 ? 0 : a === 2 ? this.pc & 255 : this.ram[a]; }
  set(f, v) {
    const a = this.address(f);
    if (a === 2) { this.pc = ((this.ram[10] & 7) << 8) | (v & 255); ++this.cycles; }
    else if (a) this.ram[a] = v & 255;
    if (a === 21) this.ccpCycles = this.cycles;
  }
  zero(v) { this.ram[3] = (this.ram[3] & ~4) | ((v & 255) === 0 ? 4 : 0); }
  arithmetic(v, carry, digitCarry) { this.zero(v); this.ram[3] = (this.ram[3] & ~3) | +carry | (+digitCarry << 1); }
  skip() { ++this.pc; ++this.cycles; }
  step() {
    const op = code[this.pc * 2] | (code[this.pc * 2 + 1] << 8); this.pc++; this.cycles++;
    const f = op & 127, d = op & 128, bit = (op >> 7) & 7;
    if (op === 8 || op === 9) { ++this.cycles; if (op === 9) this.ram[11] |= 128; if (!this.stack.length) this.done = true; else this.pc = this.stack.pop(); }
    else if (op === 0 || op === 0x64) { /* NOP / watchdog clear */ }
    else if ((op & 0x3c00) === 0x3000) this.w = op & 255;
    else if ((op & 0x3c00) === 0x3400) { ++this.cycles; this.w = op & 255; this.pc = this.stack.pop(); }
    else if ((op & 0x3800) === 0x2800) { ++this.cycles; this.pc = op & 2047; }
    else if ((op & 0x3800) === 0x2000) { ++this.cycles; this.stack.push(this.pc); this.pc = op & 2047; }
    else if ((op & 0x3f80) === 0x80) this.set(f, this.w);
    else if ((op & 0x3f80) === 0x180) { this.set(f, 0); this.zero(0); }
    else if ((op & 0x3c00) === 0x1000) this.set(f, this.get(f) & ~(1 << bit));
    else if ((op & 0x3c00) === 0x1400) this.set(f, this.get(f) | (1 << bit));
    else if ((op & 0x3c00) === 0x1800) { if (!(this.get(f) & (1 << bit))) this.skip(); }
    else if ((op & 0x3c00) === 0x1c00) { if (this.get(f) & (1 << bit)) this.skip(); }
    else if ((op & 0x3f00) === 0x3800) { this.w |= op & 255; this.zero(this.w); }
    else if ((op & 0x3f00) === 0x3900) { this.w &= op & 255; this.zero(this.w); }
    else if ((op & 0x3f00) === 0x3a00) { this.w ^= op & 255; this.zero(this.w); }
    else if ((op & 0x3e00) === 0x3e00) {
      const old = this.w, n = op & 255; this.w = (old + n) & 255;
      this.arithmetic(this.w, old + n > 255, (old & 15) + (n & 15) > 15);
    }
    else if ((op & 0x3e00) === 0x3c00) {
      const old = this.w, n = op & 255; this.w = (n - old) & 255;
      this.arithmetic(this.w, n >= old, (n & 15) >= (old & 15));
    }
    else {
      const kind = op & 0x3f00; let v = this.get(f);
      if (kind === 0x0800) this.zero(v);
      else if (kind === 0x0a00) { v = (v + 1) & 255; this.zero(v); }
      else if (kind === 0x0b00) { v = (v - 1) & 255; if (!v) this.skip(); }
      else if (kind === 0x0f00) { v = (v + 1) & 255; if (!v) this.skip(); }
      else if (kind === 0x0600) { v ^= this.w; this.zero(v); }
      else if (kind === 0x0400) { v |= this.w; this.zero(v); }
      else if (kind === 0x0500) { v &= this.w; this.zero(v); }
      else if (kind === 0x0700) { const old = v; v = (v + this.w) & 255; this.arithmetic(v, old + this.w > 255, (old & 15) + (this.w & 15) > 15); }
      else if (kind === 0x0200) { const old = v; v = (v - this.w) & 255; this.arithmetic(v, old >= this.w, (old & 15) >= (this.w & 15)); }
      else if (kind === 0x0300) { v = (v - 1) & 255; this.zero(v); }
      else if (kind === 0x0900) { v = (~v) & 255; this.zero(v); }
      else if (kind === 0x0c00) { const carry = v & 1; v = (v >> 1) | ((this.ram[3] & 1) << 7); this.ram[3] = (this.ram[3] & ~1) | carry; }
      else if (kind === 0x0d00) { const carry = v >> 7; v = ((v << 1) | (this.ram[3] & 1)) & 255; this.ram[3] = (this.ram[3] & ~1) | carry; }
      else if (kind === 0x0e00) v = ((v << 4) | (v >> 4)) & 255;
      else throw new Error(`Unsupported instruction ${op.toString(16)} at ${(this.pc - 1).toString(16)}`);
      if (d) this.set(f, v); else this.w = v;
    }
  }
}
const buffers = ['main@input', '_current', '_snapshot', '_requested', '_saved'].map(sym);
function copy(dst, src, seed, irqAt = -1) {
  const cpu = new CPU(); cpu.ram.fill(0xa5); cpu.ram[3] = 0x80; cpu.ram[10] = 0;
  const payload = Array.from({length:5}, (_, i) => (seed + 67 * i) & 255);
  cpu.ram.set(payload, src); cpu.ram[sym('dimmer_copy_state@source')] = src;
  cpu.pc = sym('_dimmer_copy_state'); cpu.w = dst; let steps = 0;
  while (!cpu.done) {
    if (steps === irqAt) {
      const returnPC = cpu.pc; cpu.ram[11] = 4; cpu.ram[140] = 0; cpu.ram[sym('_pending_ticks')] = 0;
      cpu.stack.push(returnPC); cpu.pc = 4; let limit = 100;
      while (cpu.stack.length) { assert(limit-- > 0); cpu.step(); }
    }
    assert(steps++ < 1000); cpu.step();
  }
  assert.deepStrictEqual([...cpu.ram.slice(dst, dst + 5)], payload);
  return steps;
}
let copies = 0;
for (const src of buffers) for (const dst of buffers) {
  for (let seed = 0; seed < 256; seed++) { copy(dst, src, seed); copies++; }
  const n = copy(dst, src, 0);
  for (let irq = 0; irq < n; irq++) for (const seed of [0, 128, 255]) { copy(dst, src, seed, irq); copies++; }
}
const cpu = new CPU(); cpu.ram.fill(0xa5); cpu.ram[3] = 0x18; cpu.ram[10] = 0;
for (let n = 0; cpu.pc !== sym('_main'); n++) { assert(n < 10000); cpu.step(); }
for (const [name, size] of [['_stable',1],['_long_press',1],['_duration',6],['_debounce',3],['_repeat_count',3],['_dither',1],['_pwm_low',1]])
  assert([...cpu.ram.slice(sym(name), sym(name) + size)].every(v => v === 0), name);
console.log(`PASS: actual HEX settings-copy routine, ${copies} cases across all five buffers, every payload byte, and interrupt boundaries; C startup clears all button state.`);

let irqCases = 0, maxCycles = 0, maxWriteCycles = 0;
for (let q3 = 0; q3 <= 3200; ++q3) for (let residual = 0; residual < 4; ++residual) {
  for (const status of [0x18, 0xbf]) {
    const q2 = (q3 + 1) >> 1;
    const c = new CPU(), both = status === 0xbf, w = (q3 + residual) & 255;
    c.ram[sym('_pwm_high')] = q2 >> 4; c.ram[sym('_pwm_low')] = (q2 & 15) << 1;
    c.ram[sym('_dither')] = residual << 1; c.ram[sym('_pending_ticks')] = 253;
    let sum = 0;
    for (let n = 0; n < 4; ++n) {
      const expectedAccumulator = (c.ram[sym('_dither')] & 6) + ((q2 & 15) << 1);
      c.pc = 4; c.done = false; c.cycles = 4; c.ccpCycles = 0; // Worst-case 4-cycle hardware entry latency.
      c.w = w; c.ram[3] = status; c.ram[4] = 0x91; c.ram[10] = q3 & 7;
      c.ram[11] = both ? 4 : 0; c.ram[12] = 2; c.ram[140] = 2;
      let steps = 0;
      while (!c.done) { assert(++steps < 100); c.step(); }
      assert(c.w === w && c.ram[3] === status && c.ram[4] === 0x91 && c.ram[10] === (q3 & 7));
      assert(c.ram[11] === 128 && !(c.ram[12] & 2));
      assert(!!(c.ram[140] & 2) === !!(q2 & 3));
      assert(c.ram[sym('_dither')] === expectedAccumulator);
      assert(c.ram[19] === (q2 >> 4) + (expectedAccumulator >> 5));
      assert(c.ram[21] === (12 | ((expectedAccumulator & 24) << 1)));
      const actual = (c.ram[19] << 2) | ((c.ram[21] >> 4) & 3);
      assert(actual >= (q2 >> 2) && actual <= ((q2 + 3) >> 2));
      sum += actual;
      maxCycles = Math.max(maxCycles, c.cycles); maxWriteCycles = Math.max(maxWriteCycles, c.ccpCycles);
      ++irqCases;
    }
    assert(sum === q2 && (c.ram[sym('_dither')] & 6) === residual * 2);
    assert(c.ram[sym('_pending_ticks')] === (both ? 255 : 253));
  }
}
// Each PWM period is 100 instruction cycles at 8 MHz. Both duty fields must
// finish before the next latch, with room for a short foreground GIE-off region.
assert(maxWriteCycles < 80 && maxCycles < 100);
console.log(`PASS: actual HEX ISR, ${irqCases} entries, all targets/residuals and both banks, coincident Timer0/saturation, context restoration; max ${maxWriteCycles} cycles to duty write, ${maxCycles} total (including 4-cycle entry).`);

// Advance Timer2 independently of instruction/register accesses. A flag that
// arrives while the renderer publishes a new target must still get its slot.
let timelineCases = 0, maxBoundaryDelay = 0;
function renderTimeline(oldTarget, newTarget, firstBoundary, timer0Offset) {
  const c = new CPU(), oldQ2 = (oldTarget + 1) >> 1, newQ2 = (newTarget + 1) >> 1;
  c.ram[sym('_pwm_high')] = oldQ2 >> 4; c.ram[sym('_pwm_low')] = (oldQ2 & 15) << 1;
  c.ram[19] = oldQ2 >> 4; c.ram[21] = 12 | ((oldQ2 & 12) << 2);
  c.ram[sym('_gpio_shadow')] = 1;
  c.ram[140] = 2; c.ram[11] = 0xe0;
  c.ram[sym('hw_render@duty')] = newTarget & 255;
  c.ram[sym('hw_render@duty') + 1] = newTarget >> 8;
  c.ram[sym('hw_render@indicator')] = 0;
  c.pc = sym('_hw_render');
  let boundary = firstBoundary, timer0 = Math.max(0, firstBoundary + timer0Offset);
  let returned = false, inISR = false, awaiting = null, serviced = 0;
  const end = firstBoundary + 1100;
  while (c.cycles < end || inISR || !returned) {
    assert(c.cycles < end + 1000, 'renderer did not return');
    const flag = c.ram[12] & 2, wasISR = inISR, writes = c.ccpCycles;
    const op = code[c.pc * 2] | (code[c.pc * 2 + 1] << 8);
    if (returned && !inISR) ++c.cycles;
    else {
      c.step();
      if (op === 9) inISR = false;
      if (c.done) { returned = true; c.done = false; }
    }
    assert(!(flag && !(c.ram[12] & 2) && !wasISR),
      `lost Timer2 boundary: ${oldTarget}->${newTarget}, offset ${firstBoundary}, T0 ${timer0Offset}`);
    if (c.ccpCycles !== writes) {
      assert(awaiting !== null, 'unexpected duty-register update');
      const delay = c.ccpCycles - awaiting;
      assert(delay < 100, `missed PWM latch: ${delay} cycles`);
      maxBoundaryDelay = Math.max(maxBoundaryDelay, delay);
      awaiting = null; ++serviced;
    }
    if (c.cycles >= boundary) {
      assert(awaiting === null, 'previous dithering slot was not serviced');
      c.ram[12] |= 2; awaiting = boundary; boundary += 200;
    }
    if (c.cycles >= timer0) { c.ram[11] |= 4; timer0 = Infinity; }
    if ((c.ram[11] & 128) && (((c.ram[11] & 0x40) && (c.ram[140] & c.ram[12] & 2)) ||
        ((c.ram[11] & 0x20) && (c.ram[11] & 4)))) {
      assert(!inISR);
      if (c.ram[140] & c.ram[12] & 2) {
        const target = (c.ram[sym('_pwm_high')] << 4) | (c.ram[sym('_pwm_low')] >> 1);
        assert(target === oldQ2 || target === newQ2, 'torn target at interrupt entry');
      }
      c.stack.push(c.pc); c.pc = 4; c.ram[11] &= ~128; c.cycles += 4; inISR = true;
    }
  }
  assert(returned && serviced === 6 && awaiting === null);
  assert(((c.ram[sym('_pwm_high')] << 4) | (c.ram[sym('_pwm_low')] >> 1)) === newQ2);
  ++timelineCases;
}
for (const [oldTarget, newTarget] of [[9,11], [29,33], [251,257], [3197,3195]])
  for (let offset = 0; offset < 200; ++offset)
    for (let t0 = -64; t0 <= 64; ++t0) renderTimeline(oldTarget, newTarget, offset, t0);
console.log(`PASS: actual HEX target-update race, ${timelineCases} cycle-driven Timer2/Timer0 schedules; no lost slots or torn targets, max ${maxBoundaryDelay} cycles to duty write.`);

// Independent analytical curve matching the simulator's exponential option.
function referenceExponential(phase) {
  return (Math.exp(-Math.cos(2 * Math.PI * phase / 2048)) - Math.exp(-1)) /
         (Math.E - Math.exp(-1));
}
const exponentialKnots = Array.from({length:65}, (_, i) => Math.round(referenceExponential(i * 32) * 256));
let envelopeCases = 0, maxEnvelopeCycles = 0;
function envelope(phase, span, irqAt = -1) {
  const c = new CPU();
  c.pc = sym('_exponential_level');
  c.ram[sym('exponential_level@phase')] = phase & 255;
  c.ram[sym('exponential_level@phase') + 1] = phase >> 8;
  c.ram[sym('exponential_level@span')] = span;
  let steps = 0;
  while (!c.done) {
    if (steps === irqAt) {
      const depth = c.stack.length;
      c.ram[11] = 4; c.ram[12] = c.ram[140] = 2;
      c.ram[sym('_pwm_high')] = 99; c.ram[sym('_pwm_low')] = 30;
      c.stack.push(c.pc); c.pc = 4;
      while (c.stack.length > depth) c.step();
      assert(!c.done);
    }
    assert(++steps < 2000); c.step();
  }
  const i = phase >> 5;
  const weight = exponentialKnots[i] * 32 + (exponentialKnots[i + 1] - exponentialKnots[i]) * (phase & 31);
  const expected = Math.floor((weight * span + 128) / 256);
  const a = sym('?_exponential_level'), actual = c.ram[a] | (c.ram[a + 1] << 8);
  assert(actual === expected, `exponential envelope ${phase}/${span}: ${actual} != ${expected}`);
  if (irqAt < 0) maxEnvelopeCycles = Math.max(maxEnvelopeCycles, c.cycles);
  ++envelopeCases;
  return steps;
}
for (let phase = 0; phase < 2048; ++phase)
  for (const span of [1,2,3,17,50,99,100,128]) envelope(phase, span);
for (const phase of [0,31,32,512,992,1023,1024,1025,1056,1536,1984,2016,2047])
  for (const span of [1,100]) {
    const steps = envelope(phase, span);
    for (let irq = 0; irq < steps; ++irq) envelope(phase, span, irq);
  }
console.log(`PASS: actual HEX exponential envelope, ${envelopeCases} phase/span/interrupt cases, analytic-generated knots, reflected exhale and peak wrap; max ${maxEnvelopeCycles} foreground cycles.`);

// Run the linked production main loop with independent Timer0/Timer2 clocks,
// raw held/released buttons, and asynchronous 4 ms EEPROM writes. Check that
// sustained dithering has bounded tick latency, catches up after saves, and
// never loses a timer tick or misses a PWM duty latch.
let mainCases = 0, mainTicks = 0, maxTickCycles = 0, mainWrites = 0, lateTicks = 0, maxBacklog = 0, mainPWMLatency = 0;
function mainTimeline(period, phase, heldButton) {
  const c = new CPU(), eeprom = new Uint8Array(256).fill(255);
  let pressed = heldButton, write = null, completedWrites = 0;
  const oldGet = c.get.bind(c), oldSet = c.set.bind(c);
  c.get = f => c.address(f) === 5 ? (c.ram[5] & ~0x38) | ((~pressed & 7) << 3) : oldGet(f);
  c.set = (f,v) => {
    const address = c.address(f); oldSet(f,v);
    if (address === 0x9c) {
      if (v & 1) { c.ram[0x9a] = eeprom[c.ram[0x9b]]; c.ram[0x9c] &= ~1; }
      if ((v & 2) && !write) write = {end:c.cycles + 8000, address:c.ram[0x9b], value:c.ram[0x9a]};
    }
  };
  // Boot fully, stopping at the first render so normal EEPROM initialization
  // and C startup have already executed. Tests then select a breathing state.
  while (c.pc !== sym('_hw_render')) { assert(c.cycles < 100000); c.step(); }
  c.ram.set([50,1,128 | period,0,100], sym('_current'));
  c.ram[sym('_breath_phase')] = phase & 255; c.ram[sym('_breath_phase') + 1] = phase >> 8;
  if (heldButton) {
    c.ram[sym('_stable')] = heldButton; c.ram[sym('_long_press')] = heldButton;
    const index = heldButton === 2 ? 1 : 2;
    c.ram[sym('_duration') + index * 2] = 391 & 255;
    c.ram[sym('_duration') + index * 2 + 1] = 391 >> 8;
  }
  let nextTick = c.cycles + 2048, nextPWM = c.cycles + 200;
  let inISR = false, completed = 0;
  const arrivals = [], end = c.cycles + 2048 * 1102;
  while (completed < 1100) {
    assert(c.cycles < end, 'production loop fell behind real-time ticks');
    const op = code[c.pc * 2] | (code[c.pc * 2 + 1] << 8);
    const previousWrite = c.ccpCycles;
    c.step();
    if (inISR && c.ccpCycles !== previousWrite) {
      const latency = c.ccpCycles - (nextPWM - 200);
      assert(latency < 100, "production loop missed PWM latch: " + latency);
      mainPWMLatency = Math.max(mainPWMLatency, latency);
    }
    if (op === 9) inISR = false;
    if (op === 0x64) {
      assert(arrivals.length, 'watchdog serviced without a timer tick');
      const latency = c.cycles - arrivals.shift();
      assert(latency < 4096, 'tick backlog exceeds two timer intervals: ' + latency + ' period/phase/button/tick=' + [period,phase,heldButton,completed].join('/'));
      maxTickCycles = Math.max(maxTickCycles, latency);
      if (latency >= 2048) ++lateTicks;
      maxBacklog = Math.max(maxBacklog, arrivals.length);
      ++completed;
      if (completed === 40) pressed = 0;
    }
    if (write && c.cycles >= write.end) {
      eeprom[write.address] = write.value; write = null;
      c.ram[0x9c] &= ~2; ++completedWrites;
    }
    if (c.cycles >= nextPWM) { c.ram[12] |= 2; nextPWM += 200; }
    if (c.cycles >= nextTick) {
      assert(!(c.ram[11] & 4), 'Timer0 overflow lost before ISR');
      c.ram[11] |= 4; arrivals.push(nextTick); nextTick += 2048;
    }
    if ((c.ram[11] & 128) && (((c.ram[11] & 0x40) && (c.ram[140] & c.ram[12] & 2)) ||
        ((c.ram[11] & 0x20) && (c.ram[11] & 4)))) {
      assert(!inISR); c.stack.push(c.pc); c.pc = 4;
      c.ram[11] &= ~128; c.cycles += 4; inISR = true;
    }
  }
  if (heldButton) assert(completedWrites >= 12, 'deferred preference save was not exercised');
  mainWrites += completedWrites; mainTicks += completed; ++mainCases;
}
for (const phase of [0,512,900,1536,2047]) for (const button of [0,2,4]) mainTimeline(20,phase,button);
for (const button of [0,2,4]) mainTimeline(1,512,button);
console.log('PASS: actual HEX main loop, ' + mainCases + ' real-time schedules, ' + mainTicks + ' completed ticks and ' + mainWrites + ' EEPROM writes; no lost ticks, max boundary-to-watchdog ' + maxTickCycles + ' cycles; ' + lateTicks + ' ticks exceed 2048, max additional backlog ' + maxBacklog + ', max PWM write delay ' + mainPWMLatency + ' cycles.');
