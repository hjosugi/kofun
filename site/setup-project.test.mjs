import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import {
  buildIterationConfiguration,
  buildReconciliationPlan,
  buildViewCreateRequest,
  desiredFieldUpdates,
  filterOpenCuratedIssues,
  normalizeSchedule,
  parseArguments,
  projectViewKey,
  tokenGuidance,
  validateConfig,
  verifyRepositoryRemote,
} from "./setup-project.mjs";

const config = JSON.parse(
  await readFile(new URL("./project-config.json", import.meta.url), "utf8"),
);
const liveSnapshot = JSON.parse(
  await readFile(
    new URL("../app/roadmap/plan-snapshot.json", import.meta.url),
    "utf8",
  ),
);

test("configuration pins the only writable repository and required fields", () => {
  assert.deepEqual(validateConfig(structuredClone(config)), config);
  assert.equal(config.project.visibility, "PUBLIC");
  assert.match(config.project.readme, /hjosugi\.github\.io\/kofun\/roadmap/);
  assert.throws(
    () =>
      validateConfig({
        ...structuredClone(config),
        repository: "someone/else",
      }),
    /Remote-write guard/,
  );
});

test("arguments are offline dry-run by default and require explicit apply", () => {
  assert.equal(parseArguments([]).apply, false);
  assert.equal(parseArguments(["--apply"]).apply, true);
  assert.throws(() => parseArguments(["--write"]), /Unknown argument/);
});

test("live plan schedule schema maps fields, lanes, status, and dates", () => {
  const schedule = normalizeSchedule(
    {
      schedule: [
        {
          number: 618,
          title: "Bootstrap",
          priority: "high",
          size: "m",
          lane: "writer-a",
          start_date: "2026-07-27",
          end_date: "2026-07-29",
          schedule_status: "in-progress",
        },
        {
          issue: { number: 622, title: "Next" },
          fields: {
            priority: "P0",
            size: "L",
            agentSlot: "reviewer",
            startDate: "2026-07-30",
            targetDate: "2026-08-01",
          },
          status: "ready",
        },
      ],
    },
    config.mappings,
  );

  assert.deepEqual(schedule.get(618), {
    issueNumber: 618,
    title: "Bootstrap",
    status: "In Progress",
    priority: "P1",
    size: "M",
    agentSlot: "A",
    startDate: "2026-07-27",
    targetDate: "2026-07-29",
    iteration: undefined,
  });
  assert.equal(schedule.get(622).agentSlot, "Review");
  assert.equal(schedule.get(622).priority, "P0");
  assert.throws(
    () =>
      normalizeSchedule(
        { schedule: [{ number: 1, start_date: "2026-02-30" }] },
        config.mappings,
      ),
    /invalid start date/,
  );
});

test("checked-in snapshot values all map to configured Project options", () => {
  const schedule = normalizeSchedule(liveSnapshot, config.mappings);
  const allowed = {
    status: new Set(["Todo", "In Progress", "Done"]),
    priority: new Set(["P0", "P1", "P2", "P3", undefined]),
    size: new Set(["XS", "S", "M", "L", "XL", undefined]),
    agentSlot: new Set(["A", "B", "C", "Review", undefined]),
  };
  assert.ok(schedule.size > 0);
  assert.equal(
    schedule.size,
    liveSnapshot.summary.open_curated,
    "every open curated issue must have a Project planning record",
  );
  for (const item of schedule.values()) {
    for (const [field, options] of Object.entries(allowed)) {
      assert.ok(
        options.has(item[field]),
        `#${item.issueNumber} has unmapped ${field} ${item[field]}`,
      );
    }
    if (item.startDate || item.targetDate) {
      assert.match(item.startDate, /^\d{4}-\d{2}-\d{2}$/);
      assert.match(item.targetDate, /^\d{4}-\d{2}-\d{2}$/);
    }
  }
  const iteration = config.project.fields.find(
    (field) => field.name === "Iteration",
  ).iteration;
  const finalIterationEnd = buildIterationConfiguration(iteration).iterations.at(
    -1,
  );
  const coverageEnd = new Date(`${finalIterationEnd.startDate}T00:00:00Z`);
  coverageEnd.setUTCDate(
    coverageEnd.getUTCDate() + finalIterationEnd.duration - 1,
  );
  const lastTarget = [...schedule.values()]
    .map((item) => item.targetDate)
    .filter(Boolean)
    .sort()
    .at(-1);
  assert.ok(lastTarget <= coverageEnd.toISOString().slice(0, 10));
});

test("iteration configuration produces deterministic weekly values", () => {
  const result = buildIterationConfiguration({
    startDate: "2026-07-27",
    duration: 7,
    count: 3,
    titlePrefix: "Week",
  });
  assert.deepEqual(
    result.iterations.map(({ title, startDate }) => ({ title, startDate })),
    [
      { title: "Week 1 (2026-07-27)", startDate: "2026-07-27" },
      { title: "Week 2 (2026-08-03)", startDate: "2026-08-03" },
      { title: "Week 3 (2026-08-10)", startDate: "2026-08-10" },
    ],
  );
});

test("only open issues with the exact curated label are managed", () => {
  const result = filterOpenCuratedIssues([
    { number: 3, state: "OPEN", labels: { nodes: [{ name: "curated" }] } },
    { number: 2, state: "CLOSED", labels: { nodes: [{ name: "curated" }] } },
    { number: 1, state: "OPEN", labels: { nodes: [{ name: "planning" }] } },
  ]);
  assert.deepEqual(
    result.map((issue) => issue.number),
    [3],
  );
});

