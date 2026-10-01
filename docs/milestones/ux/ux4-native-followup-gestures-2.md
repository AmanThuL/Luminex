# UX4 native follow-up gestures, part 2

**Status**: Implemented — owner accepted for integration on 2026-10-01; failed and incomplete gates retained as measured

Dated 2026-10-01. Return to [follow-up scope and limits](ux4-native-followup-validation.md).
Evidence prefix `E = ../Luminex-evidence/ux4/native-followup-2026-10-01/`. Each screenshot path below is relative to E.
Context labels retain the controller's ledger; no context is inferred from another gesture.
PASS is the recorded result for the stated action/context, not the complete native gate.

| ID / context | Action | Expected | Observed | Result | Screenshot path |
|---|---|---|---|---|---|
| 0029 / HDW-Auto | Focus Hierarchy search; press F, Home, C | Text entry consumes F, Home and C; camera and capture state remain unchanged | Search displays cf with caret after c, camera stays at 8.500/1.600/0.600 and yaw 89.680; no new capture notice limits: Only the focused Hierarchy search guard was observed. | PASS | E/0029.jpg |
| 0030 / HDW-Auto | In focused search press Cmd+A, Cmd+C, Cmd+X | Selected search text is cut without editor commands | Search is empty and all 24 rows return; Tour Camera selection remains | PASS | E/0030.jpg |
| 0032 / HDW-Auto | Press Cmd+V in focused Hierarchy search | The copied search text is pasted | cf returns; camera and selection remain unchanged | PASS | E/0014.jpg |
| 0031 / HDW-Auto | Click Hierarchy Clear after paste | All subjects return while retaining selection | Search is empty; 24/24 rows and Tour Camera selection remain | PASS | E/0031.jpg |
| 0033 / HDW-Auto | Select imported Crytek Sponza and press F outside text entry | Selection is identified and reliable bounds frame correctly | Inspector identifies Crytek Sponza and viewport changes from lion to wall; whole imported bounds fit is not established | UNVERIFIED | E/gesture-0033.jpg |
| 0034 / HDW-Auto | Press Home outside text entry | Restore authored camera while retaining scene subject selection | The lion authored view returns; Crytek Sponza remains selected | PASS | E/gesture-0034.jpg |
| 0035 / HDW-Auto | Click docked Performance Details | Open detached Live with coherent published snapshot | Performance window opens with Average/Latest stage costs, interval/FPS, extents and peak memory | PASS | E/gesture-0035.jpg |
| 0036 / HDW-Auto | Freeze Performance metrics | Hold one coherent published snapshot independently of playback | Frozen status appears; timed sum8.902ms with scene2.640 average/2.353latest | PASS | E/gesture-0036.jpg |
| 0037 / HDW-Auto | Expand light stage and Metric definitions & exact memory | Show child pass costs, retirement/publication/sample definitions and timed-sum exclusions | Four light passes shown; frame173950, 60/60 samples,4 updates/s; exclusions and controller N/A explained | PASS | E/gesture-0037.jpg |
| 0038 / HDW-Auto | Click Latest sort header while frozen | Sort most recent retired costs without changing snapshot | Temporal5.586 precedes scene2.353; frame173950 and total8.902 remain | PASS | E/gesture-0038.jpg |
| 0039 / HDW-Auto | More > Individual pass rows | Expose per-pass Min/Max/Samples columns | Individual pass rows show Average/Latest/Min/Max and 60 samples at frame173950 | PASS | E/gesture-0039.jpg |
| 0040 / HDW-Auto | Sort individual rows by Min | Sort frozen minimum costs | Descending Min starts shadow0.279,scene0.016,temporal0.012; frozen frame unchanged | PASS | E/gesture-0040.jpg |
| 0041 / HDW-Auto | Sort individual rows by Max | Sort maximum retained costs | Descending Max starts shadow9.204,temporal8.587,scene6.754 | PASS | E/gesture-0041.jpg |
| 0042 / HDW-Auto | Sort individual rows by Samples | Sort actual sample counts | Samples header shows descending selection; every visible pass has60, ties ordered by pass number limits: Unequal sample counts were not produced. | PASS | E/gesture-0042.jpg |
| 0043 / HDW-Auto | More > Clear history while frozen | Empty the held snapshot and wait without mixed stale values | Frozen — empty; frame0,0/60samples, Waiting labels and N/A extents/memory; pass rows empty | PASS | E/gesture-0043.jpg |
| 0044 / HDW-Auto | Resume Performance after Clear | Fresh retired samples refill the empty snapshot | Live costs refill and definitions identify new frame183482 with43/60samples in the first observation limits: Saved screenshot is a later live publication; first observation retained in tool output. | PASS | E/gesture-0044.jpg |
| 0045 / HDW-Auto | Open Measure while Stopped with dynamic resolution off | Start enabled with editable warmup/measured frames | Start measurement enabled; Warmup32/Measured256 and Stop disabled | PASS | E/gesture-0045.jpg |
| 0046 / HDW-Auto | Start stopped Measure with6000 frames | Start runs once; fields lock and progress/Stop appear | Measuring progress11/6000 observed, disabled Start explains already running; Stop enabled | PASS | E/gesture-0046.jpg |
| 0047 / HDW-Auto | Close Performance during Measure | Measure remains active and strip retains progress and Stop | Detached window closes; main strip still Measuring1694/6000 with Operator activity and Stop; rendering edits disabled screenshotReading: 2322/6000 readingTimingLimit: The action observation recorded 1694/6000; the retained screenshot shows 2322/6000. Exact same-frame identity between those readings is unverified. | PASS | E/gesture-0047.jpg |
| 0048 / HDW-Auto | Reopen Performance and return to Measure | Retain the running measurement with advanced progress | Same6000frame run reaches3424/6000 with disabled inputs and enabledStop | PASS | E/gesture-0048.jpg |
| 0049 / HDW-Auto | Click activity-strip Stop during Measure | Cancel the active measurement, clear activity and restore preview | Strip cleared and authored preview returned, but later results show Complete6000/6000, so cancellation was not established | UNVERIFIED | E/gesture-0049.jpg |
| 0050 / HDW-Auto | Inspect Measure results after the run | Retain final results and export action after closing/reopening | Complete6000/6000; mean encode1.349ms/timedGPU7.185ms and Export available | PASS | E/gesture-0050.jpg |
| 0051 / HDW-Auto | Export completed measurement JSON | Write current retained results and report destination | Exported path appears for luminex-interactive-measurement-1790835523426.json | PASS | E/gesture-0051.jpg |
| 0052 / HDW-Auto | Start a second Measure for strip cancellation | New run becomes active | Measuring13/6000 in first observation | PASS | E/gesture-0052.jpg |
| 0053 / HDW-Auto | Click strip Stop early in a second Measure | Cancel while progress is below total and restore preview | Strip cleared from1231/6000 to Stopped preview; results pending verification | PASS | E/gesture-0053.jpg |
