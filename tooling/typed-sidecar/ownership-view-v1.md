# Typed-sidecar ownership view v1

The ownership view is bounded, current-file tooling evidence. It is never
compiler, cache, package, or KIF authority. The adapter accepts only a
validated kofun.typed-sidecar/v1 document plus the current source digest.

The output schema name is kofun.ownership-view/v1. It contains only:

- stable ScopeId and BindingId values already present in the sidecar;
- source spans, projected fact status, and safe diagnostic IDs;
- the narrowest containing ScopeId derived from validated span containment;
- dependency edges whose two endpoints both have projected stable identities.

The adapter does not parse source, perform ownership inference, recover missing
identities from display text, access the network or filesystem, or write a
compiler artifact. Logical checkout paths and path-remap root identities are
not copied into the view.

## Status contract

complete means the checked sidecar was complete. partial preserves only the
validated or explicitly incomplete prefix from a failed sidecar. A missing
fact is unknown; an explicit unsupported reason is unsupported; provisional
or error facts are partial and retain their source status.

The caller must supply the current source digest and may supply the expected
generation. A mismatch produces a stale view with no nodes. A cancelled
sidecar likewise produces no nodes. Corrupt input returns a bounded TOV03
error and no view.

## Limits

The hard v1 maxima are 4,096 nodes, 65,536 edges, depth 128, and 4 MiB of
canonical JSON. Callers may lower but never raise those values. Any node,
edge, depth, or byte overflow returns TOV04 before a view is published.

Canonical output recursively sorts object keys and sorts nodes, roots, and
edges by stable identity and span rules. Repeated input and path-remapped
copies therefore produce byte-identical evidence.
