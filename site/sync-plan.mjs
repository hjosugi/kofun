import { mkdir, readFile, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import process from "node:process";
import { fileURLToPath, pathToFileURL } from "node:url";

export const DEFAULT_REPOSITORY = "hjosugi/kofun";
export const DEFAULT_TIME_ZONE = "Asia/Tokyo";
export const SNAPSHOT_SCHEMA = "kofun.delivery-plan/v1";
export const WORKFLOW_LABELS = [
  "in-progress",
  "verification-pending",
  "ready",
  "needs-decision",
  "needs-detail",
  "needs-triage",
  "blocked",
  "deferred",
];
export const DEPENDENCY_CHAINS = [
  {
    id: "self-host-fixed-point",
    title: "Self-host fixed point and independent reproduction",
    lane: "writer-a",
    issue_numbers: [744, 745, 746, 747, 748, 749, 751, 618, 622, 271, 272, 274],
  },
  {
    id: "decimal-delivery",
    title: "Compiler-native Decimal delivery",
    lane: "writer-b",
    issue_numbers: [721, 722, 723, 724, 725, 726],
  },
  {
    id: "optional-frontend",
    title: "Optional frontend, inference, and coalescing",
    lane: "writer-b",
    issue_numbers: [70, 312, 314],
  },
  {
    id: "shadowing-resolution",
    title: "Lexical and pattern-binding shadowing",
    lane: "writer-b",
    issue_numbers: [112, 772],
  },
  {
    id: "laws-and-sequencing",
    title: "Law declarations and composable sequencing",
    lane: "writer-b",
    issue_numbers: [551, 626],
  },
  {
    id: "visibility-surfaces",
    title: "Visibility interfaces, tooling, docs, and LSP",
    lane: "writer-c",
    issue_numbers: [583, 584, 585, 606],
  },
  {
    id: "http-client",
    title: "HTTP client contract and core",
    lane: "writer-c",
    issue_numbers: [638, 644],
  },
  {
    id: "date-time",
    title: "Date/time contract, core, clocks, and time zones",
    lane: "writer-c",
    issue_numbers: [639, 645, 647, 648],
  },
  {
    id: "benchmarking",
    title: "Benchmark contract and report format",
    lane: "writer-c",
    issue_numbers: [640, 646],
  },
  {
    id: "repository-split",
    title: "Repository split and documentation cleanup",
    lane: "writer-c",
    issue_numbers: [768, 767, 769],
  },
  {
    id: "semantic-upgrades",
    title: "Semantic identity and typed upgrade patches",
    lane: "writer-c",
    issue_numbers: [740, 741],
  },
];
export const WORKSTREAMS = [
  {
    id: "bootstrap",
    title: "Self-host & Bootstrap",
    lane: "writer-a",
    areas: ["bootstrap"],
  },
  {
    id: "decimal",
    title: "Decimal",
    lane: "writer-b",
    issue_numbers: [710, 721, 722, 723, 724, 725, 726],
  },
  {
    id: "frontend-types",
    title: "Frontend & Types",
    lane: "writer-b",
    areas: ["parser", "syntax", "types", "inference", "type-system", "laws", "meta"],
  },
  {
    id: "backend-runtime",
    title: "Backend & Runtime",
    lane: "writer-a",
    areas: ["codegen", "memory", "platforms", "ownership", "collections"],
  },
  {
    id: "effects-concurrency",
    title: "Effects & Concurrency",
    lane: "writer-b",
    areas: ["effects", "concurrency"],
  },
  {
    id: "tooling-diagnostics",
    title: "Tooling & Diagnostics",
    lane: "writer-c",
    areas: ["compiler", "developer_tools", "diagnostics", "tooling"],
  },
  {
    id: "stdlib-frameworks",
    title: "Stdlib & Frameworks",
    lane: "writer-c",
    areas: ["stdlib", "frameworks"],
  },
  {
    id: "repository-docs",
    title: "Repository & Docs",
    lane: "writer-c",
    areas: ["build-system", "docs_tools", "ecosystem", "quality"],
  },
  {
    id: "science-future",
    title: "Science & Future",
    lane: "writer-a",
    areas: ["science"],
  },
  {
    id: "research-positioning",
    title: "Research & Positioning",
    lane: "writer-c",
    issue_numbers: [281],
  },
];
export const LANES = [
  { id: "writer-a", label: "Writer A · compiler core", role: "writer", capacity: 1 },
  { id: "writer-b", label: "Writer B · language semantics", role: "writer", capacity: 1 },
  { id: "writer-c", label: "Writer C · tools and ecosystem", role: "writer", capacity: 1 },
  { id: "review", label: "Reviewer · integration", role: "reviewer", capacity: 1 },
];

const DEFAULT_SNAPSHOT_PATH = new URL(
  "../site/plan-snapshot.json",
  import.meta.url,
);
const DEFAULT_MARKDOWN_PATH = new URL("../docs/DELIVERY_PLAN.md", import.meta.url);
const DAY_MS = 24 * 60 * 60 * 1000;
const PRIORITY_RANK = new Map([
  ["P0", 0],
  ["P1", 1],
  ["P2", 2],
  ["P3", 3],
  [null, 4],
]);
const WORKFLOW_RANK = new Map([
  ["in-progress", 0],
  ["verification-pending", 1],
  ["ready", 2],
  ["needs-decision", 3],
  ["needs-detail", 4],
  ["needs-triage", 5],
  ["unclassified", 6],
  ["blocked", 7],
  ["deferred", 8],
]);
const SIZE_DAYS = {
  S: { writer: 2, review: 1 },
  M: { writer: 4, review: 1 },
  L: { writer: 8, review: 2 },
  unknown: { writer: 5, review: 1 },
};

function labelName(label) {
  return typeof label === "string" ? label : label?.name;
}

function compareText(left, right) {
  return left < right ? -1 : left > right ? 1 : 0;
}

function findPrefixed(labels, prefix) {
  const value = labels.find((label) => label.startsWith(prefix));
  return value ? value.slice(prefix.length) : null;
}

function countBy(values) {
  return Object.fromEntries(
    [...values.reduce((counts, value) => {
      const key = value ?? "none";
      counts.set(key, (counts.get(key) ?? 0) + 1);
      return counts;
    }, new Map())].sort(([left], [right]) => compareText(left, right)),
  );
}

function workstreamFor(issue) {
  return (
    WORKSTREAMS.find(
      (workstream) =>
        workstream.issue_numbers?.includes(issue.number) ||
        workstream.areas?.includes(issue.area),
    ) ?? WORKSTREAMS.at(-1)
  );
}

export function normalizeIssue(issue, repository = DEFAULT_REPOSITORY) {
  const labels = (issue.labels ?? [])
    .map(labelName)
    .filter(Boolean)
    .sort(compareText);
  const role = labels.includes("planning")
    ? "planning"
    : labels.includes("curated")
      ? "curated"
      : "unclassified";
  const workflow =
    issue.state === "closed"
      ? "closed"
      : (WORKFLOW_LABELS.find((label) => labels.includes(label)) ??
        "unclassified");
  const areas = labels
    .filter((label) => label.startsWith("area:"))
    .map((label) => label.slice("area:".length));
  const workstream = workstreamFor({
    number: issue.number,
    area: areas[0] ?? null,
  });

  return {
    number: issue.number,
    title: issue.title,
    url:
      issue.html_url ??
      `https://github.com/${repository}/issues/${issue.number}`,
    state: issue.state,
    state_reason: issue.state_reason ?? null,
    role,
    workflow,
    priority: labels.find((label) => /^P[0-3]$/.test(label)) ?? null,
    size: findPrefixed(labels, "size:"),
    kind: findPrefixed(labels, "kind:"),
    area: areas[0] ?? null,
    areas,
    workstream: workstream.title,
    labels,
    assignees: (issue.assignees ?? [])
      .map((assignee) =>
        typeof assignee === "string" ? assignee : assignee.login,
      )
      .filter(Boolean)
      .sort(compareText),
    created_at: issue.created_at ?? null,
    updated_at: issue.updated_at ?? null,
    closed_at: issue.closed_at ?? null,
  };
}

export function isoDate(value) {
  const date = value instanceof Date ? value : new Date(value);
  if (Number.isNaN(date.getTime())) {
    throw new Error(`Invalid date: ${value}`);
  }
  return date.toISOString().slice(0, 10);
}

export function dateInTimeZone(
  value = new Date(),
  timeZone = DEFAULT_TIME_ZONE,
) {
  const parts = new Intl.DateTimeFormat("en", {
    day: "2-digit",
    month: "2-digit",
    timeZone,
    year: "numeric",
  })
    .formatToParts(value)
    .filter((part) => part.type !== "literal");
  const fields = Object.fromEntries(
    parts.map((part) => [part.type, part.value]),
  );
  return `${fields.year}-${fields.month}-${fields.day}`;
}

function dateFromIso(value) {
  return new Date(`${value}T00:00:00.000Z`);
}

function shiftCalendarDays(value, days) {
  const date = dateFromIso(value);
  date.setUTCDate(date.getUTCDate() + days);
  return isoDate(date);
}

export function isBusinessDay(value) {
  const day = dateFromIso(isoDate(value)).getUTCDay();
  return day !== 0 && day !== 6;
}

export function nextBusinessDay(value, includeCurrent = false) {
  let date = isoDate(value);
  if (!includeCurrent) date = shiftCalendarDays(date, 1);
  while (!isBusinessDay(date)) date = shiftCalendarDays(date, 1);
  return date;
}

export function addBusinessDays(value, days) {
  if (!Number.isInteger(days) || days < 0) {
    throw new Error(`Business-day offset must be a non-negative integer: ${days}`);
  }
  let result = isoDate(value);
  for (let remaining = days; remaining > 0; remaining -= 1) {
    result = nextBusinessDay(result);
  }
  return result;
}

function businessDaysInclusive(start, end) {
  let count = 0;
  for (
    let cursor = isoDate(start);
    cursor <= isoDate(end);
    cursor = shiftCalendarDays(cursor, 1)
  ) {
    if (isBusinessDay(cursor)) count += 1;
  }
  return count;
}

function mondayOf(value) {
  const date = dateFromIso(value);
  const day = date.getUTCDay();
  date.setUTCDate(date.getUTCDate() - (day === 0 ? 6 : day - 1));
  return isoDate(date);
}

function durationFor(issue) {
  const base = SIZE_DAYS[issue.size] ?? SIZE_DAYS.unknown;
  let writer = base.writer;
  if (issue.workflow === "verification-pending") writer = 1;
  if (issue.workflow === "needs-detail") writer += 2;
  if (issue.workflow === "needs-decision") writer += 3;
  if (issue.workflow === "needs-triage") writer += 3;
  return { writer, review: base.review };
}

function chainMembership(openIssues) {
  const openNumbers = new Set(openIssues.map((issue) => issue.number));
  const membership = new Map();
  for (const chain of DEPENDENCY_CHAINS) {
    let previousOpen = null;
    for (const number of chain.issue_numbers) {
      if (!openNumbers.has(number)) continue;
      membership.set(number, {
        chain_id: chain.id,
        lane: chain.lane,
        depends_on: previousOpen === null ? [] : [previousOpen],
      });
      previousOpen = number;
    }
  }
  return membership;
}

function taskRank(left, right) {
  const leftChain = left.chain_id ? 0 : 1;
  const rightChain = right.chain_id ? 0 : 1;
  return (
    leftChain - rightChain ||
    (PRIORITY_RANK.get(left.priority) ?? 4) -
      (PRIORITY_RANK.get(right.priority) ?? 4) ||
    (WORKFLOW_RANK.get(left.workflow) ?? 6) -
      (WORKFLOW_RANK.get(right.workflow) ?? 6) ||
    left.number - right.number
  );
}

function scheduleConfidence(issue, isChain) {
  if (issue.workflow === "blocked") return "conditional";
  if (issue.workflow === "deferred") return "low";
  if (
    issue.workflow === "needs-detail" ||
    issue.workflow === "needs-decision" ||
    issue.workflow === "needs-triage" ||
    issue.size === "L" ||
    issue.size === null
  ) {
    return "low";
  }
  return isChain ? "medium" : "medium";
}

export function scheduleCuratedIssues(openCurated, options = {}) {
  const startDate = nextBusinessDay(options.startDate ?? new Date(), true);
  const membership = chainMembership(openCurated);
  const scheduledTasks = [];

  for (const issue of openCurated) {
    const chain = membership.get(issue.number);
    const workstream = workstreamFor(issue);
    const duration = durationFor(issue);
    scheduledTasks.push({
      ...issue,
      chain_id: chain?.chain_id ?? null,
      preferred_lane: chain?.lane ?? workstream.lane,
      depends_on: chain?.depends_on ?? [],
      writer_days: duration.writer,
      review_days: duration.review,
      confidence: scheduleConfidence(issue, Boolean(chain)),
    });
  }

  scheduledTasks.sort(taskRank);
  const byNumber = new Map(scheduledTasks.map((task) => [task.number, task]));
  const completed = new Set();
  const waitingReview = [];
  const lanes = new Map(
    LANES.filter((lane) => lane.role === "writer").map((lane) => [
      lane.id,
      { task: null },
    ]),
  );
  let reviewer = null;
  let cursor = startDate;
  let completedCount = 0;
  let elapsedCalendarDays = 0;

  function chainStillActive(laneId) {
    return scheduledTasks.some(
      (task) =>
        task.preferred_lane === laneId &&
        !completed.has(task.number) &&
        !task.review_end_date,
    );
  }

  function eligibleForLane(task, laneId) {
    if (task.start_date || waitingReview.includes(task) || task.review_start_date) {
      return false;
    }
    if (!task.depends_on.every((number) => completed.has(number))) return false;
    if (task.preferred_lane) return task.preferred_lane === laneId;
    if (chainStillActive(laneId)) return false;
    return true;
  }

  while (completedCount < scheduledTasks.length) {
    if (elapsedCalendarDays > 3650) {
      throw new Error("Schedule simulation exceeded ten calendar years");
    }
    if (!isBusinessDay(cursor)) {
      cursor = nextBusinessDay(cursor);
      elapsedCalendarDays += 1;
      continue;
    }

    if (!reviewer && waitingReview.length > 0) {
      waitingReview.sort(taskRank);
      reviewer = waitingReview.shift();
      reviewer.review_start_date = cursor;
      reviewer.review_remaining = reviewer.review_days;
    }

    for (const [laneId, state] of lanes) {
      if (state.task) continue;
      const task = scheduledTasks.find((candidate) =>
        eligibleForLane(candidate, laneId),
      );
      if (!task) continue;
      task.lane = laneId;
      task.start_date = cursor;
      task.writer_remaining = task.writer_days;
      state.task = task;
    }

    for (const state of lanes.values()) {
      if (!state.task) continue;
      state.task.writer_remaining -= 1;
      if (state.task.writer_remaining === 0) {
        state.task.writer_end_date = cursor;
        waitingReview.push(state.task);
        delete state.task.writer_remaining;
        state.task = null;
      }
    }

    if (reviewer) {
      reviewer.review_remaining -= 1;
      if (reviewer.review_remaining === 0) {
        reviewer.review_end_date = cursor;
        reviewer.end_date = cursor;
        delete reviewer.review_remaining;
        completed.add(reviewer.number);
        completedCount += 1;
        reviewer = null;
      }
    }

    cursor = nextBusinessDay(cursor);
    elapsedCalendarDays += 1;
  }

  const schedule = scheduledTasks
    .map((task) => {
      const {
        preferred_lane: _preferredLane,
        review_days: _reviewDays,
        ...result
      } = task;
      return {
        ...result,
        schedule_status:
          task.workflow === "blocked" || task.workflow === "deferred"
            ? "conditional"
            : "scheduled",
      };
    })
    .sort(
      (left, right) =>
        compareText(left.start_date, right.start_date) ||
        compareText(left.lane, right.lane) ||
        left.number - right.number,
    );

  return {
    start_date: startDate,
    finish_date:
      schedule.length > 0
        ? schedule.reduce(
            (latest, task) =>
              task.end_date > latest ? task.end_date : latest,
            schedule[0].end_date,
          )
        : startDate,
    schedule,
    unscheduled: [],
  };
}

export function computeThroughput(issues, asOf, windowDays = 14) {
  const end = new Date(`${isoDate(asOf)}T23:59:59.999Z`);
  const start = new Date(end.getTime() - windowDays * DAY_MS);
  const curated = issues.filter((issue) => issue.role === "curated");
  const created = curated.filter((issue) => {
    const timestamp = Date.parse(issue.created_at);
    return timestamp > start.getTime() && timestamp <= end.getTime();
  }).length;
  const completed = curated.filter((issue) => {
    const timestamp = Date.parse(issue.closed_at);
    return (
      issue.state === "closed" &&
      issue.state_reason !== "not_planned" &&
      timestamp > start.getTime() &&
      timestamp <= end.getTime()
    );
  }).length;
  const notPlanned = curated.filter((issue) => {
    const timestamp = Date.parse(issue.closed_at);
    return (
      issue.state === "closed" &&
      issue.state_reason === "not_planned" &&
      timestamp > start.getTime() &&
      timestamp <= end.getTime()
    );
  }).length;
  const weeks = windowDays / 7;
  const completionRate = Number((completed / weeks).toFixed(2));
  const arrivalRate = Number((created / weeks).toFixed(2));

  return {
    window_days: windowDays,
    window_start: isoDate(start),
    window_end: isoDate(end),
    created_curated: created,
    completed_curated: completed,
    closed_not_planned: notPlanned,
    completion_rate_per_week: completionRate,
    arrival_rate_per_week: arrivalRate,
    net_burn_per_week: Number((completionRate - arrivalRate).toFixed(2)),
    note:
      "Completion excludes issues closed as not planned; intake uses curated issue creation dates.",
  };
}

function buildCalendar(schedule, startDate, finishDate) {
  if (schedule.length === 0) return [];
  const calendar = [];
  for (
    let weekStart = mondayOf(startDate);
    weekStart <= finishDate;
    weekStart = shiftCalendarDays(weekStart, 7)
  ) {
    const weekEnd = shiftCalendarDays(weekStart, 6);
    const active = schedule.filter(
      (task) => task.start_date <= weekEnd && task.end_date >= weekStart,
    );
    const implementation = active.filter(
      (task) =>
        task.start_date <= weekEnd && task.writer_end_date >= weekStart,
    );
    const review = active.filter(
      (task) =>
        task.review_start_date <= weekEnd && task.review_end_date >= weekStart,
    );
    calendar.push({
      week_start: weekStart,
      week_end: weekEnd,
      active_issue_numbers: active.map((task) => task.number).sort((a, b) => a - b),
      starting_issue_numbers: active
        .filter((task) => task.start_date >= weekStart && task.start_date <= weekEnd)
        .map((task) => task.number)
        .sort((a, b) => a - b),
      finishing_issue_numbers: active
        .filter((task) => task.end_date >= weekStart && task.end_date <= weekEnd)
        .map((task) => task.number)
        .sort((a, b) => a - b),
      review_issue_numbers: review
        .map((task) => task.number)
        .sort((a, b) => a - b),
      planned_writer_days: implementation.reduce((days, task) => {
        const overlapStart =
          task.start_date > weekStart ? task.start_date : weekStart;
        const overlapEnd =
          task.writer_end_date < weekEnd ? task.writer_end_date : weekEnd;
        return days + businessDaysInclusive(overlapStart, overlapEnd);
      }, 0),
      writer_capacity_days: businessDaysInclusive(weekStart, weekEnd) * 3,
    });
  }
  return calendar;
}

function addBuffer(finishDate, startDate, ratio) {
  const workdays = businessDaysInclusive(startDate, finishDate);
  return addBusinessDays(finishDate, Math.ceil(workdays * ratio));
}

function historicalFinish(asOf, scopeCount, throughput) {
  if (throughput.net_burn_per_week <= 0) return null;
  const weeks = scopeCount / throughput.net_burn_per_week;
  return addBusinessDays(
    nextBusinessDay(asOf, true),
    Math.ceil(weeks * 5),
  );
}

function issueSummary(issue) {
  return {
    number: issue.number,
    title: issue.title,
    url: issue.url,
    role: issue.role,
    workflow: issue.workflow,
    state_reason: issue.state_reason,
    priority: issue.priority,
    size: issue.size,
    kind: issue.kind,
    area: issue.area,
    workstream: issue.workstream,
    labels: issue.labels,
  };
}

export function buildPlanSnapshot(rawItems, options = {}) {
  const repository = options.repository ?? DEFAULT_REPOSITORY;
  const timeZone = options.timeZone ?? DEFAULT_TIME_ZONE;
  const asOf =
    options.asOf === undefined
      ? dateInTimeZone(new Date(), timeZone)
      : isoDate(options.asOf);
  const generatedAt =
    options.generatedAt ??
    (options.asOf instanceof Date
      ? options.asOf.toISOString()
      : new Date().toISOString());
  const pullRequests = rawItems.filter((item) => item.pull_request);
  const issues = rawItems
    .filter((item) => !item.pull_request)
    .map((issue) => normalizeIssue(issue, repository))
    .sort((left, right) => left.number - right.number);
  const open = issues.filter((issue) => issue.state === "open");
  const closed = issues.filter((issue) => issue.state === "closed");
  const openCurated = open.filter((issue) => issue.role === "curated");
  const openPlanning = open.filter((issue) => issue.role === "planning");
  const throughput = computeThroughput(issues, asOf, options.windowDays ?? 14);
  const planned = scheduleCuratedIssues(openCurated, { startDate: asOf });
  const likelyFinish = planned.finish_date;
  const conservativeFinish = addBuffer(
    likelyFinish,
    planned.start_date,
    0.25,
  );
  const historical = historicalFinish(
    asOf,
    planned.schedule.length,
    throughput,
  );
  const workstreams = WORKSTREAMS.map((definition) => {
    const tasks = planned.schedule.filter(
      (task) => task.workstream === definition.title,
    );
    return {
      id: definition.id,
      title: definition.title,
      lane: definition.lane,
      issue_count: tasks.length,
      issue_numbers: tasks.map((task) => task.number),
      start_date: tasks[0]?.start_date ?? null,
      end_date:
        tasks.length > 0
          ? tasks.reduce(
              (latest, task) =>
                task.end_date > latest ? task.end_date : latest,
              tasks[0].end_date,
            )
          : null,
    };
  }).filter((workstream) => workstream.issue_count > 0);

  const dependencyChains = DEPENDENCY_CHAINS.map((chain) => ({
    ...chain,
    issues: chain.issue_numbers.map((number) => {
      const issue = issues.find((candidate) => candidate.number === number);
      const scheduled = planned.schedule.find(
        (candidate) => candidate.number === number,
      );
      return issue
        ? {
            ...issueSummary(issue),
            start_date: scheduled?.start_date ?? null,
            end_date: scheduled?.end_date ?? null,
          }
        : { number, missing: true };
    }),
  }));

  return {
    schema: SNAPSHOT_SCHEMA,
    repository,
    time_zone: timeZone,
    as_of: asOf,
    generated_at: generatedAt,
    source: {
      endpoint: `https://api.github.com/repos/${repository}/issues`,
      api_items: rawItems.length,
      issues: issues.length,
      pull_requests_excluded: pullRequests.length,
      pages: options.pages ?? null,
    },
    capacity: {
      max_agents: 4,
      writer_slots: 3,
      reviewer_slots: 1,
      wip_limit: 3,
      workweek: ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday"],
      policy:
        "Related issues stay on one workstream lane while independent workstreams run in parallel; one agent owns review, integration, and CI.",
    },
    summary: {
      issues_total: issues.length,
      open_issues: open.length,
      closed_issues: closed.length,
      open_curated: openCurated.length,
      open_planning: openPlanning.length,
      open_unclassified: open.filter(
        (issue) => issue.role === "unclassified",
      ).length,
      scheduled_curated: planned.schedule.length,
      unscheduled_curated: planned.unscheduled.length,
      open_roles: countBy(open.map((issue) => issue.role)),
      open_workflows: countBy(open.map((issue) => issue.workflow)),
      open_priorities: countBy(open.map((issue) => issue.priority)),
      open_sizes: countBy(open.map((issue) => issue.size)),
      curated_workflows: countBy(
        openCurated.map((issue) => issue.workflow),
      ),
      curated_priorities: countBy(
        openCurated.map((issue) => issue.priority),
      ),
      curated_sizes: countBy(openCurated.map((issue) => issue.size)),
      state_reasons: countBy(issues.map((issue) => issue.state_reason)),
    },
    throughput,
    forecast: {
      scope:
        "All open curated issues. Blocked and deferred items receive low-confidence placeholder dates so the Gantt remains complete.",
      scheduled_count: planned.schedule.length,
      unscheduled_count: planned.unscheduled.length,
      start_date: planned.start_date,
      likely_finish: likelyFinish,
      conservative_finish: conservativeFinish,
      confidence: "low",
      assumptions: [
        "No new issues enter the scheduled scope.",
        "Three writer lanes and one review/integration lane are continuously available on business days.",
        "Known dependency chains are serial; unrelated workstreams run in parallel on stable lanes to reduce file collisions.",
        "Blocked and deferred items have rough placeholder dates only; their tracker state remains authoritative.",
        "S/M/L use 2/4/8 writer days; unknown size uses 5; refinement and decision states receive extra time.",
        "The calendar models weekdays only; public holidays, leave, incidents, and new intake are outside the lane simulation.",
        "The conservative date adds a 25% business-day buffer after the simulated review bottleneck.",
      ],
      scenarios: [
        {
          id: "capacity-plan",
          label: "4-agent capacity plan",
          writer_slots: 3,
          reviewer_slots: 1,
          finish_date: likelyFinish,
          calendar_days:
            Math.round(
              (dateFromIso(likelyFinish) - dateFromIso(planned.start_date)) /
                DAY_MS,
            ) + 1,
          note: "Deterministic lane simulation with one serial reviewer.",
        },
        {
          id: "conservative-buffer",
          label: "Conservative +25% buffer",
          writer_slots: 3,
          reviewer_slots: 1,
          finish_date: conservativeFinish,
          calendar_days:
            Math.round(
              (dateFromIso(conservativeFinish) -
                dateFromIso(planned.start_date)) /
                DAY_MS,
            ) + 1,
          note: "Allows for refinement, rework, and integration variance.",
        },
        {
          id: "historical-completion-no-intake",
          label: "Observed completion pace, intake frozen",
          writer_slots: null,
          reviewer_slots: null,
          finish_date:
            throughput.completion_rate_per_week > 0
              ? addBusinessDays(
                  planned.start_date,
                  Math.ceil(
                    (planned.schedule.length /
                      throughput.completion_rate_per_week) *
                      5,
                  ),
                )
              : null,
          calendar_days:
            throughput.completion_rate_per_week > 0
              ? Math.round(
                  (dateFromIso(
                    addBusinessDays(
                      planned.start_date,
                      Math.ceil(
                        (planned.schedule.length /
                          throughput.completion_rate_per_week) *
                          5,
                      ),
                    ),
                  ) -
                    dateFromIso(planned.start_date)) /
                    DAY_MS,
                ) + 1
              : null,
          note:
            throughput.completion_rate_per_week > 0
              ? "Optimistic extrapolation; it freezes intake and may include retrospective tracker closure."
              : "No completed curated issues in the observation window.",
        },
        {
          id: "historical-net-burn",
          label: "Observed net issue burn",
          writer_slots: null,
          reviewer_slots: null,
          finish_date: historical,
          calendar_days:
            historical === null
              ? null
              : Math.round(
                  (dateFromIso(historical) - dateFromIso(planned.start_date)) /
                    DAY_MS,
                ) + 1,
          note:
            historical === null
              ? "No finish date: curated intake equals or exceeds completed work in the observation window."
              : "Projection from completed curated issues minus new curated issues; not a capacity commitment.",
        },
      ],
    },
    lanes: LANES,
    workstreams,
    dependency_chains: dependencyChains,
    schedule: planned.schedule,
    unscheduled: planned.unscheduled.map((issue) => ({
      ...issueSummary(issue),
      reason: issue.reason,
    })),
    calendar: buildCalendar(
      planned.schedule,
      planned.start_date,
      planned.finish_date,
    ),
    issues,
  };
}

function markdownCell(value) {
  return String(value ?? "—")
    .replaceAll("|", "\\|")
    .replaceAll("\n", " ");
}

function issueLink(issue) {
  return `[#${issue.number}](${issue.url})`;
}

export function renderDeliveryPlan(snapshot) {
  const scenarioRows = snapshot.forecast.scenarios
    .map(
      (scenario) =>
        `| ${markdownCell(scenario.label)} | ${markdownCell(scenario.finish_date)} | ${markdownCell(scenario.note)} |`,
    )
    .join("\n");
  const workstreamRows = snapshot.workstreams
    .map(
      (workstream) =>
        `| ${markdownCell(workstream.title)} | \`${workstream.lane}\` | ` +
        `${workstream.issue_count} | ${workstream.start_date} | ${workstream.end_date} | ` +
        `${workstream.issue_numbers.map((number) => `#${number}`).join(", ")} |`,
    )
    .join("\n");
  const chainSections = snapshot.dependency_chains
    .map((chain) => {
      const rows = chain.issues
        .map((issue) => {
          if (issue.missing) {
            return `| #${issue.number} | missing | — | — | — |`;
          }
          return (
            `| ${issueLink(issue)} | ${markdownCell(issue.workflow)} | ` +
            `${markdownCell(issue.priority)} | ${markdownCell(issue.start_date)} | ` +
            `${markdownCell(issue.end_date)} |`
          );
        })
        .join("\n");
      return `### ${chain.title}

Lane: \`${chain.lane}\`

| Issue | Tracker state | Priority | Start | Delivered |
|---|---|---|---|---|
${rows}`;
    })
    .join("\n\n");
  const scheduleRows = snapshot.schedule
    .map(
      (issue) =>
        `| ${issueLink(issue)} | ${markdownCell(issue.workstream)} | \`${issue.lane}\` | ${markdownCell(issue.workflow)} | ` +
        `${markdownCell(issue.priority)} | ${markdownCell(issue.size)} | ` +
        `${issue.start_date} | ${issue.writer_end_date} | ${issue.review_end_date} | ` +
        `${markdownCell(issue.confidence)} | ${markdownCell(issue.title)} |`,
    )
    .join("\n");
  const unscheduledRows =
    snapshot.unscheduled.length === 0
      ? "| — | — | — |"
      : snapshot.unscheduled
          .map(
            (issue) =>
              `| ${issueLink(issue)} | ${markdownCell(issue.workflow)} | ${markdownCell(issue.reason)} |`,
          )
          .join("\n");
  const calendarRows = snapshot.calendar
    .map(
      (week) =>
        `| ${week.week_start} | ${week.active_issue_numbers.map((number) => `#${number}`).join(", ") || "—"} | ` +
        `${week.finishing_issue_numbers.map((number) => `#${number}`).join(", ") || "—"} | ` +
        `${week.planned_writer_days}/${week.writer_capacity_days} |`,
    )
    .join("\n");

  return `# Delivery plan

Status: generated read-only planning snapshot. This is a capacity forecast, not
a promise that unresolved, blocked, or deferred work will complete by a date.

Repository: [\`${snapshot.repository}\`](https://github.com/${snapshot.repository})

As of: \`${snapshot.as_of}\`

## Capacity and scope

- Maximum four agents: three writer lanes plus one review/integration lane.
- Work in progress is capped at three implementation issues.
- Planning umbrellas are counted but never scheduled as implementation work.
- All \`${snapshot.summary.scheduled_curated}\` open curated issues have rough
  Gantt dates; blocked and deferred dates are explicitly low-confidence.

| Metric | Count |
|---|---:|
| All issues | ${snapshot.summary.issues_total} |
| Open issues | ${snapshot.summary.open_issues} |
| Open curated | ${snapshot.summary.open_curated} |
| Open planning umbrellas | ${snapshot.summary.open_planning} |
| Scheduled curated | ${snapshot.summary.scheduled_curated} |
| Unscheduled curated | ${snapshot.summary.unscheduled_curated} |

## Forecast

| Scenario | Finish | Interpretation |
|---|---|---|
${scenarioRows}

Confidence is **${snapshot.forecast.confidence}** until missing sizes, refinement
states, external blockers, and future intake are resolved. Over the trailing
\`${snapshot.throughput.window_days}\` days, curated intake was
\`${snapshot.throughput.arrival_rate_per_week}\`/week and completion was
\`${snapshot.throughput.completion_rate_per_week}\`/week; issues closed as
\`not_planned\` are excluded from completion throughput.

## Parallel workstreams

Issues in the same workstream stay on one primary writer lane so they can share
context and avoid cross-worktree collisions. Different lanes run in parallel.

| Workstream | Primary lane | Issues | Start | Rough finish | Grouped issue numbers |
|---|---|---:|---|---|---|
${workstreamRows}

## Critical dependency chains

${chainSections}

## Scheduled curated work

Writer end is implementation complete; delivered is after the single reviewer
lane finishes integration.

| Issue | Workstream | Lane | State | Priority | Size | Start | Writer end | Delivered | Confidence | Title |
|---|---|---|---|---|---|---|---|---|---|---|
${scheduleRows}

## Weekly calendar

| Week of | Active issues | Delivered | Writer load |
|---|---|---|---:|
${calendarRows}

## Unscheduled exceptions

Every open curated issue receives a rough date. This table is therefore normally
empty; planning umbrellas remain intentionally outside the execution Gantt.

| Issue | State | Reason |
|---|---|---|
${unscheduledRows}

## Assumptions

${snapshot.forecast.assumptions.map((assumption) => `- ${assumption}`).join("\n")}

Regenerate this document and
\`site/plan-snapshot.json\` with \`node site/sync-plan.mjs\`. Use
\`node site/sync-plan.mjs --check\` in CI to detect semantic drift without
rewriting files.
`;
}

function canonicalize(value, ignoredKeys) {
  if (Array.isArray(value)) {
    return value.map((item) => canonicalize(item, ignoredKeys));
  }
  if (value && typeof value === "object") {
    return Object.fromEntries(
      Object.entries(value)
        .filter(([key]) => !ignoredKeys.has(key))
        .sort(([left], [right]) => compareText(left, right))
        .map(([key, item]) => [key, canonicalize(item, ignoredKeys)]),
    );
  }
  return value;
}

export function semanticSnapshot(snapshot) {
  return canonicalize(snapshot, new Set(["generated_at"]));
}

export function semanticallyEqual(left, right) {
  return (
    JSON.stringify(semanticSnapshot(left)) ===
    JSON.stringify(semanticSnapshot(right))
  );
}

export async function fetchAllIssues(options = {}) {
  const repository = options.repository ?? DEFAULT_REPOSITORY;
  const fetchImpl = options.fetchImpl ?? fetch;
  const headers = {
    Accept: "application/vnd.github+json",
    "User-Agent": "kofun-delivery-plan-sync",
    "X-GitHub-Api-Version": "2022-11-28",
  };
  const token = options.token ?? process.env.GITHUB_TOKEN;
  if (token) headers.Authorization = `Bearer ${token}`;

  const items = [];
  let page = 1;
  for (;;) {
    const url =
      `https://api.github.com/repos/${repository}/issues?` +
      new URLSearchParams({
        state: "all",
        sort: "created",
        direction: "asc",
        per_page: "100",
        page: String(page),
      });
    const response = await fetchImpl(url, { headers });
    if (!response.ok) {
      const remaining = response.headers?.get?.("x-ratelimit-remaining");
      const suffix =
        remaining === "0"
          ? " (rate limit exhausted; provide GITHUB_TOKEN)"
          : "";
      throw new Error(
        `GitHub API ${response.status} ${response.statusText} on page ${page}${suffix}`,
      );
    }
    const batch = await response.json();
    if (!Array.isArray(batch)) {
      throw new Error(`GitHub API returned a non-array on page ${page}`);
    }
    items.push(...batch);
    if (batch.length < 100) break;
    page += 1;
  }
  return { items, pages: page };
}

export function parseArgs(argv) {
  const result = {
    check: false,
    repository: DEFAULT_REPOSITORY,
    asOf: null,
    input: null,
    output: fileURLToPath(DEFAULT_SNAPSHOT_PATH),
    markdown: fileURLToPath(DEFAULT_MARKDOWN_PATH),
  };
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--check") {
      result.check = true;
      continue;
    }
    const key = argument.startsWith("--") ? argument.slice(2) : null;
    if (!["repository", "as-of", "input", "output", "markdown"].includes(key)) {
      throw new Error(`Unknown argument: ${argument}`);
    }
    const value = argv[index + 1];
    if (!value || value.startsWith("--")) {
      throw new Error(`Missing value for ${argument}`);
    }
    index += 1;
    if (key === "as-of") result.asOf = isoDate(value);
    else if (key === "repository") result.repository = value;
    else result[key] = resolve(value);
  }
  return result;
}

