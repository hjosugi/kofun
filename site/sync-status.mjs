import { readFile, writeFile } from "node:fs/promises";
import process from "node:process";

const repository = "hjosugi/kofun";
const trackedIssues = [650, 666, 667, 668];
const workflowLabels = new Set([
  "needs-triage",
  "needs-detail",
  "needs-decision",
  "blocked",
  "ready",
  "in-progress",
  "verification-pending",
  "deferred",
]);
const snapshotPath = new URL(
  "../app/docs/status-snapshot.json",
  import.meta.url,
);
const markdownPath = new URL("../docs/ISSUE_PROGRESS.md", import.meta.url);

const headers = {
  Accept: "application/vnd.github+json",
  "User-Agent": "kofun-docs-status-sync",
  "X-GitHub-Api-Version": "2022-11-28",
};
if (process.env.GITHUB_TOKEN) {
  headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;
}

async function github(path) {
  const response = await fetch(`https://api.github.com${path}`, { headers });
  if (!response.ok) {
    throw new Error(
      `GitHub API ${response.status} ${response.statusText}: ${path}`,
    );
  }
  return response.json();
}

function semanticSnapshot(snapshot) {
  return {
    schema: snapshot.schema,
    repository: snapshot.repository,
    source_commit: snapshot.source_commit,
    issues: snapshot.issues,
  };
}

function markdown(snapshot) {
  const shortCommit = snapshot.source_commit.slice(0, 7);
  const rows = snapshot.issues
    .map(
      (issue) =>
        `| [#${issue.number}](${issue.url}) | \`${issue.workflow}\` | ` +
        `\`${issue.state}\` | \`${issue.updated_at}\` | ` +
        `${issue.title.replaceAll("|", "\\|")} |`,
    )
    .join("\n");

  return `# Tracked issue progress

Status: generated read-only snapshot for documentation synchronization.

Repository: [\`${snapshot.repository}\`](https://github.com/${snapshot.repository})

Observed main commit: [\`${shortCommit}\`](https://github.com/${snapshot.repository}/commit/${snapshot.source_commit})

Reviewed at: \`${snapshot.reviewed_at}\`

| Issue | Workflow | State | Last tracker update | Title |
|---|---|---|---|---|
${rows}

This snapshot reports tracker state; it does not prove implementation. Update
capability claims in \`README.md\` and \`docs/MVP_IMPLEMENTED.md\` only after the
corresponding source and executable gates are present on the observed source
commit.
`;
}

const previous = JSON.parse(await readFile(snapshotPath, "utf8"));
const [commit, ...issues] = await Promise.all([
  github(`/repos/${repository}/commits/main`),
  ...trackedIssues.map((number) =>
    github(`/repos/${repository}/issues/${number}`),
  ),
]);

const nextSemantic = {
  schema: "kofun.docs-status/v1",
  repository,
  source_commit: commit.sha,
  issues: issues.map((issue) => {
    const labels = issue.labels.map((label) =>
      typeof label === "string" ? label : label.name,
    );
    return {
      number: issue.number,
      title: issue.title,
      state: issue.state,
      workflow:
        labels.find((label) => workflowLabels.has(label)) ??
        (issue.state === "closed" ? "closed" : "unclassified"),
      updated_at: issue.updated_at,
      url: issue.html_url,
    };
  }),
};

if (
  JSON.stringify(semanticSnapshot(previous)) ===
  JSON.stringify(nextSemantic)
) {
  console.log(
    `UNCHANGED: ${nextSemantic.source_commit.slice(0, 7)} and ` +
      `${nextSemantic.issues.length} tracked issues`,
  );
  process.exit(0);
}

if (process.argv.includes("--check")) {
  console.error("STALE: run npm run sync:status and review the generated diff");
  process.exit(1);
}

const next = {
  ...nextSemantic,
  reviewed_at: new Date().toISOString(),
};
await writeFile(snapshotPath, `${JSON.stringify(next, null, 2)}\n`);
await writeFile(markdownPath, markdown(next));
console.log(
  `UPDATED: ${next.source_commit.slice(0, 7)} and ` +
    `${next.issues.length} tracked issues`,
);
