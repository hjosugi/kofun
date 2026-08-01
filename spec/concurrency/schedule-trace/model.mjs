import crypto from "node:crypto";
import fs from "node:fs";

const MAX = Object.freeze({ tasks: 32, decisions: 256, steps: 512, states: 4096 });
const TRACE_SCHEMA = "kofun.schedule-trace/v1";
const WITNESS_SCHEMA = "kofun.schedule-witness/v1";
const PROGRAM_SCHEMA = "kofun.schedule-program/v1";
const SEMANTICS = "kofun.scoped-concurrency/model-v1";
const TASK_ID = /^s0\.t0(?:\.s1\.t[1-9][0-9]*)*$/;
const EVENT_KINDS = new Set(["spawn", "yield", "block", "wake", "join", "cancel", "send", "receive", "ownership", "stdout", "complete", "failure"]);

class ModelError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

function reject(code, message) {
  throw new ModelError(code, message);
}

function exact(value, keys, label, code = "EPROGRAM") {
  if (value === null || typeof value !== "object" || Array.isArray(value)) reject(code, `${label} must be an object`);
  const actual = Object.keys(value).sort();
  const wanted = [...keys].sort();
  if (actual.length !== wanted.length || actual.some((key, index) => key !== wanted[index])) {
    reject(code, `${label} has unknown or missing fields`);
  }
}

function canonicalValue(value) {
  if (Array.isArray(value)) return value.map(canonicalValue);
  if (value !== null && typeof value === "object") {
    return Object.fromEntries(Object.keys(value).sort().map((key) => [key, canonicalValue(value[key])]));
  }
  return value;
}

function canonical(value) {
  return `${JSON.stringify(canonicalValue(value), null, 2)}\n`;
}

function parseCanonical(raw, label) {
  let value;
  try {
    value = JSON.parse(raw);
  } catch {
    reject("ETRACE_PARSE", `${label} is malformed JSON`);
  }
  if (canonical(value) !== raw) reject("ETRACE_PARSE", `${label} is not canonical JSON`);
  return value;
}

function integer(value, min, max, label, code = "EPROGRAM") {
  if (!Number.isSafeInteger(value) || value < min || value > max) reject(code, `${label} must be ${min}..${max}`);
}

function validateBudgets(value, code = "EPROGRAM") {
  exact(value, ["decisions", "states", "steps", "tasks"], "budgets", code);
  for (const key of Object.keys(MAX)) integer(value[key], 1, MAX[key], `budgets.${key}`, code);
}

function validateOperations(operations, depth = 0) {
  if (!Array.isArray(operations) || depth > 8) reject("EPROGRAM", "operation nesting exceeds eight scopes");
  for (const [index, operation] of operations.entries()) {
    const label = `operation[${index}]`;
    if (operation?.op === "spawn") {
      exact(operation, ["as", "body", "op"], label);
      if (typeof operation.as !== "string" || !/^[a-z][a-z0-9_]*$/.test(operation.as)) reject("EPROGRAM", `${label}.as is invalid`);
      validateOperations(operation.body, depth + 1);
    } else if (operation?.op === "yield") {
      exact(operation, ["op"], label);
    } else if (operation?.op === "print") {
      exact(operation, ["op", "text"], label);
      if (typeof operation.text !== "string") reject("EPROGRAM", `${label}.text must be a string`);
    } else if (operation?.op === "fail") {
      exact(operation, ["code", "message", "op"], label);
      if (!/^E[A-Z0-9_]+$/.test(operation.code) || typeof operation.message !== "string") reject("EPROGRAM", `${label} failure is invalid`);
    } else if (operation?.op === "join" || operation?.op === "cancel") {
      exact(operation, ["op", "task"], label);
      if (typeof operation.task !== "string") reject("EPROGRAM", `${label}.task must be a local handle name`);
    } else if (operation?.op === "send") {
      exact(operation, ["channel", "op", "value"], label);
      if (typeof operation.channel !== "string" || typeof operation.value !== "string") reject("EPROGRAM", `${label} send is invalid`);
    } else if (operation?.op === "receive") {
      exact(operation, ["channel", "op"], label);
      if (typeof operation.channel !== "string") reject("EPROGRAM", `${label}.channel is invalid`);
    } else if (operation?.op === "ownership") {
      exact(operation, ["mode", "op", "resource"], label);
      if (!["read", "edit", "take"].includes(operation.mode) || typeof operation.resource !== "string") {
        reject("EPROGRAM", `${label} ownership request is invalid`);
      }
    } else {
      reject("EPROGRAM", `${label} has unknown operation`);
    }
  }
}

