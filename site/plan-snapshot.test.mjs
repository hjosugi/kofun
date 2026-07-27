import assert from "node:assert/strict";
import {
  SNAPSHOT_SCHEMA,
  addBusinessDays,
  buildPlanSnapshot,
  dateInTimeZone,
  fetchAllIssues,
  nextBusinessDay,
  parseArgs,
  renderDeliveryPlan,
  semanticSnapshot,
  semanticallyEqual,
} from "./sync-plan.mjs";

function issue(number, options = {}) {
  const labels = options.labels ?? ["curated", "ready", "P1", "size:S"];
  return {
    number,
    title: options.title ?? `Issue ${number}`,
    html_url: `https://github.com/hjosugi/kofun/issues/${number}`,
    state: options.state ?? "open",
    state_reason: options.stateReason ?? null,
    labels: labels.map((name) => ({ name })),
    assignees: options.assignees ?? [],
    created_at: options.createdAt ?? "2026-07-20T00:00:00Z",
    updated_at: options.updatedAt ?? "2026-07-20T00:00:00Z",
    closed_at: options.closedAt ?? null,
    ...(options.pullRequest ? { pull_request: { url: "https://api.example/pr" } } : {}),
  };
}

const fixture = [
  issue(50, { labels: ["planning", "kind:planning", "P2"] }),
  issue(51, { pullRequest: true }),
  issue(52, {
    state: "closed",
    stateReason: "not_planned",
    closedAt: "2026-07-25T00:00:00Z",
  }),
  issue(53, {
    labels: ["curated", "deferred", "P3", "size:M"],
  }),
  issue(54, {
    labels: ["curated", "blocked", "P1", "size:M"],
  }),
  issue(55, {
    labels: ["curated", "ready", "P2", "size:M", "kind:implementation"],
  }),
  issue(618, {
    stateReason: "reopened",
    labels: ["curated", "needs-detail", "P0", "size:L"],
  }),
  issue(622, {
    labels: ["curated", "blocked", "P0", "size:M"],
  }),
  issue(271, {
    labels: ["curated", "blocked", "P0", "size:M"],
  }),
  issue(272, {
    labels: ["curated", "blocked", "P0", "size:M"],
  }),
  issue(274, {
    labels: ["curated", "blocked", "P1", "size:M"],
  }),
  issue(721, {
    labels: ["curated", "in-progress", "P1", "size:M"],
  }),
  issue(722, { labels: ["curated", "P1"] }),
  issue(723, { labels: ["curated", "P1"] }),
  issue(724, { labels: ["curated", "P1"] }),
  issue(725, { labels: ["curated", "blocked", "P2"] }),
  issue(726, { labels: ["curated", "blocked", "P2"] }),
];

const snapshot = buildPlanSnapshot(fixture, {
  asOf: "2026-07-26",
  generatedAt: "2026-07-26T09:00:00.000Z",
  pages: 1,
});

assert.equal(snapshot.schema, SNAPSHOT_SCHEMA);
assert.equal(snapshot.source.api_items, fixture.length);
assert.equal(snapshot.source.pull_requests_excluded, 1);
assert.equal(snapshot.summary.issues_total, fixture.length - 1);
assert.equal(snapshot.summary.open_planning, 1);
assert.equal(snapshot.summary.open_curated, 14);
assert.equal(snapshot.summary.scheduled_curated, 12);
assert.equal(snapshot.summary.unscheduled_curated, 2);
assert.equal(
  snapshot.summary.scheduled_curated + snapshot.summary.unscheduled_curated,
  snapshot.summary.open_curated,
);
assert.deepEqual(snapshot.summary.curated_workflows, {
  blocked: 7,
  deferred: 1,
  "in-progress": 1,
  "needs-detail": 1,
  ready: 1,
  unclassified: 3,
});
assert.deepEqual(snapshot.summary.curated_priorities, {
  P0: 4,
  P1: 6,
  P2: 3,
  P3: 1,
});
assert.deepEqual(snapshot.summary.curated_sizes, {
  L: 1,
  M: 8,
  none: 5,
});
assert.equal(snapshot.summary.state_reasons.not_planned, 1);
assert.equal(snapshot.summary.state_reasons.reopened, 1);
assert.equal(snapshot.issues.find((candidate) => candidate.number === 50).role, "planning");
assert.equal(snapshot.issues.find((candidate) => candidate.number === 618).state_reason, "reopened");
assert.ok(!snapshot.issues.some((candidate) => candidate.number === 51));
assert.deepEqual(
  snapshot.lanes.map((lane) => lane.id),
  ["writer-a", "writer-b", "writer-c", "review"],
);
assert.equal(snapshot.capacity.max_agents, 4);
assert.equal(snapshot.capacity.writer_slots, 3);
assert.equal(snapshot.capacity.reviewer_slots, 1);
assert.equal(snapshot.capacity.wip_limit, 3);

for (const chain of snapshot.dependency_chains) {
  let previous = null;
  for (const issueItem of chain.issues) {
    if (issueItem.missing || issueItem.start_date === null) continue;
    if (previous) {
      assert.ok(
        issueItem.start_date > previous.end_date,
        `#${issueItem.number} must start after #${previous.number} is reviewed`,
      );
    }
    previous = issueItem;
  }
}

