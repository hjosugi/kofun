import { GUIDES, STEPS } from "./content.mjs";
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
  status: document.querySelector("[data-status]"),
  output: document.querySelector("[data-output]"),
  ownership: document.querySelector("[data-ownership]"),
  guideList: document.querySelector("[data-guide-list]"),
  stage: document.querySelector("[data-stage]"),
  character: document.querySelector("[data-character]"),
  finish: document.querySelector("[data-finish]"),
  direction: document.querySelector("[data-direction]"),
};

let currentStep = STEPS[0];
let running = false;

function setStatus(message, state = "idle") {
  elements.status.textContent = message;
  elements.status.dataset.state = state;
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

async function runEditor() {
  if (running) return;
  running = true;
  elements.run.disabled = true;
  setStatus("Compiling locally…", "working");
  try {
    const { lines, moduleBytes } = await runKofun(elements.editor.value);
    renderResult(lines);
    setStatus(
      `Ran ${moduleBytes.length} WebAssembly bytes in this browser.`,
      "success",
    );
  } catch (error) {
    elements.output.textContent = error instanceof Error ? error.message : String(error);
    setStatus("The program stopped with a useful diagnostic.", "error");
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
  elements.eyebrow.textContent = step.eyebrow;
  elements.title.textContent = step.title;
  elements.intro.textContent = step.intro;
  elements.exercise.textContent = step.exercise;
  elements.editor.value = source;
  renderOwnership(step);
  for (const button of elements.stepList.querySelectorAll("button")) {
    const selected = button.dataset.step === step.id;
    button.setAttribute("aria-current", selected ? "step" : "false");
  }
  setStatus("Ready. Edit the code or press Run.");
  if (autorun) void runEditor();
}

function renderSteps() {
  for (const [index, step] of STEPS.entries()) {
    const item = document.createElement("li");
    const button = document.createElement("button");
    button.type = "button";
    button.dataset.step = step.id;
    button.textContent = `${index + 1}. ${step.title}`;
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
elements.editor.addEventListener("keydown", (event) => {
  if ((event.ctrlKey || event.metaKey) && event.key === "Enter") {
    event.preventDefault();
    void runEditor();
  }
});
elements.direction.addEventListener("change", () => {
  document.documentElement.dir = elements.direction.value;
});

renderSteps();
renderGuides();
const initial = loadSharedSource();
selectStep(initial?.step ?? STEPS[0], initial?.source ?? STEPS[0].source);
