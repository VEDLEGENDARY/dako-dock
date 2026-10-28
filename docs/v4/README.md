# V4 design source

[The supplied plan](DAKO_V4_PLAN.md) and [visual references](references/README.md) remain here for review.
The redundant foundation source/test copies were removed after integration. Canonical implementations are
[src/Anchor.h](../../src/Anchor.h), [src/IslandState.h](../../src/IslandState.h) and
[tests/IslandTests.cpp](../../tests/IslandTests.cpp), with the expanded runtime tests in the same source tree.

The October 28 review refines the original interaction: dwell applies to the Dako launcher rather than app
icons; wheel input over apps scrolls in both directions; playing-only Peek is the default with a paused-media
option. Slideshow owns one explicit, cancellable deadline and a separate playlist. See the current
[verification record](../VERIFICATION.md) for RC acceptance and evidence.