async function readJsonIfPresent(path) {
  try {
    return JSON.parse(await readFile(path, "utf8"));
  } catch (error) {
    if (error.code === "ENOENT") return null;
    throw error;
  }
}

async function readTextIfPresent(path) {
  try {
    return await readFile(path, "utf8");
  } catch (error) {
    if (error.code === "ENOENT") return null;
    throw error;
  }
}

export async function main(argv = process.argv.slice(2)) {
  const args = parseArgs(argv);
  let items;
  let pages = null;
  if (args.input) {
    const fixture = JSON.parse(await readFile(args.input, "utf8"));
    items = Array.isArray(fixture) ? fixture : fixture.items;
    if (!Array.isArray(items)) {
      throw new Error("--input must contain an array or an object with items");
    }
    pages = 0;
  } else {
    const fetched = await fetchAllIssues({ repository: args.repository });
    items = fetched.items;
    pages = fetched.pages;
  }

  const snapshot = buildPlanSnapshot(items, {
    repository: args.repository,
    asOf: args.asOf ?? new Date(),
    pages,
  });
  const markdown = renderDeliveryPlan(snapshot);
  const [previousSnapshot, previousMarkdown] = await Promise.all([
    readJsonIfPresent(args.output),
    readTextIfPresent(args.markdown),
  ]);
  const stale =
    previousSnapshot === null ||
    !semanticallyEqual(previousSnapshot, snapshot) ||
    previousMarkdown !== markdown;

  if (!stale) {
    console.log(
      `UNCHANGED: ${snapshot.summary.open_issues} open issues, ` +
        `${snapshot.summary.scheduled_curated} scheduled`,
    );
    return 0;
  }
  if (args.check) {
    console.error(
      "STALE: run node site/sync-plan.mjs and review the generated plan diff",
    );
    return 1;
  }

  await Promise.all([
    mkdir(dirname(args.output), { recursive: true }),
    mkdir(dirname(args.markdown), { recursive: true }),
  ]);
  await Promise.all([
    writeFile(args.output, `${JSON.stringify(snapshot, null, 2)}\n`),
    writeFile(args.markdown, markdown),
  ]);
  console.log(
    `UPDATED: ${snapshot.summary.open_issues} open issues; ` +
      `${snapshot.summary.scheduled_curated} scheduled through ` +
      `${snapshot.forecast.likely_finish}`,
  );
  return 0;
}

const entryPoint = process.argv[1]
  ? pathToFileURL(resolve(process.argv[1])).href
  : null;
if (entryPoint === import.meta.url) {
  process.exitCode = await main();
}
