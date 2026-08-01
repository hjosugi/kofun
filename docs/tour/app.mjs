import { analyzeKofun } from "./compiler.mjs";
import { GUIDES, STEPS } from "./content.mjs";
import { completionAt, hoverAt } from "./intelligence.mjs";
import { renderKofunKun } from "./kofun-kun.mjs";
import { runKofun } from "./runtime.mjs";
import { decodeShareHash, encodeShareHash } from "./share.mjs";

const elements = {
  stepList: document.querySelector("[data-step-list]"),
  eyebrow: document.querySelector("[data-eyebrow]"),
  title: document.querySelector("[data-step-title]"),
  intro: document.querySelector("[data-intro]"),
  exercise: document.querySelector("[data-exercise]"),
  editor: document.querySelector("[data-editor]"),
  run: document.querySelector("[data-run]"),
  reset: document.querySelector("[data-reset]"),
  share: document.querySelector("[data-share]"),
  next: document.querySelector("[data-next]"),
  status: document.querySelector("[data-status]"),
  output: document.querySelector("[data-output]"),
  ownership: document.querySelector("[data-ownership]"),
  guideList: document.querySelector("[data-guide-list]"),
  stage: document.querySelector("[data-stage]"),
  character: document.querySelector("[data-character]"),
  finish: document.querySelector("[data-finish]"),
  direction: document.querySelector("[data-direction]"),
  mirror: document.querySelector("[data-editor-mirror]"),
  completion: document.querySelector("[data-completion]"),
  hoverCard: document.querySelector("[data-hover-card]"),
  inspector: document.querySelector("[data-inspector]"),
  progress: document.querySelector("[data-progress]"),
  feedback: document.querySelector("[data-kofun-feedback]"),
  heroButton: document.querySelector("[data-kofun-button]"),
  heroKofun: document.querySelector("[data-kofun-hero]"),
  runnerKofun: document.querySelector("[data-kofun-runner]"),
};

let currentStep = STEPS[0];
let running = false;

// Editor intelligence state. The analysis is the compiler's own reading of the
// current text, recomputed whenever the text changes and never guessed at.
let analysis = { declarations: [], error: null };
let completionItems = [];
let completionIndex = 0;
let completionAnchor = null;
let highlight = null;

function refreshAnalysis() {
  analysis = analyzeKofun(elements.editor.value);
}

function renderMirror() {
  const text = elements.editor.value;
  elements.mirror.replaceChildren();
  if (highlight === null || highlight.start >= highlight.end) {
    elements.mirror.append(document.createTextNode(text));
  } else {
    const mark = document.createElement("mark");
    mark.append(document.createTextNode(text.slice(highlight.start, highlight.end)));
    elements.mirror.append(
      document.createTextNode(text.slice(0, highlight.start)),
      mark,
      document.createTextNode(text.slice(highlight.end)),
    );
  }
  elements.mirror.scrollTop = elements.editor.scrollTop;
  elements.mirror.scrollLeft = elements.editor.scrollLeft;
}

// The mirror may be split around a highlight, so this walks its text nodes in
// order rather than assuming there is only one.
function rectForOffset(offset) {
  const walker = document.createTreeWalker(elements.mirror, NodeFilter.SHOW_TEXT);
  let total = 0;
  for (let node = walker.nextNode(); node !== null; node = walker.nextNode()) {
    const length = node.nodeValue.length;
    if (offset <= total + length) {
      const range = document.createRange();
      const local = offset - total;
      range.setStart(node, local);
      range.setEnd(node, Math.min(local + 1, length));
      const rect = range.getBoundingClientRect();
      if (rect.width > 0 || rect.height > 0) return rect;
    }
    total += length;
  }
  return null;
}

// caretPositionFromPoint would answer with the textarea, which sits above the
// mirror and owns the pointer, so the offset is found by measuring instead.
// Character rectangles run top-to-bottom then start-to-end, including across
// wrapped lines, so the first offset that is not before the point can be found
// by bisection — a handful of measurements per pointer move rather than a scan.
function beforePoint(rect, x, y) {
  if (rect.bottom <= y) return true;
  if (rect.top > y) return false;
  return rect.right <= x;
}

function offsetFromPoint(x, y) {
  const length = elements.editor.value.length;
  if (length === 0) return null;
  let low = 0;
  let high = length;
  while (low < high) {
    const middle = (low + high) >> 1;
    const rect = rectForOffset(middle);
    if (rect !== null && beforePoint(rect, x, y)) low = middle + 1;
    else high = middle;
  }
  const rect = rectForOffset(low);
  if (rect === null) return null;
  // A point past the end of a line is not on any character of it.
  if (y < rect.top || y > rect.bottom || x < rect.left || x > rect.right) return null;
  return low;
}

