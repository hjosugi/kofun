# Kofun official site

This repository also contains the official English-language project and
documentation site. It is a standard Next.js application rooted at `app/`.

The `/docs` route renders a curated manifest of repository Markdown at build
time. Source files remain authoritative; relative links either resolve to
another curated page or to the corresponding file on GitHub. The overview
records the exact reviewed source commit and keeps active implementation claims
separate from design direction and open issues.

The embedded playground is deliberately honest about its boundary:

- `app/kofun-runtime.ts` is a bounded, browser-only learning evaluator.
- The checked repository CLI remains authoritative for ownership diagnostics,
  law evidence, native backends, and bootstrap gates.
- The evaluator has step and List-size limits and does not execute arbitrary
  JavaScript or access the network.

Run the site locally:

```sh
npm install
npm run verify:site
npm run build:sites
npm run verify:pages
npm run check:status
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