function validateProgram(program) {
  exact(program, ["budgets", "channels", "name", "root", "schema"], "program");
  if (program.schema !== PROGRAM_SCHEMA) reject("EPROGRAM", "program schema is incompatible");
  if (typeof program.name !== "string" || program.name.length === 0) reject("EPROGRAM", "program name is empty");
  validateBudgets(program.budgets);
  if (!Array.isArray(program.channels) || program.channels.some((name) => typeof name !== "string")) reject("EPROGRAM", "channels must be names");
  if (new Set(program.channels).size !== program.channels.length) reject("EPROGRAM", "channel names must be unique");
  validateOperations(program.root);
  return program;
}

function digest(program) {
  return crypto.createHash("sha256").update(canonical(program)).digest("hex");
}

function initialState(program) {
  const root = "s0.t0";
  return {
    program,
    tasks: {
      [root]: { id: root, scope: "s0", parent: null, body: program.root, pc: 0, status: "runnable", bindings: {}, children: [], joined: [], nextChild: 1 },
    },
    ready: [root],
    channels: Object.fromEntries(program.channels.map((name) => [name, []])),
    ownership: {},
    stdout: "",
    stderr: "",
    diagnostic: null,
    ownershipEvent: null,
    events: [],
    decisions: [],
    eventSequence: 0,
    steps: 0,
    bound: null,
    seed: 0,
  };
}

function emit(state, kind, task, detail = {}) {
  state.events.push({ sequence: state.eventSequence++, kind, task: task.id, scope: task.scope, detail });
}

function isTerminal(task) {
  return ["complete", "failed", "cancelled"].includes(task.status);
}

function ready(state, task, reason) {
  if (isTerminal(task) || task.status === "runnable") return;
  task.status = "runnable";
  state.ready.push(task.id);
  emit(state, "wake", task, { reason });
}

function releaseOwnership(state, taskId) {
  for (const resource of Object.keys(state.ownership)) {
    state.ownership[resource] = state.ownership[resource].filter((claim) => claim.task !== taskId);
    if (state.ownership[resource].length === 0) delete state.ownership[resource];
  }
}

function wakeParents(state, child) {
  if (!child.parent) return;
  const parent = state.tasks[child.parent];
  if (!parent || isTerminal(parent)) return;
  if (parent.status === `join:${child.id}`) ready(state, parent, "join-terminal");
  if (parent.status === "scope" && parent.children.every((id) => isTerminal(state.tasks[id]))) ready(state, parent, "scope-terminal");
}

function failTask(state, task, code, message) {
  task.status = "failed";
  state.diagnostic = { code, message, task: task.id };
  state.stderr += `${code}: ${message}\n`;
  emit(state, "failure", task, { code, message });
  releaseOwnership(state, task.id);
  wakeParents(state, task);
}

function finishTask(state, task) {
  if (isTerminal(task) || task.pc < task.body.length) return;
  const live = task.children.filter((id) => !isTerminal(state.tasks[id]));
  if (live.length !== 0) {
    task.status = "scope";
    return;
  }
  const failed = task.children.map((id) => state.tasks[id]).find((child) => child.status === "failed" && !task.joined.includes(child.id));
  if (failed) {
    failTask(state, task, failed === undefined ? "ECHILD" : state.diagnostic?.code ?? "ECHILD", "child scope failed");
    return;
  }
  task.status = "complete";
  releaseOwnership(state, task.id);
  emit(state, "complete", task, {});
  wakeParents(state, task);
}

