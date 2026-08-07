'use strict';

// The visibility decision the LSP applies to completion and navigation (#1033),
// mirroring `spec/modules/visibility.md` for the bounded slice the Stage 2
// compiler implements: basic modifiers on top-level declarations.
//
// This file is deliberately pure. It takes a declaration's committed identity
// and a caller context and returns a boolean; it reads no document, no
// sidecar, and no filesystem. That is what makes the rule testable on its own
// and what keeps the one security-shaped criterion checkable: caller context is
// an argument here, so the only way to get it wrong is to pass the wrong thing,
// and there is exactly one place that constructs it.
//
// The spec sentence this implements, for top-level declarations, is:
//
//     a top-level declaration is visible only within its source file
//
// so `private` is per *file*, not per module or per package. That is what makes
// "hide dependency internals, keep same-file private symbols" a single rule
// rather than two competing ones.

// spec/modules/visibility.md, "Visibility table": narrowest to widest.
const VISIBILITY_RANK = Object.freeze({
    private: 0,
    restricted: 1,
    internal: 2,
    pub: 3,
});

// The modifiers the bounded slice recognises. `public`, `protected`,
// `pub(crate)`, `pub(super)`, and `pub(in path)` are explicitly *not* aliases —
// the spec says so — and reach `declaredVisibility` as unrecognised text, which
// resolves to the narrowest reading rather than to the one they resemble.
const RECOGNISED = Object.freeze(['private', 'internal', 'pub']);

// `no modifier | private`. An unrecognised modifier also lands here: a reader
// that guessed `pub(crate)` meant `internal` would widen an API on a spelling
// the language does not have.
function declaredVisibility(modifier) {
    if (typeof modifier !== 'string') return 'private';
    return RECOGNISED.includes(modifier) ? modifier : 'private';
}

// `restricted` is `pub(to ancestor.path)`, which `spec/modules/visibility.md`
// lists in its table and then records as follow-up work alongside cross-package
// signature components. Until the compiler decides it, an editor must not: this
// returns false for anything outside the same file, so a restricted declaration
// is treated as private rather than as something this layer invented a rule for.
function accessible(declaration, caller) {
    if (!declaration || !caller) return false;
    if (typeof declaration.fileId !== 'string' || typeof caller.fileId !== 'string') {
        return false;
    }

    // Same source file: every top-level declaration is reachable, including
    // private ones. This is the clause that keeps a file's own helpers in its
    // own editor session, and it is checked first so no wider rule can take it
    // away.
    if (declaration.fileId === caller.fileId) return true;

    const visibility = declaredVisibility(declaration.visibility);
    if (visibility === 'private') return false;

    // An anonymous single-file package is non-importable
    // (`spec/modules/package-roots.md`), so nothing crosses out of it and
    // nothing crosses into it — not even `pub`. A null package identity is that
    // case, and it is also what an unidentifiable file gets, so the unknown and
    // the non-importable fail the same closed way.
    if (declaration.packageId === null || caller.packageId === null) return false;
    if (typeof declaration.packageId !== 'string' || typeof caller.packageId !== 'string') {
        return false;
    }

    if (visibility === 'internal') return declaration.packageId === caller.packageId;
    if (visibility === 'pub') return true;
    return false;
}

// Ordering over the visibility *lattice*, which is not the same thing as
// reading a source modifier — and conflating the two is a mistake that hides.
//
// `declaredVisibility` answers "what does this text in the source mean?", where
// anything unrecognised must read as private. `rank` answers "where does this
// level sit relative to that one?", where `restricted` is a real rung between
// private and internal even though no bounded source spelling reaches it yet.
// Routing `rank` through `declaredVisibility` collapsed restricted onto private
// and made `rank('private') < rank('restricted')` false, which is not an
// ordering anyone would want to reason with.
//
// An unknown level has no position, so it returns null rather than a number a
// comparison would silently accept.
function rank(level) {
    return Object.hasOwn(VISIBILITY_RANK, level) ? VISIBILITY_RANK[level] : null;
}

module.exports = {
    RECOGNISED,
    VISIBILITY_RANK,
    accessible,
    declaredVisibility,
    rank,
};
