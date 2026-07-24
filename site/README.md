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
npm run dev
```

`npm run verify:site` executes the playground examples and negative diagnostics
and checks every rendered Markdown source and local link before creating a
production build. Run `npm audit --audit-level=high` before publishing a saved
site version.