function cancelTree(state, task) {
  for (const childId of task.children) if (!isTerminal(state.tasks[childId])) cancelTree(state, state.tasks[childId]);
  if (isTerminal(task)) return;
  task.status = "cancelled";
  state.ready = state.ready.filter((id) => id !== task.id);
  releaseOwnership(state, task.id);
  emit(state, "cancel", task, { owner: "lexical-scope" });
  wakeParents(state, task);
}

function execute(state, selected) {
  const task = state.tasks[selected];
  state.steps += 1;
  const operation = task.body[task.pc];
  if (!operation) {
    finishTask(state, task);
    return;
  }
  if (operation.op === "spawn") {
    if (Object.keys(state.tasks).length >= state.program.budgets.tasks) {
      state.bound = "tasks";
      return;
    }
    if (task.bindings[operation.as]) return failTask(state, task, "EHANDLE", "local task handle already bound");
    const childId = `${task.id}.s1.t${task.nextChild++}`;
    const child = { id: childId, scope: `${task.id}.s1`, parent: task.id, body: operation.body, pc: 0, status: "new", bindings: {}, children: [], joined: [], nextChild: 1 };
    state.tasks[childId] = child;
    task.bindings[operation.as] = childId;
    task.children.push(childId);
    task.pc += 1;
    ready(state, child, "spawn");
    emit(state, "spawn", task, { child: childId });
  } else if (operation.op === "yield") {
    task.pc += 1;
    emit(state, "yield", task, { origin: "user" });
  } else if (operation.op === "print") {
    task.pc += 1;
    state.stdout += operation.text;
    emit(state, "stdout", task, { text: operation.text });
  } else if (operation.op === "fail") {
    task.pc += 1;
    failTask(state, task, operation.code, operation.message);
  } else if (operation.op === "join") {
    const targetId = task.bindings[operation.task];
    const target = state.tasks[targetId];
    if (!target || task.joined.includes(targetId)) return failTask(state, task, "EHANDLE", "invalid or consumed join handle");
    if (!isTerminal(target)) {
      task.status = `join:${targetId}`;
      emit(state, "block", task, { reason: "join", target: targetId });
    } else {
      task.joined.push(targetId);
      task.pc += 1;
      emit(state, "join", task, { outcome: target.status, target: targetId });
      if (target.status === "failed") failTask(state, task, state.diagnostic?.code ?? "ECHILD", "joined child failed");
    }
  } else if (operation.op === "cancel") {
    const targetId = task.bindings[operation.task];
    const target = state.tasks[targetId];
    if (!target) return failTask(state, task, "EHANDLE", "invalid cancel handle");
    if (!isTerminal(target)) cancelTree(state, target);
    task.pc += 1;
  } else if (operation.op === "send") {
    if (!(operation.channel in state.channels)) return failTask(state, task, "ECHANNEL", "unknown channel");
    state.channels[operation.channel].push(operation.value);
    task.pc += 1;
    emit(state, "send", task, { channel: operation.channel, value: operation.value });
    for (const waiting of Object.values(state.tasks).filter((candidate) => candidate.status === `receive:${operation.channel}`).sort((a, b) => a.id.localeCompare(b.id))) {
      ready(state, waiting, "channel-send");
      break;
    }
  } else if (operation.op === "receive") {
    if (!(operation.channel in state.channels)) return failTask(state, task, "ECHANNEL", "unknown channel");
    if (state.channels[operation.channel].length === 0) {
      task.status = `receive:${operation.channel}`;
      emit(state, "block", task, { channel: operation.channel, reason: "receive" });
    } else {
      const value = state.channels[operation.channel].shift();
      task.pc += 1;
      emit(state, "receive", task, { channel: operation.channel, value });
    }
  } else if (operation.op === "ownership") {
    const claims = state.ownership[operation.resource] ?? [];
    const conflict = claims.find((claim) => claim.task !== task.id && (operation.mode !== "read" || claim.mode !== "read"));
    const ownership = { resource: operation.resource, requested: operation.mode, held_by: conflict?.task ?? null, held_mode: conflict?.mode ?? null };
    state.ownershipEvent = ownership;
    emit(state, "ownership", task, ownership);
    task.pc += 1;
    if (conflict) failTask(state, task, "EOWN01", "sibling ownership conflict");
    else state.ownership[operation.resource] = [...claims, { task: task.id, mode: operation.mode }];
  }
  if (task.status === "runnable") finishTask(state, task);
}