function placeAt(element, offset, below) {
  const rect = rectForOffset(offset);
  const surface = elements.mirror.parentElement.getBoundingClientRect();
  if (rect === null) {
    element.style.insetInlineStart = "1rem";
    element.style.insetBlockStart = "1rem";
    return;
  }
  const start = rect.left - surface.left;
  const top = below ? rect.bottom - surface.top + 4 : rect.top - surface.top;
  element.style.insetInlineStart = `${Math.max(0, Math.round(start))}px`;
  element.style.insetBlockStart = `${Math.max(0, Math.round(top))}px`;
}

function hideCompletion() {
  completionItems = [];
  completionAnchor = null;
  elements.completion.hidden = true;
  elements.completion.replaceChildren();
  elements.editor.setAttribute("aria-expanded", "false");
  elements.editor.removeAttribute("aria-activedescendant");
}

function renderCompletion() {
  elements.completion.replaceChildren();
  for (const [index, entry] of completionItems.entries()) {
    const option = document.createElement("li");
    option.id = `completion-option-${index}`;
    option.setAttribute("role", "option");
    option.setAttribute("aria-selected", index === completionIndex ? "true" : "false");
    const label = document.createElement("span");
    label.className = "completion-label";
    label.textContent = entry.label;
    const kind = document.createElement("span");
    kind.className = "completion-kind";
    kind.textContent = entry.kind;
    const detail = document.createElement("span");
    detail.className = "completion-detail";
    detail.textContent = entry.note ?? entry.detail ?? "";
    option.append(label, kind, detail);
    option.addEventListener("mousedown", (event) => {
      event.preventDefault();
      acceptCompletion(index);
    });
    elements.completion.append(option);
  }
  elements.completion.hidden = false;
  elements.editor.setAttribute("aria-expanded", "true");
  elements.editor.setAttribute(
    "aria-activedescendant", `completion-option-${completionIndex}`);
  placeAt(elements.completion, completionAnchor, true);
}

function showCompletion() {
  const offset = elements.editor.selectionStart;
  if (offset !== elements.editor.selectionEnd) {
    hideCompletion();
    return;
  }
  const result = completionAt(analysis, elements.editor.value, offset);
  if (result.items.length === 0) {
    hideCompletion();
    return;
  }
  let start = offset;
  const text = elements.editor.value;
  while (start > 0 && /[A-Za-z0-9_]/u.test(text[start - 1])) start -= 1;
  completionItems = result.items;
  completionIndex = 0;
  completionAnchor = start;
  renderCompletion();
}

function acceptCompletion(index) {
  const entry = completionItems[index];
  if (entry === undefined || completionAnchor === null) return;
  const text = elements.editor.value;
  const caret = elements.editor.selectionStart;
  elements.editor.value =
    text.slice(0, completionAnchor) + entry.label + text.slice(caret);
  const next = completionAnchor + entry.label.length;
  elements.editor.setSelectionRange(next, next);
  hideCompletion();
  onEditorChanged();
  elements.editor.focus();
}

function describeHover(result) {
  if (result === null) {
    elements.hoverCard.hidden = true;
    if (highlight !== null) {
      highlight = null;
      renderMirror();
    }
    return;
  }
  const title = document.createElement("strong");
  title.textContent = result.title;
  const body = document.createElement("span");
  body.textContent = result.body;
  elements.hoverCard.replaceChildren(title, body);
  elements.hoverCard.hidden = false;
  const changed = highlight === null || highlight.start !== result.start ||
    highlight.end !== result.end;
  highlight = { start: result.start, end: result.end };
  if (changed) renderMirror();
  placeAt(elements.hoverCard, result.start, true);
}

function inspectCaret() {
  const offset = elements.editor.selectionStart;
  const result = hoverAt(analysis, elements.editor.value, offset);
  elements.inspector.textContent = result === null
    ? analysis.error === null
      ? "Hover a name, or put the caret on one, to see what the compiler knows about it."
      : `Does not compile yet: ${analysis.error}`
    : `${result.title} — ${result.body}`;
}

function onEditorChanged() {
  refreshAnalysis();
  renderMirror();
  inspectCaret();
}

function setStatus(message, state = "idle") {
  elements.status.textContent = message;
  elements.status.dataset.state = state;
}

function setKofunFeedback(message, pose = "idle") {
  elements.feedback.textContent = message;
  renderKofunKun(elements.runnerKofun, pose);
}

