# Production shared-service UI lifecycle review

Both `production-ui-lifecycle-01` and fresh `-02` pass: 12 main-window captures are byte-identical to the frozen loaded-asset baseline in indexed pixels, physical RGBA and PNG files. This qualifies the bounded production owner/factory lifecycle through real SDL/Vulkan window recreation. No divergence or exception was introduced.

The independent review rehashed both summaries, all per-capture buffers/reports/PNGs, both auxiliary outputs, the lifecycle reports, validation settings/logs and every recorded input pin. It also checked UI34's successful stable build receipt, relevant production source hashes, build log, linked artifacts and runtime DLLs. Build44 supplied the receipt-qualified shaders. Exact hashes and observations are in `vulkan-production-service-lifecycle-review.json`.

Each process completed this sequence at tick 1215, camera rotation 0 / zoom 0 / view position [-504,-229], with 960x640 logical and physical output:

| Phase | Renderer | SDL window ID | Shared owner |
| --- | --- | --- | --- |
| Initial | Software | 1 | Uncreated |
| First Vulkan | Vulkan | 2 | Created |
| After auxiliary image | Vulkan | 2 | Same context/device |
| Recreated window | Vulkan | 3 | Same context/device |
| Return to software | Software | 4 | Same context/device retained |
| Return to Vulkan | Vulkan | 5 | Same context/device |

All five post-creation observations in each process report the same nonzero context/device identity. Auxiliary rendering leaves the main window unchanged. Both 64x64 owned auxiliary results independently match every expected byte: palette index/RGB 17 outside the inclusive rectangle x=7..29, y=11..43; 37 inside; alpha 255 throughout. Submission, target and target-generation identities are nonzero. The fresh process reproduces the same image hashes.

I manually inspected the frozen image, all six first-process phase images, the fresh final Vulkan image, and both auxiliary PNGs (10 images). Ferris-wheel spokes/supports, entrance towers/banner, grass and cliff boundaries, fences, toolbars and translucent bottom panels match. The auxiliary PNG shows the expected lighter rectangle on a uniform dark-grey field. There is no visible stale surface or auxiliary-image contamination after recreation or renderer switches.

Both runs activated Khronos validation with synchronization validation enabled and emitted no validation warning, error, VUID or synchronization hazard. At explicit Context destruction, the observer drops its strong owner reference; weak owner and device references expire while an unsubmitted recorder remains alive. SDL video is stopped, and a late submission fails with `Offscreen service is shutting down`. The retained recorder therefore does not prolong the device or SDL loader lease.

Reviewed production wiring agrees with these observations: `Ui.cpp` passes the same owner to its persistent drawing factory and configured render-service factory; recreated presentation surfaces reuse the owner context; Context drains auxiliary work; the service joins its worker and clears its provider; UiContext releases the drawing factory before quitting SDL video.

Limits remain explicit. Device observations and source wiring are not interception/counting of every Vulkan device creation. Shutdown checks final weak-reference/SDL state, without a timestamped destructor-order trace. The diagnostic UI driver uses the real production factories but does not execute the ordinary launcher. The synthetic auxiliary pattern does not cover every offscreen caller or giant screenshots. These paused ordinary-paint baseline captures do not establish native category completeness, vsync/TPS performance, global Gate P, or permission to remove software. Production configured CLI qualification is recorded separately after its own captures pass.