function observation(state) {
  const root = state.tasks["s0.t0"];
  let exitCategory = "success";
  if (state.bound) exitCategory = "budget";
  else if (state.diagnostic?.code === "EDEADLOCK") exitCategory = "deadlock";
  else if (root.status === "failed" || state.diagnostic) exitCategory = "language-runtime-error";
  return { stdout: state.stdout, stderr: state.stderr, exit_category: exitCategory, ownership_event: state.ownershipEvent, diagnostic: state.diagnostic };
}

function terminal(state) {
  const tasks = Object.values(state.tasks);
  if (state.bound) return true;
  if (tasks.every(isTerminal)) return true;
  if (state.ready.length === 0) {
    const root = state.tasks["s0.t0"];
    failTask(state, root, "EDEADLOCK", "no runnable task can make progress");
    return true;
  }
  return false;
}

function chooseSeeded(state) {
  let x = state.seed >>> 0;
  x ^= x << 13; x ^= x >>> 17; x ^= x << 5;
  state.seed = x >>> 0;
  return state.ready[state.seed % state.ready.length];
}

function advance(state, selected) {
  if (state.decisions.length >= state.program.budgets.decisions) {
    state.bound = "decisions";
    return;
  }
  if (state.steps >= state.program.budgets.steps) {
    state.bound = "steps";
    return;
  }
  const runnable = [...state.ready];
  if (!runnable.includes(selected)) reject("EREPLAY_TASK", `selected task ${selected} is not runnable`);
  state.decisions.push({ index: state.decisions.length, runnable, selected });
  state.ready.splice(state.ready.indexOf(selected), 1);
  execute(state, selected);
  const task = state.tasks[selected];
  if (task?.status === "runnable") state.ready.push(selected);
}

function forbiddenAuthority(value) {
  if (Array.isArray(value)) return value.some(forbiddenAuthority);
  if (value && typeof value === "object") {
    return Object.entries(value).some(([key, child]) => /handle|token|capability|address|thread[_-]?id/i.test(key) || forbiddenAuthority(child));
  }
  return false;
}

