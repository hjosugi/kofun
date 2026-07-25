# Self-hosting and bootstrap

The canonical current self-host profile, evidence inventory, runnable first
compiler generation, and fixed-point boundary live in
[`bootstrap/selfhost/README.md`](../bootstrap/selfhost/README.md). That document
is also the source rendered by the official self-hosting documentation page.

For the adjacent bounded compiler checkpoints, see
[`bootstrap/stage1/README.md`](../bootstrap/stage1/README.md) and
[`bootstrap/stage2/README.md`](../bootstrap/stage2/README.md).

The current repository has a Python-free Kofun-written seed and a runnable
compiler-produced compiler. It does not yet have the required
three-generation semantic self-hosting fixed point. This file intentionally
remains a short pointer so that bootstrap status has one authority.