function renderResult(lines) {
  elements.output.textContent = lines.length === 0 ? "(no output)" : lines.join("\n");
  const hue = Number.parseInt(lines[0] ?? "210", 10);
  const distance = Number.parseInt(lines[1] ?? lines[0] ?? "50", 10);
  const safeHue = Number.isFinite(hue) ? ((hue % 360) + 360) % 360 : 210;
  const safeDistance = Number.isFinite(distance)
    ? Math.max(8, Math.min(92, Math.abs(distance)))
    : 50;
  elements.stage.style.setProperty("--result-hue", safeHue);
  elements.stage.style.setProperty("--result-distance", `${safeDistance}%`);
  elements.character.classList.remove("is-running");
  void elements.character.offsetWidth;
  elements.character.classList.add("is-running");
  elements.finish.style.setProperty("--finish-position", `${safeDistance}%`);
}

async function runEditor({ announceGuide = true } = {}) {
  if (running) return;
  running = true;
  elements.run.disabled = true;
  setStatus("Compiling locally…", "working");
  if (announceGuide) {
    setKofunFeedback("I am checking exactly what this browser compiler accepts…", "blink");
  }
  try {
    const { lines, moduleBytes } = await runKofun(elements.editor.value);
    renderResult(lines);
    if (announceGuide) setKofunFeedback(currentStep.guide.success, "smile");
    setStatus(
      `Ran ${moduleBytes.length} WebAssembly bytes in this browser.`,
      "success",
    );
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    const expectedFailure = currentStep.expectedError === message;
    elements.output.textContent = message;
    if (announceGuide) {
      setKofunFeedback(
        expectedFailure
          ? currentStep.guide.expectedFailure
          : "Read the diagnostic, repair the source, and I will try the exact same path again.",
        expectedFailure ? "smile" : "blink",
      );
    }
    setStatus(
      expectedFailure
        ? "The expected checked failure was reported. Now repair it."
        : "The program stopped with a useful diagnostic.",
      expectedFailure ? "lesson" : "error",
    );
  } finally {
    elements.run.disabled = false;
    running = false;
  }
}

function renderOwnership(step) {
  elements.ownership.replaceChildren();
  elements.ownership.hidden = step.ownership === undefined;
  if (step.ownership === undefined) return;

  const heading = document.createElement("h3");
  heading.textContent = "The prevented bug, then the vocabulary";
  const bug = document.createElement("p");
  bug.textContent = step.ownership.bug;
  bug.className = "bug-callout";
  const prevention = document.createElement("p");
  prevention.textContent = step.ownership.prevention;
  const list = document.createElement("ul");
  for (const rule of step.ownership.rules) {
    const item = document.createElement("li");
    item.textContent = rule;
    list.append(item);
  }
  const boundary = document.createElement("p");
  boundary.className = "boundary-note";
  boundary.textContent =
    "Honest boundary: edit/read/take are target language design here. The current browser compiler does not parse or enforce them.";
  elements.ownership.append(heading, bug, prevention, list, boundary);
}

function selectStep(step, source = step.source, { autorun = true } = {}) {
  currentStep = step;
  const stepIndex = STEPS.indexOf(step);
  elements.eyebrow.textContent = step.eyebrow;
  elements.title.textContent = step.title;
  elements.intro.textContent = step.intro;
  elements.exercise.textContent = step.exercise;
  elements.editor.value = source;
  hideCompletion();
  describeHover(null);
  onEditorChanged();
  renderOwnership(step);
  elements.progress.textContent = `Step ${stepIndex + 1} of ${STEPS.length}`;
  elements.next.textContent = stepIndex === STEPS.length - 1
    ? "Start again"
    : "Next lesson";
  setKofunFeedback(step.guide.ready);
  for (const button of elements.stepList.querySelectorAll("button")) {
    const selected = button.dataset.step === step.id;
    button.setAttribute("aria-current", selected ? "step" : "false");
  }
  setStatus("Ready. Edit the code or press Run.");
  if (autorun) void runEditor({ announceGuide: false });
}

function selectNextStep() {
  const currentIndex = STEPS.indexOf(currentStep);
  selectStep(STEPS[(currentIndex + 1) % STEPS.length]);
  document.querySelector("#lesson").scrollIntoView({ block: "start" });
}

function renderSteps() {
  for (const [index, step] of STEPS.entries()) {
    const item = document.createElement("li");
    const button = document.createElement("button");
    button.type = "button";
    button.dataset.step = step.id;
    const number = document.createElement("span");
    number.className = "step-number";
    number.textContent = String(index + 1).padStart(2, "0");
    const label = document.createElement("span");
    label.className = "step-label";
    label.textContent = step.title;
    button.append(number, label);
    button.addEventListener("click", () => selectStep(step));
    item.append(button);
    elements.stepList.append(item);
  }
}