function validateTrace(trace, program) {
  if (forbiddenAuthority(trace)) reject("ETRACE_AUTHORITY", "trace serializes authority");
  exact(trace, ["algorithm", "budgets", "decisions", "events", "program_digest", "schema", "seed", "target_semantics"], "trace", "ETRACE_VERSION");
  if (trace.schema !== TRACE_SCHEMA || trace.target_semantics !== SEMANTICS || !["fifo-v1", "seeded-xorshift32-v1", "explicit-replay-v1", "exhaustive-dfs-v1"].includes(trace.algorithm)) {
    reject("ETRACE_VERSION", "trace version is incompatible");
  }
  if (trace.program_digest !== digest(program)) reject("ETRACE_PROGRAM", "program digest differs");
  try { validateBudgets(trace.budgets, "ETRACE_BUDGET"); } catch (error) { if (error instanceof ModelError) throw error; throw error; }
  if (JSON.stringify(trace.budgets) !== JSON.stringify(program.budgets)) reject("ETRACE_BUDGET", "trace budgets differ");
  if (trace.seed !== null) integer(trace.seed, 0, 0xffffffff, "trace.seed", "ETRACE_VERSION");
  if (!Array.isArray(trace.decisions) || !Array.isArray(trace.events)) reject("ETRACE_VERSION", "trace arrays are missing");
  trace.decisions.forEach((decision, index) => {
    exact(decision, ["index", "runnable", "selected"], `decisions[${index}]`, "ETRACE_VERSION");
    if (decision.index !== index || !Array.isArray(decision.runnable) || !TASK_ID.test(decision.selected) || decision.runnable.some((id) => !TASK_ID.test(id))) {
      reject("EREPLAY_TASK", `decision ${index} has an invalid task ID`);
    }
  });
  trace.events.forEach((event, index) => {
    exact(event, ["detail", "kind", "scope", "sequence", "task"], `events[${index}]`, "ETRACE_VERSION");
    if (event.sequence !== index || !EVENT_KINDS.has(event.kind) || !TASK_ID.test(event.task) || typeof event.scope !== "string") {
      reject("ETRACE_VERSION", `event ${index} is invalid`);
    }
    const label = `events[${index}].detail`;
    if (event.kind === "spawn") {
      exact(event.detail, ["child"], label, "ETRACE_VERSION");
      if (!TASK_ID.test(event.detail.child)) reject("ETRACE_VERSION", `${label}.child is invalid`);
    } else if (event.kind === "yield") {
      exact(event.detail, ["origin"], label, "ETRACE_VERSION");
      if (event.detail.origin !== "user") reject("ETRACE_VERSION", `${label}.origin is invalid`);
    } else if (event.kind === "block") {
      if (event.detail?.reason === "join") {
        exact(event.detail, ["reason", "target"], label, "ETRACE_VERSION");
        if (!TASK_ID.test(event.detail.target)) reject("ETRACE_VERSION", `${label}.target is invalid`);
      } else {
        exact(event.detail, ["channel", "reason"], label, "ETRACE_VERSION");
        if (event.detail.reason !== "receive" || typeof event.detail.channel !== "string") reject("ETRACE_VERSION", `${label} is invalid`);
      }
    } else if (event.kind === "wake") {
      exact(event.detail, ["reason"], label, "ETRACE_VERSION");
      if (!["spawn", "join-terminal", "scope-terminal", "channel-send"].includes(event.detail.reason)) reject("ETRACE_VERSION", `${label}.reason is invalid`);
    } else if (event.kind === "join") {
      exact(event.detail, ["outcome", "target"], label, "ETRACE_VERSION");
      if (!["complete", "failed", "cancelled"].includes(event.detail.outcome) || !TASK_ID.test(event.detail.target)) reject("ETRACE_VERSION", `${label} is invalid`);
    } else if (event.kind === "cancel") {
      exact(event.detail, ["owner"], label, "ETRACE_VERSION");
      if (event.detail.owner !== "lexical-scope") reject("ETRACE_VERSION", `${label}.owner is invalid`);
    } else if (event.kind === "send" || event.kind === "receive") {
      exact(event.detail, ["channel", "value"], label, "ETRACE_VERSION");
      if (typeof event.detail.channel !== "string" || typeof event.detail.value !== "string") reject("ETRACE_VERSION", `${label} is invalid`);
    } else if (event.kind === "ownership") {
      exact(event.detail, ["held_by", "held_mode", "requested", "resource"], label, "ETRACE_VERSION");
      if (!["read", "edit", "take"].includes(event.detail.requested) || typeof event.detail.resource !== "string" ||
          (event.detail.held_by !== null && !TASK_ID.test(event.detail.held_by)) ||
          (event.detail.held_mode !== null && !["read", "edit", "take"].includes(event.detail.held_mode))) {
        reject("ETRACE_VERSION", `${label} is invalid`);
      }
    } else if (event.kind === "stdout") {
      exact(event.detail, ["text"], label, "ETRACE_VERSION");
      if (typeof event.detail.text !== "string") reject("ETRACE_VERSION", `${label}.text is invalid`);
    } else if (event.kind === "complete") {
      exact(event.detail, [], label, "ETRACE_VERSION");
    } else if (event.kind === "failure") {
      exact(event.detail, ["code", "message"], label, "ETRACE_VERSION");
      if (!/^E[A-Z0-9_]+$/.test(event.detail.code) || typeof event.detail.message !== "string") reject("ETRACE_VERSION", `${label} is invalid`);
    }
  });
}

function traceFor(state, algorithm, seed) {
  return { schema: TRACE_SCHEMA, algorithm, target_semantics: SEMANTICS, program_digest: digest(state.program), seed, budgets: { ...state.program.budgets }, decisions: state.decisions, events: state.events };
}

function witnessFor(state, policy, seed, algorithm = policy === "fifo" ? "fifo-v1" : "seeded-xorshift32-v1") {
  return { schema: WITNESS_SCHEMA, program: state.program, policy, trace: traceFor(state, algorithm, seed), status: state.bound ? "budget" : observation(state).exit_category, bound: state.bound, observation: observation(state) };
}

