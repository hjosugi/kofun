# Naming

## Working title

This prototype uses `Kofun` as a working title.

Intent:

- Start from frustration with Rust
- Easy to associate with functional + Rust
- Short CLI name
- Easy to use with the `.kofun` extension

## Collision risk

The name `kofun` is already used as a crate name in the Rust ecosystem. The same idea has also been published before as shorthand for "functional Rust".

The following are therefore mandatory before any public launch.

1. Search crates.io, PyPI, npm, GitHub, GitLab, and the major Linux distributions.
2. Investigate trademarks for programming languages, compilers, databases, and developer tools.
3. Check domains, social handles, and package namespaces.
4. Consult a legal professional, covering Japan, the United States, and the EU at minimum.
5. Evaluate the project name, CLI name, package prefix, and file extension separately.

## Candidate direction

For the final name, prefer a short coined word that satisfies the following.

- 4 to 7 characters
- Easy to pronounce for both Japanese and English speakers
- Evokes the lightness of `fn` and pipelines
- Not so close as to be mistaken for a Rust-derived language
- Highly searchable
- Package namespace can be secured

This ZIP does not fix the name, and is set up so that namespace changes inside the compiler can be applied in bulk.
