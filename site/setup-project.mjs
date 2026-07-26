#!/usr/bin/env node

import { spawnSync } from "node:child_process";
import { readFile } from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import { fileURLToPath, pathToFileURL } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, "..");
const defaultConfigPath = path.join(scriptDirectory, "project-config.json");

const PROJECT_QUERY = `
  query ProjectByTitle($owner: String!) {
    user(login: $owner) {
      id
      projectsV2(first: 100) {
        nodes {
          id
          number
          title
          url
          closed
          public
          readme
          shortDescription
        }
      }
    }
  }
`;

const PROJECT_STATE_QUERY = `
  query ProjectState($owner: String!, $number: Int!) {
    user(login: $owner) {
      projectV2(number: $number) {
        id
        number
        title
        url
        public
        readme
        shortDescription
        repositories(first: 100) {
          nodes {
            nameWithOwner
          }
        }
        fields(first: 100) {
          nodes {
            __typename
            ... on ProjectV2Field {
              id
              databaseId
              name
              dataType
            }
            ... on ProjectV2SingleSelectField {
              id
              databaseId
              name
              dataType
              options {
                id
                name
                color
                description
              }
            }
            ... on ProjectV2IterationField {
              id
              databaseId
              name
              dataType
              configuration {
                duration
                startDay
                iterations {
                  id
                  title
                  startDate
                  duration
                }
                completedIterations {
                  id
                  title
                  startDate
                  duration
                }
              }
            }
          }
        }
        views(first: 100) {
          nodes {
            id
            number
            name
            layout
            filter
          }
        }
      }
    }
  }
`;

const ALL_ISSUES_QUERY = `
  query AllIssues(
    $owner: String!
    $repository: String!
    $after: String
  ) {
    repository(owner: $owner, name: $repository) {
      issues(
        first: 100
        after: $after
        states: [OPEN, CLOSED]
        orderBy: {field: CREATED_AT, direction: ASC}
      ) {
        nodes {
          id
          number
          title
          url
          state
          labels(first: 100) {
            nodes {
              name
            }
          }
        }
        pageInfo {
          hasNextPage
          endCursor
        }
      }
    }
  }
`;

const PROJECT_ITEMS_QUERY = `
  query ProjectItems($project: ID!, $after: String) {
    node(id: $project) {
      ... on ProjectV2 {
        items(first: 100, after: $after) {
          nodes {
            id
            type
            content {
              ... on Issue {
                id
                number
                url
                state
                repository {
                  nameWithOwner
                }
                labels(first: 100) {
                  nodes {
                    name
                  }
                }
              }
            }
            fieldValues(first: 100) {
              nodes {
                __typename
                ... on ProjectV2ItemFieldSingleSelectValue {
                  optionId
                  name
                  field {
                    ... on ProjectV2FieldCommon {
                      id
                      name
                    }
                  }
                }
                ... on ProjectV2ItemFieldDateValue {
                  date
                  field {
                    ... on ProjectV2FieldCommon {
                      id
                      name
                    }
                  }
                }
                ... on ProjectV2ItemFieldIterationValue {
                  iterationId
                  title
                  startDate
                  field {
                    ... on ProjectV2FieldCommon {
                      id
                      name
                    }
                  }
                }
              }
            }
          }
          pageInfo {
            hasNextPage
            endCursor
          }
        }
      }
    }
  }
`;

function fail(message) {
  throw new Error(message);
}

function firstDefined(...values) {
  return values.find((value) => value !== undefined && value !== null);
}

function arrayValue(value) {
  return Array.isArray(value) ? value : [];
}

function cleanText(value) {
  return typeof value === "string" ? value.trim() : value;
}

function lowerText(value) {
  return String(value).trim().toLowerCase();
}

