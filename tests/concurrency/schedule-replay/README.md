# Schedule replay bounded corpus

These programs exercise the normative `kofun.schedule-trace/v1` model. They
are model inputs, not Kofun surface programs, and do not claim a production
scheduler, channel runtime, or ownership implementation.

`programs/` covers cancellation, failure propagation, nested scopes, join
order, channel block/wake, sibling ownership conflict, and a declared decision
budget. `rejections/` names each strict replay refusal; the model derives a
known-good trace, applies exactly that mutation, and checks the stable code.

Run from any directory:

```sh
sh spec/concurrency/schedule-trace/check.sh
```