function renderGuides() {
  for (const guide of GUIDES) {
    const article = document.createElement("article");
    article.id = `from-${guide.id}`;
    article.className = "guide-card";
    const title = document.createElement("h3");
    title.textContent = `Coming from ${guide.name}`;
    const transfers = document.createElement("p");
    transfers.textContent = `What transfers: ${guide.transfers}`;
    const surprise = document.createElement("p");
    surprise.textContent = `What surprises: ${guide.surprise}`;
    const worse = document.createElement("p");
    worse.className = "worse-note";
    worse.textContent = `Where Kofun is worse today: ${guide.worse}`;
    const comparison = document.createElement("div");
    comparison.className = "comparison";
    for (const [label, code] of [[guide.name, guide.from], ["Kofun", guide.to]]) {
      const sample = document.createElement("div");
      const sampleTitle = document.createElement("h4");
      sampleTitle.textContent = label;
      const pre = document.createElement("pre");
      pre.textContent = code;
      sample.append(sampleTitle, pre);
      comparison.append(sample);
    }
    article.append(title, transfers, surprise, worse, comparison);
    elements.guideList.append(article);
  }
}

async function shareEditor() {
  try {
    const url = new URL(window.location.href);
    url.hash = encodeShareHash(currentStep.id, elements.editor.value);
    history.replaceState(null, "", url);
    if (navigator.clipboard?.writeText !== undefined) {
      await navigator.clipboard.writeText(url.href);
      setStatus("Share link copied. The code is stored only in the URL.", "success");
    } else {
      setStatus("Share link is now in the address bar.", "success");
    }
  } catch (error) {
    setStatus(error instanceof Error ? error.message : "Could not create link", "error");
  }
}

function loadSharedSource() {
  try {
    const shared = decodeShareHash(window.location.hash);
    if (shared === null) return null;
    const step = STEPS.find((candidate) => candidate.id === shared.stepId) ?? STEPS[0];
    return { step, source: shared.source };
  } catch (error) {
    setStatus(error instanceof Error ? error.message : "Invalid share link", "error");
    return null;
  }
}

elements.run.addEventListener("click", runEditor);
elements.reset.addEventListener("click", () => selectStep(currentStep));
elements.share.addEventListener("click", shareEditor);
elements.next.addEventListener("click", selectNextStep);
elements.heroButton.addEventListener("click", () => {
  if (elements.heroButton.classList.contains("is-hopping")) return;
  renderKofunKun(elements.heroKofun, "smile");
  elements.heroButton.classList.add("is-hopping");
  elements.heroKofun.addEventListener("animationend", () => {
    elements.heroButton.classList.remove("is-hopping");
    renderKofunKun(elements.heroKofun, "idle");
  }, { once: true });
});
elements.editor.addEventListener("keydown", (event) => {
  if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
    event.preventDefault();
    hideCompletion();
    void runEditor();
    return;
  }
  if (!elements.completion.hidden) {
    if (event.key === "ArrowDown" || event.key === "ArrowUp") {
      event.preventDefault();
      const step = event.key === "ArrowDown" ? 1 : -1;
      completionIndex =
        (completionIndex + step + completionItems.length) % completionItems.length;
      renderCompletion();
      return;
    }
    if (event.key === "Enter" || event.key === "Tab") {
      event.preventDefault();
      acceptCompletion(completionIndex);
      return;
    }
    if (event.key === "Escape") {
      event.preventDefault();
      hideCompletion();
      return;
    }
  }
  // Ctrl/⌘ + Space asks for the list without typing, as editors do.
  if ((event.ctrlKey || event.metaKey) && event.key === " ") {
    event.preventDefault();
    onEditorChanged();
    showCompletion();
  }
});

elements.editor.addEventListener("input", () => {
  onEditorChanged();
  showCompletion();
});

elements.editor.addEventListener("scroll", renderMirror);
elements.editor.addEventListener("blur", () => {
  hideCompletion();
  describeHover(null);
});
elements.editor.addEventListener("click", () => {
  hideCompletion();
  inspectCaret();
});
elements.editor.addEventListener("keyup", (event) => {
  if (event.key.startsWith("Arrow") || event.key === "Home" || event.key === "End") {
    inspectCaret();
  }
});

elements.editor.addEventListener("mousemove", (event) => {
  const offset = offsetFromPoint(event.clientX, event.clientY);
  describeHover(offset === null
    ? null : hoverAt(analysis, elements.editor.value, offset));
});
elements.editor.addEventListener("mouseleave", () => describeHover(null));
elements.direction.addEventListener("change", () => {
  document.documentElement.dir = elements.direction.value;
});

for (const sprite of document.querySelectorAll("[data-kofun-sprite]")) {
  renderKofunKun(sprite, sprite.dataset.pose ?? "idle");
}
renderKofunKun(elements.heroKofun, "idle");
renderKofunKun(elements.runnerKofun, "idle");
renderSteps();
renderGuides();
const initial = loadSharedSource();
selectStep(initial?.step ?? STEPS[0], initial?.source ?? STEPS[0].source);