function parseIssueNumber(value) {
  if (Number.isInteger(value) && value > 0) {
    return value;
  }
  if (typeof value === "string") {
    const direct = Number.parseInt(value.replace(/^#/, ""), 10);
    if (/^#?\d+$/.test(value.trim()) && direct > 0) {
      return direct;
    }
    const match = value.match(/\/issues\/(\d+)(?:$|[?#/])/);
    if (match) {
      return Number.parseInt(match[1], 10);
    }
  }
  return undefined;
}

function normalizeDate(value, fieldName, issueNumber) {
  if (value === undefined || value === null || value === "") {
    return undefined;
  }
  const text = String(value).slice(0, 10);
  if (!/^\d{4}-\d{2}-\d{2}$/.test(text)) {
    fail(`Issue #${issueNumber} has an invalid ${fieldName}: ${value}`);
  }
  const date = new Date(`${text}T00:00:00Z`);
  if (Number.isNaN(date.valueOf()) || date.toISOString().slice(0, 10) !== text) {
    fail(`Issue #${issueNumber} has an invalid ${fieldName}: ${value}`);
  }
  return text;
}

function mappedValue(value, mapping) {
  if (value === undefined || value === null || value === "") {
    return undefined;
  }
  const normalized = lowerText(value);
  const withoutPrefix = normalized.includes(":")
    ? normalized.slice(normalized.lastIndexOf(":") + 1)
    : normalized;
  return mapping[normalized] ?? mapping[withoutPrefix] ?? cleanText(String(value));
}

export function parseArguments(argv) {
  const options = {
    apply: false,
    configPath: defaultConfigPath,
    snapshotPath: undefined,
    help: false,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--apply") {
      options.apply = true;
    } else if (argument === "--config") {
      options.configPath = path.resolve(argv[++index] ?? "");
    } else if (argument === "--snapshot") {
      options.snapshotPath = path.resolve(argv[++index] ?? "");
    } else if (argument === "--help" || argument === "-h") {
      options.help = true;
    } else {
      fail(`Unknown argument: ${argument}`);
    }
  }
  return options;
}

export function validateConfig(config) {
  if (config?.schemaVersion !== 1) {
    fail("project-config.json must use schemaVersion 1");
  }
  if (!/^[A-Za-z0-9-]+$/.test(config.owner ?? "")) {
    fail("project-config.json has an invalid owner");
  }
  if (config.repository !== `${config.owner}/kofun`) {
    fail(
      `Remote-write guard: repository must be ${config.owner}/kofun, got ${config.repository}`,
    );
  }
  if (config.title !== "Kofun Delivery Roadmap") {
    fail("Project title must be Kofun Delivery Roadmap");
  }
  if (config.issueScope !== "all") {
    fail("Project issueScope must be all");
  }
  if (config.project?.visibility !== "PUBLIC") {
    fail("Kofun Delivery Roadmap must be public");
  }
  if (!String(config.project?.readme ?? "").includes("/kofun/roadmap/")) {
    fail("Project README must link the published roadmap");
  }
  const fields = arrayValue(config.project?.fields);
  const names = new Set(fields.map((field) => field.name));
  for (const required of [
    "Status",
    "Priority",
    "Size",
    "Agent Slot",
    "Start Date",
    "Target Date",
    "Iteration",
  ]) {
    if (!names.has(required)) {
      fail(`Missing required project field configuration: ${required}`);
    }
  }
  const status = fields.find((field) => field.name === "Status");
  if (!status.existing || status.type !== "SINGLE_SELECT") {
    fail("Status must be configured as an existing SINGLE_SELECT field");
  }
  const slots = fields.find((field) => field.name === "Agent Slot")?.options;
  if (JSON.stringify(slots) !== JSON.stringify(["A", "B", "C", "Review"])) {
    fail("Agent Slot options must be exactly A, B, C, Review");
  }
  const iteration = fields.find((field) => field.name === "Iteration")?.iteration;
  if (
    !iteration ||
    !Number.isInteger(iteration.duration) ||
    iteration.duration < 1 ||
    !Number.isInteger(iteration.count) ||
    iteration.count < 1
  ) {
    fail("Iteration requires positive integer duration and count values");
  }
  normalizeDate(iteration.startDate, "iteration startDate", "configuration");
  return config;
}

export function normalizeSchedule(snapshot, mappings) {
  const scheduled = Array.isArray(snapshot?.schedule)
    ? snapshot.schedule
    : arrayValue(snapshot?.schedule?.items);
  const unscheduled = arrayValue(snapshot?.unscheduled).filter(
    (item) => item && typeof item === "object",
  );
  // Scheduled records come last so their lane and dates remain authoritative
  // if a malformed snapshot ever contains the same issue in both collections.
  const rawSchedule = [...unscheduled, ...scheduled];
  const normalized = new Map();

  for (const raw of rawSchedule) {
    const fields = raw?.fields ?? {};
    const issueNumber = parseIssueNumber(
      firstDefined(
        raw?.issue_number,
        raw?.issueNumber,
        raw?.issue?.number,
        raw?.number,
        raw?.issue_url,
        raw?.url,
      ),
    );
    if (!issueNumber) {
      fail("Every schedule item must identify a positive issue number");
    }
    const iterationValue = firstDefined(
      raw?.iteration?.title,
      raw?.iteration,
      fields.iteration,
      raw?.week,
    );
    normalized.set(issueNumber, {
      issueNumber,
      title: cleanText(firstDefined(raw?.title, raw?.issue?.title)),
      status: mappedValue(
        firstDefined(
          raw?.status,
          fields.status,
          raw?.workflow,
          raw?.schedule_status,
        ),
        mappings.status ?? {},
      ),
      priority: mappedValue(
        firstDefined(raw?.priority, fields.priority),
        mappings.priority ?? {},
      ),
      size: mappedValue(
        firstDefined(raw?.size, fields.size),
        mappings.size ?? {},
      ),
      agentSlot: mappedValue(
        firstDefined(
          raw?.agent_slot,
          raw?.agentSlot,
          fields.agent_slot,
          fields.agentSlot,
          raw?.lane,
          raw?.agent,
        ),
        mappings.agentSlot ?? {},
      ),
      startDate: normalizeDate(
        firstDefined(
          raw?.start_date,
          raw?.startDate,
          fields.start_date,
          fields.startDate,
          raw?.start,
        ),
        "start date",
        issueNumber,
      ),
      targetDate: normalizeDate(
        firstDefined(
          raw?.target_date,
          raw?.targetDate,
          fields.target_date,
          fields.targetDate,
          raw?.end_date,
          raw?.endDate,
          raw?.finish,
        ),
        "target date",
        issueNumber,
      ),
      iteration:
        typeof iterationValue === "string" ? iterationValue.trim() : undefined,
    });
  }
  return normalized;
}

export function isOpenCuratedIssue(issue) {
  const labels = arrayValue(issue?.labels?.nodes ?? issue?.labels).map((label) =>
    lowerText(typeof label === "string" ? label : label?.name),
  );
  return lowerText(issue?.state ?? "") === "open" && labels.includes("curated");
}

export function filterOpenCuratedIssues(issues) {
  return arrayValue(issues)
    .filter(isOpenCuratedIssue)
    .sort((left, right) => left.number - right.number);
}

export function filterRepositoryIssues(issues) {
  return arrayValue(issues)
    .filter((issue) => Number.isInteger(issue?.number) && issue.number > 0)
    .filter((issue) =>
      ["open", "closed"].includes(lowerText(issue.state ?? "")),
    )
    .sort((left, right) => left.number - right.number);
}

function addDays(dateText, days) {
  const date = new Date(`${dateText}T00:00:00Z`);
  date.setUTCDate(date.getUTCDate() + days);
  return date.toISOString().slice(0, 10);
}

export function buildIterationConfiguration(iteration) {
  const iterations = Array.from({ length: iteration.count }, (_, index) => {
    const startDate = addDays(iteration.startDate, iteration.duration * index);
    return {
      title: `${iteration.titlePrefix ?? "Week"} ${index + 1} (${startDate})`,
      startDate,
      duration: iteration.duration,
    };
  });
  return {
    startDate: iteration.startDate,
    duration: iteration.duration,
    iterations,
  };
}

function fieldType(field) {
  return field?.dataType ?? field?.type;
}

function fieldByName(fields, name) {
  return arrayValue(fields).find((field) => field.name === name);
}

function optionByName(field, name) {
  if (!name) {
    return undefined;
  }
  return arrayValue(field?.options).find(
    (option) => lowerText(option.name) === lowerText(name),
  );
}

function iterationByValue(field, title, dateText) {
  const allIterations = [
    ...arrayValue(field?.configuration?.iterations),
    ...arrayValue(field?.configuration?.completedIterations),
  ];
  if (title) {
    const exact = allIterations.find(
      (iteration) => lowerText(iteration.title) === lowerText(title),
    );
    if (exact) {
      return exact;
    }
  }
  if (!dateText) {
    return undefined;
  }
  const time = new Date(`${dateText}T00:00:00Z`).valueOf();
  return allIterations.find((iteration) => {
    const start = new Date(`${iteration.startDate}T00:00:00Z`).valueOf();
    const end = start + iteration.duration * 86_400_000;
    return time >= start && time < end;
  });
}

function currentFieldValues(item) {
  const result = new Map();
  for (const value of arrayValue(item?.fieldValues?.nodes)) {
    const name = value?.field?.name;
    if (!name) {
      continue;
    }
    if (value.__typename === "ProjectV2ItemFieldSingleSelectValue") {
      result.set(name, {
        kind: "single",
        id: value.optionId,
        display: value.name,
      });
    } else if (value.__typename === "ProjectV2ItemFieldDateValue") {
      result.set(name, { kind: "date", id: value.date, display: value.date });
    } else if (value.__typename === "ProjectV2ItemFieldIterationValue") {
      result.set(name, {
        kind: "iteration",
        id: value.iterationId,
        display: value.title,
      });
    }
  }
  return result;
}

export function desiredFieldUpdates(scheduleItem, fields, item = undefined) {
  if (!scheduleItem) {
    return { updates: [], warnings: [] };
  }
  const updates = [];
  const warnings = [];
  const existing = currentFieldValues(item);

  const addSingle = (fieldName, desiredName) => {
    if (!desiredName) {
      return;
    }
    const field = fieldByName(fields, fieldName);
    const option = optionByName(field, desiredName);
    if (!field || !option) {
      warnings.push(
        `#${scheduleItem.issueNumber}: ${fieldName} option "${desiredName}" is unavailable`,
      );
      return;
    }
    if (existing.get(fieldName)?.id !== option.id) {
      updates.push({
        fieldId: field.id,
        fieldName,
        display: option.name,
        value: { singleSelectOptionId: option.id },
      });
    }
  };
  const addDate = (fieldName, desiredDate) => {
    if (!desiredDate) {
      return;
    }
    const field = fieldByName(fields, fieldName);
    if (!field) {
      warnings.push(`#${scheduleItem.issueNumber}: ${fieldName} is unavailable`);
      return;
    }
    if (existing.get(fieldName)?.id !== desiredDate) {
      updates.push({
        fieldId: field.id,
        fieldName,
        display: desiredDate,
        value: { date: desiredDate },
      });
    }
  };

  addSingle("Status", scheduleItem.status);
  addSingle("Priority", scheduleItem.priority);
  addSingle("Size", scheduleItem.size);
  addSingle("Agent Slot", scheduleItem.agentSlot);
  addDate("Start Date", scheduleItem.startDate);
  addDate("Target Date", scheduleItem.targetDate);

  const iterationField = fieldByName(fields, "Iteration");
  if (iterationField && (scheduleItem.iteration || scheduleItem.startDate)) {
    const iteration = iterationByValue(
      iterationField,
      scheduleItem.iteration,
      scheduleItem.startDate,
    );
    if (!iteration) {
      warnings.push(
        `#${scheduleItem.issueNumber}: no Iteration contains ${scheduleItem.startDate ?? scheduleItem.iteration}`,
      );
    } else if (existing.get("Iteration")?.id !== iteration.id) {
      updates.push({
        fieldId: iterationField.id,
        fieldName: "Iteration",
        display: iteration.title,
        value: { iterationId: iteration.id },
      });
    }
  }
  return { updates, warnings };
}

export function buildReconciliationPlan({
  repository,
  fields,
  projectItems,
  issues,
  schedule,
}) {
  const managedIssues = filterRepositoryIssues(issues);
  const itemByNumber = new Map(
    arrayValue(projectItems)
      .filter((item) => item?.content?.repository?.nameWithOwner === repository)
      .filter((item) => Number.isInteger(item?.content?.number))
      .map((item) => [item.content.number, item]),
  );
  const additions = [];
  const updates = [];
  const warnings = [];

  for (const issue of managedIssues) {
    const item = itemByNumber.get(issue.number);
    if (!item) {
      additions.push(issue);
      continue;
    }
    const desired = desiredFieldUpdates(schedule.get(issue.number), fields, item);
    updates.push(
      ...desired.updates.map((update) => ({
        ...update,
        issueNumber: issue.number,
        itemId: item.id,
      })),
    );
    warnings.push(...desired.warnings);
  }

  const managedNumbers = new Set(managedIssues.map((issue) => issue.number));
  for (const number of schedule.keys()) {
    if (!managedNumbers.has(number)) {
      warnings.push(
        `#${number}: skipped because it is not a repository issue`,
      );
    }
  }
  return { additions, updates, warnings, managedIssues };
}

export function buildViewCreateRequest(config, projectNumber, view) {
  return {
    endpoint: `users/${config.owner}/projectsV2/${projectNumber}/views`,
    headers: [
      "Accept: application/vnd.github+json",
      `X-GitHub-Api-Version: ${config.api.viewsVersion}`,
    ],
    fields: {
      name: view.name,
      layout: view.layout,
      filter: view.filter,
    },
  };
}

export function projectViewKey(view) {
  const layout = lowerText(view?.layout ?? "").replace(/_layout$/, "");
  return `${lowerText(view?.name ?? "")}:${layout}`;
}

export function tokenGuidance({ apply, actions, projectsTokenConfigured }) {
  if (actions && !projectsTokenConfigured) {
    return {
      canContinue: !apply,
      message:
        "GitHub Actions GITHUB_TOKEN is repository-scoped and cannot access Projects. " +
        "Configure a classic PAT with project and repo scopes as the PROJECTS_TOKEN Actions secret. " +
        "A fine-grained PAT cannot create views for this user-owned project.",
    };
  }
  return {
    canContinue: true,
    message:
      "Local apply requires gh authentication with the project scope " +
      "(gh auth refresh -s project). User-owned view creation requires a classic PAT/OAuth token.",
  };
}

function runCommand(command, args, { input, allowFailure = false } = {}) {
  const result = spawnSync(command, args, {
    cwd: repositoryRoot,
    encoding: "utf8",
    input,
    env: process.env,
    maxBuffer: 20 * 1024 * 1024,
  });
  if (result.error) {
    throw result.error;
  }
  if (result.status !== 0 && !allowFailure) {
    const details = (result.stderr || result.stdout || "").trim();
    fail(`${command} ${args.slice(0, 3).join(" ")} failed: ${details}`);
  }
  return result;
}

function runGh(args, options) {
  return runCommand("gh", args, options);
}

function runGraphQL(query, variables) {
  const result = runGh(["api", "graphql", "--input", "-"], {
    input: JSON.stringify({ query, variables }),
  });
  let response;
  try {
    response = JSON.parse(result.stdout);
  } catch {
    fail(`GitHub returned invalid JSON: ${result.stdout.slice(0, 500)}`);
  }
  if (response.errors?.length) {
    fail(response.errors.map((error) => error.message).join("; "));
  }
  return response.data;
}

function parseRemoteRepository(remote) {
  const match = remote
    .trim()
    .match(/github\.com(?::|\/)([^/]+)\/([^/\s]+?)(?:\.git)?$/i);
  return match ? `${match[1]}/${match[2]}` : undefined;
}

export function verifyRepositoryRemote(expected, remote) {
  const actual = parseRemoteRepository(remote);
  if (actual !== expected) {
    fail(
      `Remote-write guard: current origin resolves to ${actual ?? "an unknown repository"}, expected ${expected}`,
    );
  }
  return actual;
}

function getOrigin() {
  const result = runCommand("git", ["remote", "get-url", "origin"]);
  return result.stdout.trim();
}

function findProject(config) {
  const data = runGraphQL(PROJECT_QUERY, { owner: config.owner });
  if (!data.user) {
    fail(`GitHub user ${config.owner} was not found`);
  }
  const matches = data.user.projectsV2.nodes.filter(
    (project) => !project.closed && project.title === config.title,
  );
  if (matches.length > 1) {
    fail(`More than one open project is titled "${config.title}"`);
  }
  return { ownerId: data.user.id, project: matches[0] };
}

function createProject(config, ownerId) {
  const query = `
    mutation CreateProject($input: CreateProjectV2Input!) {
      createProjectV2(input: $input) {
        projectV2 {
          id
          number
          title
          url
          shortDescription
        }
      }
    }
  `;
  return runGraphQL(query, {
    input: { ownerId, title: config.title },
  }).createProjectV2.projectV2;
}

function getProjectState(config, number) {
  const data = runGraphQL(PROJECT_STATE_QUERY, {
    owner: config.owner,
    number,
  });
  if (!data.user?.projectV2) {
    fail(`Project ${config.owner}/${number} was not found`);
  }
  return data.user.projectV2;
}

function ensureProjectMetadata(config, project) {
  const editArgs = [
    "project",
    "edit",
    String(project.number),
    "--owner",
    config.owner,
  ];
  if (project.shortDescription !== config.project.description) {
    editArgs.push("--description", config.project.description);
  }
  if (project.readme !== config.project.readme) {
    editArgs.push("--readme", config.project.readme);
  }
  if (!project.public) {
    editArgs.push("--visibility", config.project.visibility);
  }
  if (editArgs.length > 6) {
    runGh(editArgs);
  }
  const linked = arrayValue(project.repositories?.nodes).some(
    (repository) => repository.nameWithOwner === config.repository,
  );
  if (!linked) {
    runGh([
      "project",
      "link",
      String(project.number),
      "--owner",
      config.owner,
      "--repo",
      config.repository,
    ]);
  }
}

function createIterationField(projectId, definition) {
  const query = `
    mutation CreateIteration($input: CreateProjectV2FieldInput!) {
      createProjectV2Field(input: $input) {
        projectV2Field {
          ... on ProjectV2IterationField {
            id
            name
          }
        }
      }
    }
  `;
  runGraphQL(query, {
    input: {
      projectId,
      dataType: "ITERATION",
      name: definition.name,
      iterationConfiguration: buildIterationConfiguration(definition.iteration),
    },
  });
}

function createField(config, project, definition) {
  if (definition.type === "ITERATION") {
    createIterationField(project.id, definition);
    return;
  }
  const args = [
    "project",
    "field-create",
    String(project.number),
    "--owner",
    config.owner,
    "--name",
    definition.name,
    "--data-type",
    definition.type,
  ];
  if (definition.type === "SINGLE_SELECT") {
    args.push("--single-select-options", definition.options.join(","));
  }
  runGh(args);
}

function updateSingleSelectField(field, requiredOptions) {
  const colors = [
    "GRAY",
    "BLUE",
    "GREEN",
    "YELLOW",
    "ORANGE",
    "RED",
    "PURPLE",
    "PINK",
  ];
  const current = arrayValue(field.options).map((option) => ({
    id: option.id,
    name: option.name,
    color: option.color,
    description: option.description ?? "",
  }));
  const existingNames = new Set(current.map((option) => lowerText(option.name)));
  for (const name of requiredOptions) {
    if (!existingNames.has(lowerText(name))) {
      current.push({
        name,
        color: colors[current.length % colors.length],
        description: "",
      });
    }
  }
  const query = `
    mutation UpdateField($input: UpdateProjectV2FieldInput!) {
      updateProjectV2Field(input: $input) {
        projectV2Field {
          ... on ProjectV2SingleSelectField {
            id
          }
        }
      }
    }
  `;
  runGraphQL(query, {
    input: {
      fieldId: field.id,
      singleSelectOptions: current,
    },
  });
}

function ensureFields(config, project) {
  let changed = false;
  for (const definition of config.project.fields) {
    const existing = fieldByName(project.fields.nodes, definition.name);
    if (!existing) {
      if (definition.existing) {
        fail(
          `Required existing field "${definition.name}" is missing; refusing to create a second Status field`,
        );
      }
      createField(config, project, definition);
      changed = true;
      continue;
    }
    if (fieldType(existing) !== definition.type) {
      fail(
        `Field "${definition.name}" has type ${fieldType(existing)}, expected ${definition.type}`,
      );
    }
    if (definition.type === "SINGLE_SELECT") {
      const missing = definition.options.filter(
        (name) => !optionByName(existing, name),
      );
      if (missing.length) {
        updateSingleSelectField(existing, definition.options);
        changed = true;
      }
    }
  }
  return changed;
}

function getAllRepositoryIssues(config) {
  const [owner, repository] = config.repository.split("/");
  const issues = [];
  let after = null;
  do {
    const data = runGraphQL(ALL_ISSUES_QUERY, {
      owner,
      repository,
      after,
    });
    if (!data.repository) {
      fail(`Repository ${config.repository} was not found`);
    }
    const connection = data.repository.issues;
    issues.push(...connection.nodes);
    after = connection.pageInfo.hasNextPage
      ? connection.pageInfo.endCursor
      : null;
  } while (after);
  return filterRepositoryIssues(issues);
}

function getProjectItems(projectId) {
  const items = [];
  let after = null;
  do {
    const data = runGraphQL(PROJECT_ITEMS_QUERY, {
      project: projectId,
      after,
    });
    const connection = data.node?.items;
    if (!connection) {
      fail(`Project node ${projectId} was not found`);
    }
    items.push(...connection.nodes);
    after = connection.pageInfo.hasNextPage
      ? connection.pageInfo.endCursor
      : null;
  } while (after);
  return items;
}

export function buildAddItemsMutation(issues) {
  const declarations = ["$project: ID!"];
  const selections = [];
  const variables = {};
  issues.forEach((issue, index) => {
    declarations.push(`$content${index}: ID!`);
    selections.push(`
      add${index}: addProjectV2ItemById(
        input: {projectId: $project, contentId: $content${index}}
      ) {
        item { id }
      }
    `);
    variables[`content${index}`] = issue.id;
  });
  return {
    query: `mutation AddItems(${declarations.join(", ")}) {
      ${selections.join("\n")}
    }`,
    variables,
  };
}

function addProjectItems(projectId, issues, batchSize = 20) {
  const added = new Map();
  for (let offset = 0; offset < issues.length; offset += batchSize) {
    const batch = issues.slice(offset, offset + batchSize);
    const mutation = buildAddItemsMutation(batch);
    const data = runGraphQL(mutation.query, {
      project: projectId,
      ...mutation.variables,
    });
    batch.forEach((issue, index) => {
      added.set(issue.number, data[`add${index}`].item.id);
    });
  }
  return added;
}

function applyItemUpdates(projectId, itemId, updates) {
  if (!updates.length) {
    return;
  }
  const declarations = [];
  const selections = [];
  const variables = {};
  updates.forEach((update, index) => {
    const key = `input${index}`;
    declarations.push(`$${key}: UpdateProjectV2ItemFieldValueInput!`);
    selections.push(`
      update${index}: updateProjectV2ItemFieldValue(input: $${key}) {
        projectV2Item {
          id
        }
      }
    `);
    variables[key] = {
      projectId,
      itemId,
      fieldId: update.fieldId,
      value: update.value,
    };
  });
  runGraphQL(
    `mutation UpdateItem(${declarations.join(", ")}) {
      ${selections.join("\n")}
    }`,
    variables,
  );
}

function ensureViews(config, project) {
  const existing = new Set(
    arrayValue(project.views?.nodes).map(projectViewKey),
  );
  const created = [];
  const warnings = [];
  for (const view of config.project.views) {
    const key = projectViewKey(view);
    if (existing.has(key)) {
      continue;
    }
    const request = buildViewCreateRequest(config, project.number, view);
    const args = ["api", "--method", "POST"];
    for (const header of request.headers) {
      args.push("-H", header);
    }
    args.push(request.endpoint);
    for (const [name, value] of Object.entries(request.fields)) {
      args.push("-f", `${name}=${value}`);
    }
    const result = runGh(args, { allowFailure: true });
    if (result.status === 0) {
      created.push(view.name);
    } else {
      warnings.push(
        `Could not create view "${view.name}": ${(result.stderr || result.stdout).trim()}. ` +
          "For a user-owned project this endpoint requires a classic PAT/OAuth token; " +
          "fine-grained PATs and GitHub App tokens are unsupported.",
      );
    }
  }
  return { created, warnings };
}

function printManualViewConfiguration(projectUrl) {
  console.log("");
  console.log("View configuration that the public API still cannot set:");
  console.log(
    `1. Open ${projectUrl} and edit "Delivery roadmap": Start Date = Start Date; Target Date = Target Date; group by Agent Slot.`,
  );
  console.log(
    '2. Edit "This week": group by Status and filter the current Iteration in the UI.',
  );
  console.log(
    '3. Edit "Agent capacity": group by Agent Slot and show Size, Start Date, Target Date, and Iteration.',
  );
  console.log(
    "The 2026-03-10 view-create endpoint accepts only name, layout, filter, and visible_fields (not roadmap date fields, grouping, or sorting).",
  );
}

function printDryRun(config, schedule, snapshotPath, guidance) {
  console.log("DRY RUN — no GitHub API or repository mutation was performed.");
  console.log(`Target repository: ${config.repository}`);
  console.log(`Target user project: ${config.owner} / ${config.title}`);
  console.log(`Snapshot: ${path.relative(repositoryRoot, snapshotPath)}`);
  console.log(`Curated planning records: ${schedule.size}`);
  console.log(
    "Apply will query GitHub and add every open and closed issue from the exact repository.",
  );
  console.log(
    `Fields: ${config.project.fields.map((field) => `${field.name}:${field.type}`).join(", ")}`,
  );
  console.log(
    `Views: ${config.project.views.map((view) => `${view.name}:${view.layout}`).join(", ")}`,
  );
  console.log(`Authentication: ${guidance.message}`);
  console.log("Run again with --apply to authorize these exact Project mutations.");
  printManualViewConfiguration(
    `https://github.com/users/${config.owner}/projects`,
  );
}

async function readJson(filename) {
  return JSON.parse(await readFile(filename, "utf8"));
}

async function apply(config, schedule) {
  verifyRepositoryRemote(config.repository, getOrigin());
  console.log(
    `APPLY authorized by --apply: mutate user Project "${config.title}" for exact repository ${config.repository}.`,
  );
  console.log("No commits, refs, pull requests, issues, or issue labels will be changed.");

  let { ownerId, project } = findProject(config);
  let projectCreated = false;
  if (!project) {
    project = createProject(config, ownerId);
    projectCreated = true;
    console.log(`Created project #${project.number}: ${project.url}`);
  } else {
    console.log(`Using existing project #${project.number}: ${project.url}`);
  }

  project = getProjectState(config, project.number);
  ensureProjectMetadata(config, project);
  if (ensureFields(config, project)) {
    project = getProjectState(config, project.number);
  }

  const issues = getAllRepositoryIssues(config);
  let items = getProjectItems(project.id);
  let plan = buildReconciliationPlan({
    repository: config.repository,
    fields: project.fields.nodes,
    projectItems: items,
    issues,
    schedule,
  });

  const addedItemIds = addProjectItems(project.id, plan.additions);
  if (plan.additions.length) {
    items = getProjectItems(project.id);
    plan = buildReconciliationPlan({
      repository: config.repository,
      fields: project.fields.nodes,
      projectItems: items,
      issues,
      schedule,
    });
  }

  const updatesByItem = new Map();
  for (const update of plan.updates) {
    const list = updatesByItem.get(update.itemId) ?? [];
    list.push(update);
    updatesByItem.set(update.itemId, list);
  }
  for (const [itemId, updates] of updatesByItem) {
    applyItemUpdates(project.id, itemId, updates);
  }

  project = getProjectState(config, project.number);
  const viewResult = ensureViews(config, project);
  const warnings = [...plan.warnings, ...viewResult.warnings];

  console.log("");
  console.log("Project synchronization complete.");
  console.log(`Project created: ${projectCreated ? "yes" : "no"}`);
  console.log(`Repository issues managed: ${issues.length}`);
  console.log(`Issues added: ${addedItemIds.size}`);
  console.log(`Field values updated: ${plan.updates.length}`);
  console.log(`Views created: ${viewResult.created.length}`);
  if (warnings.length) {
    console.log("Warnings:");
    for (const warning of warnings) {
      console.log(`- ${warning}`);
    }
  }
  printManualViewConfiguration(project.url);
}

function help() {
  console.log(`Usage: node site/setup-project.mjs [options]

Options:
  --apply             Mutate the exact configured user Project (default is offline dry-run)
  --config PATH       Override site/project-config.json
  --snapshot PATH     Override app/roadmap/plan-snapshot.json
  -h, --help          Show this help

GitHub Actions must provide a classic PAT with project and repo scopes through
the PROJECTS_TOKEN secret. The repository-scoped GITHUB_TOKEN cannot access
Projects. No token is embedded in this script or its configuration.`);
}

export async function main(argv = process.argv.slice(2)) {
  const options = parseArguments(argv);
  if (options.help) {
    help();
    return;
  }
  const config = validateConfig(await readJson(options.configPath));
  const snapshotPath =
    options.snapshotPath ??
    path.resolve(repositoryRoot, config.snapshotPath);
  const snapshot = await readJson(snapshotPath);
  const schedule = normalizeSchedule(snapshot, config.mappings);
  const guidance = tokenGuidance({
    apply: options.apply,
    actions: process.env.GITHUB_ACTIONS === "true",
    projectsTokenConfigured:
      process.env.KOFUN_PROJECTS_TOKEN_CONFIGURED === "true",
  });

  if (!guidance.canContinue) {
    fail(guidance.message);
  }
  if (!options.apply) {
    printDryRun(config, schedule, snapshotPath, guidance);
    return;
  }
  await apply(config, schedule);
}

const invokedDirectly =
  process.argv[1] &&
  pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url;

if (invokedDirectly) {
  main().catch((error) => {
    const projectsHint =
      /scope|project|resource not accessible|forbidden|graphql/i.test(
        error.message,
      )
        ? "\nProjects authentication: local gh needs `gh auth refresh -s project`; " +
          "Actions needs a classic PAT with project and repo scopes in PROJECTS_TOKEN. " +
          "GITHUB_TOKEN cannot access Projects."
        : "";
    console.error(`ERROR: ${error.message}${projectsHint}`);
    process.exitCode = 1;
  });
}