for (const task of snapshot.schedule) {
  assert.match(task.start_date, /^\d{4}-\d{2}-\d{2}$/);
  assert.match(task.writer_end_date, /^\d{4}-\d{2}-\d{2}$/);
  assert.match(task.review_start_date, /^\d{4}-\d{2}-\d{2}$/);
  assert.match(task.review_end_date, /^\d{4}-\d{2}-\d{2}$/);
  assert.equal(task.end_date, task.review_end_date);
  assert.ok(task.start_date <= task.writer_end_date);
  assert.ok(task.writer_end_date < task.review_start_date);
}

for (const lane of ["writer-a", "writer-b", "writer-c"]) {
  const tasks = snapshot.schedule
    .filter((task) => task.lane === lane)
    .sort((left, right) => left.start_date.localeCompare(right.start_date));
  for (let index = 1; index < tasks.length; index += 1) {
    assert.ok(
      tasks[index - 1].writer_end_date < tasks[index].start_date,
      `${lane} overlaps #${tasks[index - 1].number} and #${tasks[index].number}`,
    );
  }
}
const reviews = [...snapshot.schedule].sort((left, right) =>
  left.review_start_date.localeCompare(right.review_start_date),
);
for (let index = 1; index < reviews.length; index += 1) {
  assert.ok(
    reviews[index - 1].review_end_date < reviews[index].review_start_date,
    `review overlaps #${reviews[index - 1].number} and #${reviews[index].number}`,
  );
}

for (const week of snapshot.calendar) {
  assert.ok(week.planned_writer_days <= week.writer_capacity_days);
}

for (
  let date = snapshot.forecast.start_date;
  date <= snapshot.forecast.likely_finish;
  date = addBusinessDays(date, 1)
) {
  const activeWriters = snapshot.schedule.filter(
    (task) => task.start_date <= date && task.writer_end_date >= date,
  ).length;
  assert.ok(activeWriters <= 3, `${date} exceeds writer WIP`);
  if (date === snapshot.forecast.likely_finish) break;
}

const shuffled = buildPlanSnapshot([...fixture].reverse(), {
  asOf: "2026-07-26",
  generatedAt: "2027-01-01T00:00:00.000Z",
  pages: 1,
});
assert.ok(semanticallyEqual(snapshot, shuffled));
assert.notEqual(snapshot.generated_at, shuffled.generated_at);
assert.deepEqual(semanticSnapshot(snapshot), semanticSnapshot(shuffled));
const changed = structuredClone(shuffled);
changed.summary.open_issues += 1;
assert.ok(!semanticallyEqual(snapshot, changed));

const markdown = renderDeliveryPlan(snapshot);
assert.match(markdown, /^# Delivery plan/m);
assert.match(markdown, /three writer lanes plus one review\/integration lane/i);
assert.match(markdown, /\[#618\]\(https:\/\/github\.com\/hjosugi\/kofun\/issues\/618\)/);
assert.match(markdown, /Open planning umbrellas \| 1/);
assert.match(markdown, /not_planned/);
assert.deepEqual(
  snapshot.forecast.scenarios.map((scenario) => scenario.id),
  [
    "capacity-plan",
    "conservative-buffer",
    "historical-completion-no-intake",
    "historical-net-burn",
  ],
);

assert.equal(nextBusinessDay("2026-07-26", true), "2026-07-27");
assert.equal(addBusinessDays("2026-07-31", 1), "2026-08-03");
assert.equal(
  dateInTimeZone(new Date("2026-07-26T15:30:00.000Z")),
  "2026-07-27",
);
assert.deepEqual(parseArgs(["--check", "--as-of", "2026-07-26"]), {
  check: true,
  repository: "hjosugi/kofun",
  asOf: "2026-07-26",
  input: null,
  output: new URL("../site/plan-snapshot.json", import.meta.url).pathname,
  markdown: new URL("../docs/DELIVERY_PLAN.md", import.meta.url).pathname,
});

const apiItems = Array.from({ length: 101 }, (_, index) => issue(index + 1000));
apiItems.splice(37, 0, issue(999, { pullRequest: true }));
const requests = [];
const fetched = await fetchAllIssues({
  repository: "hjosugi/kofun",
  token: "test-token",
  fetchImpl: async (url, init) => {
    requests.push({ url, init });
    const page = Number(new URL(url).searchParams.get("page"));
    const items = page === 1 ? apiItems.slice(0, 100) : apiItems.slice(100);
    return {
      ok: true,
      status: 200,
      statusText: "OK",
      headers: new Headers(),
      json: async () => items,
    };
  },
});
assert.equal(fetched.pages, 2);
assert.equal(fetched.items.length, 102);
assert.equal(requests.length, 2);
assert.equal(requests[0].init.headers.Authorization, "Bearer test-token");
assert.equal(new URL(requests[0].url).searchParams.get("per_page"), "100");

console.log(
  `PASS: ${snapshot.summary.scheduled_curated} scheduled issues, ` +
    `${snapshot.calendar.length} calendar weeks, pagination and semantic checks`,
);
