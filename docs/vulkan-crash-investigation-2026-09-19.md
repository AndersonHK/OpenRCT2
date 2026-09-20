# Crash investigation — 2026-09-19

Status: bounded investigation complete; owner accepted the analysis and resumed the migration goal. Resume with sequential builds/tests and limited compiler concurrency. Stop on renewed system instability and preserve new evidence.

Windows System event 1001 (Microsoft-Windows-WER-SystemErrorReporting), logged at 16:28:12 PDT, records bugcheck `0x50 (0xffffb386b473c404, 0, 0xfffff80733aaa810, 2)` and `C:\Windows\MEMORY.DMP` (5,204,625,615 bytes). The event is Error severity, while Kernel-Power event41 is Critical; filtering only Critical can miss the bugcheck details. Report ID: `255aa7a2-bc1c-4b72-a4c1-beb7c2659e3d`.

The full validation run34 and the UI25 build with parallel C++ compilation overlapped until approximately 16:26:36 PDT. Their partial logs and outputs are preserved. The final flushed test marker names LinearInteger scaling, but later sample timestamps mean it cannot identify the precise faulting test. B1 balloon tests had completed earlier without assertion or validation failure. Ordinary scaling does not dispatch B1 balloon shaders. No concrete invalid access was found in the bounded source review; that does not exclude a renderer-triggered driver defect or earlier memory corruption.

No matching WHEA, display/NVIDIA, resource-exhaustion, volume or disk event was returned in the inspected 15:50–16:27 window. This is absent supporting evidence, not proof of healthy memory, a driver or hardware. No crash-time memory-usage recording exists. Neither concurrency nor the later clean757-test run establishes causality.

All active build/test processes were stopped after the owner's report. The suite had already been restarted before the report and completed757 tests; UI26 was stopped before completion. Agents stopped implementation and performed read-only timeline/source reviews.

Evidence: `obj/vulkan-parity/crash-2026-09-19/` contains event exports, the initial failed dump reads, and successful administrator-authorized `dump-analysis.txt` / `dump-followup.txt` with completion receipts. `obj/vulkan-parity/bsod-workload-timeline-01/` contains pinned workload timings and source hashes. Only public Microsoft symbols were downloaded; the dump stayed local and no system setting changed.

The dump dates the crash to **16:26:37.183 PDT**. The active process was **cl.exe**, started approximately 33 ms earlier. The faulting stack is `nt!PsQueryStatisticsProcess` → `nt!ExpCopyProcessInfo` → `nt!ExpGetProcessInformation` → `nt!ExpQuerySystemInformation` → `nt!NtQuerySystemInformation`. No Vulkan or graphics-driver frame appears on this stack. This identifies the crash context, not the component that originally caused corruption.

The follow-up reconstructs the nonvolatile register omitted from the initial trap: `rsi=ffffb386b473c660`, with `add ebp,dword ptr [rsi-25Ch]` reading unmapped address `ffffb386b473c404`. A mapped address differs by one bit, but this alone cannot diagnose faulty hardware. Reported available RAM was about 38.7 GiB and commit about 41.4 GiB of 75.3 GiB; these figures do not support memory exhaustion. Some historical counters are anomalous and a session-list read is incomplete, so not every dump metric is reliable.

Disposition: **unclassified kernel memory fault during process-statistics query in compiler context**. Independent agent review found no concrete renderer defect established by this evidence. No driver, firmware or memory replacement is justified by this incident alone. Reduced concurrency improves attribution and reduces overlapping load; it is not a verified fix.

Controlled resumption: `obj/vulkan-parity/post-crash-isolated-smoke-01/summary.json` records four passing isolated tests (three GPU balloon pipeline cases and LinearInteger display scaling), with unchanged build40 artifact hashes and clean synchronization validation. No compiler ran concurrently. This supports proceeding with bounded implementation checks, not crash exoneration or complete parity. The next UI build explicitly disables compiler `/MP`; builds and rendering checks run sequentially.

- [x] Stop builds/render tests and preserve partial evidence.
- [x] Find authoritative bugcheck event and dump location.
- [x] Correlate source/build/test timeline without assigning causality from log tails.
- [x] Read the dump under administrator permissions; inspect faulting instruction, module, process, stack and memory state.
- [x] Bound the finding without claiming a root cause; no concrete renderer defect was identified to fix.
- [x] Independent agent review and owner acceptance of controlled resumption; do not treat successful repetitions as crash exoneration.
- [x] Complete a small isolated validation check before resuming implementation qualification.

Parameter interpretation follows [Microsoft's bugcheck0x50 documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x50--page-fault-in-nonpaged-area): invalid system-memory read, with no valid active page-table entry. These parameters alone do not name the responsible driver or prove memory exhaustion.
