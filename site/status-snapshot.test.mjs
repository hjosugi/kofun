import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const snapshot = JSON.parse(
  await readFile(
    new URL("../app/docs/status-snapshot.json", import.meta.url),
    "utf8",
  ),
);

assert.equal(snapshot.schema, "kofun.docs-status/v1");
assert.equal(snapshot.repository, "hjosugi/kofun");
assert.match(snapshot.source_commit, /^[0-9a-f]{40}$/);
assert.equal(snapshot.verification.workflow, "CI");
assert.ok(
  [
    "queued",
    "in_progress",
    "completed",
    "waiting",
    "requested",
    "pending",
    "missing",
  ].includes(snapshot.verification.status),
  `unknown verification status: ${snapshot.verification.status}`,
);
assert.ok(
  snapshot.verification.conclusion === null ||
    (typeof snapshot.verification.conclusion === "string" &&
      snapshot.verification.conclusion.length > 0),
  "verification conclusion must be null or a non-empty string",
);
assert.match(snapshot.verification.url, /^https:\/\/github\.com\/hjosugi\/kofun\//);

if (snapshot.verification.status === "completed") {
  assert.ok(snapshot.verification.conclusion, "completed CI needs a conclusion");
  assert.ok(
    !Number.isNaN(Date.parse(snapshot.verification.completed_at)),
    "completed CI needs a valid completion time",
  );
} else {
  assert.equal(snapshot.verification.completed_at, null);
}

const issueNumbers = snapshot.issues.map((issue) => issue.number);
assert.equal(new Set(issueNumbers).size, issueNumbers.length);
for (const issue of snapshot.issues) {
  if (issue.state === "closed") {
    assert.equal(issue.workflow, "closed");
  }
}

console.log(
  `PASS: ${snapshot.verification.workflow} is ` +
    `${snapshot.verification.conclusion ?? snapshot.verification.status} for ` +
    `${snapshot.source_commit.slice(0, 7)} and ${snapshot.issues.length} issues`,
);
