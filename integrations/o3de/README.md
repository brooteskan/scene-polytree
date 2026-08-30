# O3DE integration

This directory is reserved for an optional O3DE Gem adapter. The adapter will:

- map scene node identities to O3DE entities or render instances where needed;
- translate O3DE notifications into scene operations;
- publish changed transforms at an explicit synchronization boundary; and
- use generic polytree operations rather than implementing hierarchy traversal.

The core library must remain buildable without O3DE.
