# POC-06 validation runbook

1. Run the headless replay and adapter tests. Confirm the Default and Arc event
   traces, Stroke digest, and Document digest match.
2. Run fault/lifecycle tests with queue capacity, duplicate begin, stale ack,
   surface loss, resize/generation replacement, cancellation, and repeated
   create/destroy.
3. On each platform, record the platform kind, backend capability bits, target
   generation, refresh rate, sample-to-visible frame count, missed presents,
   queue age, and presentation evidence level.
4. For Tier A devices, report Preview p95 ≤ 16.7 ms and p99 ≤ 33.3 ms and
   handoff ≤ 2 frames. Do not claim a compositor-visible or photometric result
   from a render-return timestamp.
5. Archive the generated JSON and trace. A device report is `Validating` until
   the physical-device and Human Ink Gate evidence is reviewed.
