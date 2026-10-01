# UX4 native follow-up gestures, part 5

**Status**: In progress

Dated 2026-10-01. Return to [follow-up scope and limits](ux4-native-followup-validation.md).
Evidence prefix `E = ../Luminex-evidence/ux4/native-followup-2026-10-01/`. Each screenshot path below is relative to E.
Context labels retain the controller's ledger; no context is inferred from another gesture.
PASS is the recorded result for the stated action/context, not the complete native gate.

| ID / context | Action | Expected | Observed | Result | Screenshot path |
|---|---|---|---|---|---|
| 0104 / Parent cleanup | Choose Luminex > Quit from the native menu after reading its Quit item. | The parent QA editor exits normally. | Quit item 22 was invoked. The owned process returned exit 0 and the log ends with 29734 presented, 0 skipped; pgrep then finds neither QA process. The referenced screenshot precedes Quit; the menu snapshot returned no pixels. limitation: Process exit only; no menu/closing screenshot or relaunch/persistence observation. | PASS | E/gesture-0100.jpg |