function run(program, policy, seed = null, replayTrace = null) {
  validateProgram(program);
  if (replayTrace) validateTrace(replayTrace, program);
  const state = initialState(program);
  state.seed = seed ?? 0;
  let replayIndex = 0;
  while (!terminal(state)) {
    if (state.decisions.length >= state.program.budgets.decisions) {
      state.bound = "decisions";
      break;
    }
    if (state.steps >= state.program.budgets.steps) {
      state.bound = "steps";
      break;
    }
    let selected;
    if (replayTrace) {
      if (replayIndex >= replayTrace.decisions.length) reject("EREPLAY_EARLY", "trace ended before execution");
      const recorded = replayTrace.decisions[replayIndex++];
      if (JSON.stringify(recorded.runnable) !== JSON.stringify(state.ready)) reject("EREPLAY_RUNNABLE", `decision ${recorded.index} runnable set drifted`);
      if (!state.ready.includes(recorded.selected)) reject("EREPLAY_TASK", `decision ${recorded.index} selected an absent task`);
      selected = recorded.selected;
    } else {
      selected = policy === "seeded" ? chooseSeeded(state) : state.ready[0];
    }
    advance(state, selected);
  }
  if (replayTrace) {
    if (replayIndex !== replayTrace.decisions.length) reject("EREPLAY_SUFFIX", "trace has an unconsumed decision suffix");
    if (canonical(state.events) !== canonical(replayTrace.events)) reject("EREPLAY_EVENT", "semantic events differ");
  }
  return state;
}

function stateKey(state) {
  return canonicalValue({
    tasks: Object.fromEntries(Object.entries(state.tasks).map(([id, task]) => [id, { pc: task.pc, status: task.status, bindings: task.bindings, children: task.children, joined: task.joined, nextChild: task.nextChild }])),
    ready: state.ready,
    channels: state.channels,
    ownership: state.ownership,
    stdout: state.stdout,
    stderr: state.stderr,
    diagnostic: state.diagnostic,
    ownershipEvent: state.ownershipEvent,
  });
}

function exhaustive(program) {
  validateProgram(program);
  const seen = new Set();
  const terminals = [];
  const bounds = new Set();
  let failure = null;
  let stateLimitReached = false;

  function betterWitness(candidate, incumbent) {
    if (!incumbent) return true;
    const left = candidate.trace.decisions.map((decision) => decision.selected).join("\u0000");
    const right = incumbent.trace.decisions.map((decision) => decision.selected).join("\u0000");
    return candidate.trace.decisions.length < incumbent.trace.decisions.length ||
      (candidate.trace.decisions.length === incumbent.trace.decisions.length && left < right);
  }

  function visit(state) {
    if (stateLimitReached) return;
    if (terminal(state)) {
      if (state.bound) bounds.add(state.bound);
      const observed = observation(state);
      terminals.push(observed.exit_category);
      if (observed.exit_category !== "success" && observed.exit_category !== "budget") {
        const candidate = witnessFor(state, "exhaustive", null, "exhaustive-dfs-v1");
        if (betterWitness(candidate, failure)) failure = candidate;
      }
      return;
    }
    const key = JSON.stringify(stateKey(state));
    if (seen.has(key)) return;
    if (seen.size >= program.budgets.states) {
      bounds.add("states");
      stateLimitReached = true;
      return;
    }
    seen.add(key);
    for (const selected of [...state.ready]) {
      const next = structuredClone(state);
      advance(next, selected);
      visit(next);
      if (stateLimitReached) return;
    }
  }

  visit(initialState(program));
  return { schema: "kofun.schedule-exploration/v1", program, visited_states: seen.size, terminal_count: terminals.length, terminal_categories: [...new Set(terminals)].sort(), bounds_reached: [...bounds].sort(), failure_witness: failure };
}

