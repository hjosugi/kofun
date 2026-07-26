# Kofun official site

This repository also contains the official English-language project and
documentation site. It is a standard Next.js application rooted at `app/`.

The `/docs` route renders a curated manifest of repository Markdown at build
time. Source files remain authoritative; relative links either resolve to
another curated page or to the corresponding file on GitHub. The overview
records the exact reviewed source commit and keeps active implementation claims
separate from design direction and open issues.

The `/roadmap` route renders the generated delivery snapshot from
`app/roadmap/plan-snapshot.json`. It separates planning umbrellas from curated
work, caps active implementation at three collision-safe writer lanes, reserves
the fourth agent for review and integration, and exposes dependency-aware
timeline and calendar views. `docs/DELIVERY_PLAN.md` is generated from the same
snapshot for a reviewable text representation.

The embedded playground is deliberately honest about its boundary:

- `app/kofun-runtime.ts` is a bounded, browser-only learning evaluator.
- `app/kofun-highlight.ts` provides lossless, tolerant syntax coloring while
  the editable `textarea` remains the source of truth.
- The checked repository CLI remains authoritative for ownership diagnostics,
  law evidence, native backends, and bootstrap gates.
- The evaluator has step and List-size limits and does not execute arbitrary
  JavaScript or access the network.

The documentation manifest in `app/docs/docs-manifest.ts` is the only list of
Markdown rendered as first-class site pages. Adding a document there exposes it
in both the overview and sidebar while preserving the source file as the
authority.

Run the site locally:

```sh
npm install
npm run verify:site
npm run build:sites
npm run verify:pages
npm run check:plan
npm run check:status
npm run project:plan
npm run dev
```

`npm run verify:site` executes the playground examples and negative diagnostics
and checks every rendered Markdown source and local link before creating a
production build. Run `npm audit --audit-level=high` before publishing a saved
site version. `npm run build:sites` produces the checked `.open-next/worker.js`
entrypoint and static assets consumed by Sites; the adapter and compatibility
date are pinned in `package.json`, `open-next.config.ts`, and `wrangler.jsonc`.

`npm run verify:pages` copies the checked `docs/tour/` browser application,
builds a static export under `out/` with the `/kofun` project base path, writes
the required `.nojekyll` marker, and checks the exported routes and
root-relative URLs. `.github/workflows/docs-hourly.yml` repeats that gate from
a clean checkout, synchronizes selected issue and exact-commit CI evidence, and
publishes changed output through the configured `gh-pages` branch.

`npm run check:status` performs a read-only GitHub check of `main`, the full CI
run for that exact implementation commit, and the selected
documentation/evidence issues. It exits cleanly without rewriting files when
nothing changed. When it reports a stale snapshot, run `npm run sync:status`,
review `app/docs/status-snapshot.json` and `docs/ISSUE_PROGRESS.md`, then
validate any changed capability claims against the named executable gates.

`npm run check:plan` performs the corresponding read-only issue and schedule
check. `npm run sync:plan` refreshes the checked-in JSON and Markdown delivery
snapshots. Project configuration is intentionally separate:
`npm run project:plan` prints the idempotent GitHub Projects changes, while
`npm run project:apply` applies them only with an explicit classic/OAuth token
carrying the `project` scope. The Project registers every open and closed
repository issue; schedule fields remain limited to the curated execution
scope. `.github/workflows/project-roadmap.yml` repeats
the safe dry-run every six hours and switches to field synchronization only
after a classic PAT with `repo` and `project` scopes is stored as the
`PROJECTS_TOKEN` repository secret.