const fields = [
  {
    id: "status-field",
    name: "Status",
    dataType: "SINGLE_SELECT",
    options: [
      { id: "todo", name: "Todo" },
      { id: "doing", name: "In Progress" },
    ],
  },
  {
    id: "priority-field",
    name: "Priority",
    dataType: "SINGLE_SELECT",
    options: [{ id: "p1", name: "P1" }],
  },
  {
    id: "size-field",
    name: "Size",
    dataType: "SINGLE_SELECT",
    options: [{ id: "m", name: "M" }],
  },
  {
    id: "slot-field",
    name: "Agent Slot",
    dataType: "SINGLE_SELECT",
    options: [{ id: "a", name: "A" }],
  },
  { id: "start-field", name: "Start Date", dataType: "DATE" },
  { id: "target-field", name: "Target Date", dataType: "DATE" },
  {
    id: "iteration-field",
    name: "Iteration",
    dataType: "ITERATION",
    configuration: {
      completedIterations: [],
      iterations: [
        {
          id: "week-1",
          title: "Week 1 (2026-07-27)",
          startDate: "2026-07-27",
          duration: 7,
        },
      ],
    },
  },
];

test("field reconciliation skips values that are already synchronized", () => {
  const scheduleItem = {
    issueNumber: 618,
    status: "In Progress",
    priority: "P1",
    size: "M",
    agentSlot: "A",
    startDate: "2026-07-27",
    targetDate: "2026-07-29",
  };
  const item = {
    fieldValues: {
      nodes: [
        {
          __typename: "ProjectV2ItemFieldSingleSelectValue",
          optionId: "doing",
          name: "In Progress",
          field: { name: "Status" },
        },
        {
          __typename: "ProjectV2ItemFieldDateValue",
          date: "2026-07-27",
          field: { name: "Start Date" },
        },
        {
          __typename: "ProjectV2ItemFieldIterationValue",
          iterationId: "week-1",
          title: "Week 1 (2026-07-27)",
          field: { name: "Iteration" },
        },
      ],
    },
  };
  const result = desiredFieldUpdates(scheduleItem, fields, item);
  assert.deepEqual(
    result.updates.map((update) => update.fieldName),
    ["Priority", "Size", "Agent Slot", "Target Date"],
  );
});

test("reconciliation adds missing curated issues and ignores other issues", () => {
  const schedule = new Map([
    [
      1,
      {
        issueNumber: 1,
        status: "Todo",
        priority: "P1",
        startDate: "2026-07-27",
      },
    ],
    [
      2,
      {
        issueNumber: 2,
        status: "Todo",
      },
    ],
  ]);
  const issues = [
    {
      id: "issue-1",
      number: 1,
      state: "OPEN",
      labels: { nodes: [{ name: "curated" }] },
    },
    {
      id: "issue-2",
      number: 2,
      state: "CLOSED",
      labels: { nodes: [{ name: "curated" }] },
    },
    {
      id: "issue-3",
      number: 3,
      state: "OPEN",
      labels: { nodes: [{ name: "planning" }] },
    },
  ];
  const plan = buildReconciliationPlan({
    repository: "hjosugi/kofun",
    fields,
    projectItems: [],
    issues,
    schedule,
  });
  assert.deepEqual(
    plan.additions.map((issue) => issue.number),
    [1],
  );
  assert.match(plan.warnings.join("\n"), /#2: skipped/);
  assert.doesNotMatch(plan.warnings.join("\n"), /#3/);
});

test("view request uses the documented 2026 user-project REST endpoint", () => {
  const request = buildViewCreateRequest(config, 7, {
    name: "Delivery roadmap",
    layout: "roadmap",
    filter: "is:issue is:open",
  });
  assert.equal(
    request.endpoint,
    "users/hjosugi/projectsV2/7/views",
  );
  assert.ok(
    request.headers.includes("X-GitHub-Api-Version: 2026-03-10"),
  );
  assert.equal(request.fields.layout, "roadmap");
});

test("GraphQL and REST view layouts reconcile to one idempotent key", () => {
  assert.equal(
    projectViewKey({
      name: "Delivery roadmap",
      layout: "ROADMAP_LAYOUT",
    }),
    projectViewKey({
      name: "Delivery roadmap",
      layout: "roadmap",
    }),
  );
  assert.equal(
    projectViewKey({ name: "This week", layout: "BOARD_LAYOUT" }),
    "this week:board",
  );
});

test("Actions apply refuses repository GITHUB_TOKEN without PROJECTS_TOKEN", () => {
  assert.deepEqual(
    tokenGuidance({
      apply: true,
      actions: true,
      projectsTokenConfigured: false,
    }).canContinue,
    false,
  );
  assert.equal(
    tokenGuidance({
      apply: false,
      actions: true,
      projectsTokenConfigured: false,
    }).canContinue,
    true,
  );
});

test("remote guard accepts only this exact GitHub repository", () => {
  assert.equal(
    verifyRepositoryRemote(
      "hjosugi/kofun",
      "git@github.com:hjosugi/kofun.git",
    ),
    "hjosugi/kofun",
  );
  assert.throws(
    () =>
      verifyRepositoryRemote(
        "hjosugi/kofun",
        "https://github.com/hjosugi/another.git",
      ),
    /Remote-write guard/,
  );
});