function replayWitness(witness) {
  exact(witness, ["bound", "observation", "policy", "program", "schema", "status", "trace"], "witness", "ETRACE_VERSION");
  if (witness.schema !== WITNESS_SCHEMA) reject("ETRACE_VERSION", "witness version is incompatible");
  const state = run(witness.program, "replay", witness.trace.seed, witness.trace);
  if (canonical(observation(state)) !== canonical(witness.observation)) reject("EREPLAY_EVENT", "terminal observation differs");
  return witness.observation;
}

function baseProgram() {
  return { schema: PROGRAM_SCHEMA, name: "replay-rejection-base", budgets: { tasks: 4, decisions: 16, steps: 16, states: 64 }, channels: [], root: [{ op: "print", text: "a\n" }, { op: "yield" }, { op: "print", text: "b\n" }] };
}

function rejectionTest(fixture) {
  exact(fixture, ["case", "expect"], "rejection fixture");
  const program = baseProgram();
  const base = witnessFor(run(program, "fifo"), "fifo", null);
  let thrown = null;
  try {
    if (fixture.case === "parse") parseCanonical('{"schema":', "trace");
    else {
      const witness = structuredClone(base);
      if (fixture.case === "version") witness.trace.schema = "kofun.schedule-trace/v2";
      else if (fixture.case === "program") witness.trace.program_digest = "0".repeat(64);
      else if (fixture.case === "budget") witness.trace.budgets.decisions -= 1;
      else if (fixture.case === "authority") witness.trace.task_handle = "live-capability";
      else if (fixture.case === "runnable") witness.trace.decisions[0].runnable.push("s0.t0.s1.t9");
      else if (fixture.case === "task") witness.trace.decisions[0].selected = "s0.t0.s1.t9";
      else if (fixture.case === "early") witness.trace.decisions.pop();
      else if (fixture.case === "suffix") witness.trace.decisions.push({ index: witness.trace.decisions.length, runnable: ["s0.t0"], selected: "s0.t0" });
      else if (fixture.case === "event") witness.trace.events[0].detail.text = "changed\n";
      else reject("EPROGRAM", "unknown rejection fixture");
      replayWitness(witness);
    }
  } catch (error) {
    if (error instanceof ModelError) thrown = error.code;
    else throw error;
  }
  if (thrown !== fixture.expect) reject("EPROGRAM", `${fixture.case}: expected ${fixture.expect}, got ${thrown}`);
}

function readJson(file) {
  return JSON.parse(fs.readFileSync(file, "utf8"));
}

function main(args) {
  const command = args[0];
  if (command === "run" && (args[1] === "fifo" || args[1] === "seeded")) {
    const program = readJson(args[2]);
    const seed = args[1] === "seeded" ? Number(args[3]) : null;
    if (args[1] === "seeded") integer(seed, 0, 0xffffffff, "seed");
    process.stdout.write(canonical(witnessFor(run(program, args[1], seed), args[1], seed)));
  } else if (command === "replay") {
    const program = readJson(args[1]);
    const witness = parseCanonical(fs.readFileSync(args[2], "utf8"), "witness");
    if (digest(validateProgram(program)) !== digest(validateProgram(witness.program))) reject("ETRACE_PROGRAM", "witness program differs from supplied program");
    process.stdout.write(canonical(replayWitness(witness)));
  } else if (command === "exhaustive") {
    process.stdout.write(canonical(exhaustive(readJson(args[1]))));
  } else if (command === "replay-witness") {
    const report = parseCanonical(fs.readFileSync(args[1], "utf8"), "exploration report");
    if (!report.failure_witness) reject("EREPLAY_EVENT", "report has no failure witness");
    process.stdout.write(canonical(replayWitness(report.failure_witness)));
  } else if (command === "rejection-test") {
    rejectionTest(readJson(args[1]));
  } else {
    reject("EUSAGE", "usage: model.mjs run fifo|seeded PROGRAM [SEED] | replay PROGRAM WITNESS | exhaustive PROGRAM | replay-witness REPORT | rejection-test FIXTURE");
  }
}

try {
  main(process.argv.slice(2));
} catch (error) {
  if (error instanceof ModelError) console.error(`${error.code}: ${error.message}`);
  else console.error(`schedule-trace-model: ${error.stack ?? error.message}`);
  process.exitCode = 1;
}
