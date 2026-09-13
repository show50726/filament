# Weighted blended transparency

Architecture issues and resolution tracking: [OIT architecture tracker (繁體中文)](ARCHITECTURE_TRACKER.zh-TW.md).

OIT is an opt-in, approximate coverage renderer. Request it with
`View::setOitEnabled(true)` and inspect `View::getOitStatus()` after rendering.
`isOitEnabled()` returns the request, not the effective result. Repeating the
same request preserves the last result; changing it resets evaluation.

## Supported behavior

The current implementation enables OIT on OpenGL / GLES at feature level 1
or higher with renderable RGBA16F and R16F targets. Other backends report
`UNSUPPORTED_DEVICE`. In particular,
this implementation does not claim a tested Vulkan or Metal OIT execution path.
FL0 uses ordinary transparency. FL0 material packages can also contain upgraded
GLSL shaders for use by higher-feature-level engines.

Opaque and masked materials remain in the ordinary color/depth pass. Eligible
transparent materials use two geometry passes, selected by dynamic specialization
constants in the same material shaders. Every ordinary and OIT fragment shader
has only output 0. There is no OIT material variant or unused MRT output.
OIT commands use a separate range of the same command buffer, sorted by material;
the two executors replay that immutable range without rebuilding the commands.

The variant layout matches main: seven bits, 61 valid keys, and eight-bit
serialized indices. Before user filtering, lit materials have 26 shader stage
entries and unlit materials have 18. Runtime programs are cached independently
for ordinary rendering, accumulation, and weight, together with the lighting
specializations. Two mutually exclusive boolean specialization constants select
the OIT passes. Depth, stereo, SSR, non-TRANSPARENT, refractive and FL0 materials
do not acquire OIT programs. The two extra dynamic key bits replace the two
removed variant bits, so the dense surface program-cache capacity is unchanged
from the preceding MRT implementation; this is not a measured memory saving.
Material version 79 rejects earlier packages; rebuild embedded and ubershader
assets. Command sorting returns to eight variant bits and twelve instance bits;
PrimitiveInfo remains 56 bytes.

Only TRANSPARENT materials enter OIT. FADE, ADD, MULTIPLY, SCREEN and CUSTOM
remain in the ordinary color pass. OIT overrides command depth writes to false
without changing the MaterialInstance. Nonstandard depth tests, active stencil
state, alpha-to-coverage and TWO_PASSES_ONE_SIDE remain in the color pass;
TWO_PASSES_TWO_SIDES contributes both faces without depth writes.

The entire View falls back for MSAA, stereo, custom render targets, enabled View
stencil buffers, channel depth clears, visible refraction, non-default render
channels, or non-default priorities / explicit ordering on TRANSPARENT primitives.
Opaque priorities (including the default skybox priority of 7) do not disable OIT. TAA alone
does not disable OIT. Device/backend restrictions above are unchanged.

Color-pass blended objects are composed before OIT, regardless of their relative
depths. This intentionally does not preserve arbitrary interleaving between the
two groups. Depth-writing color objects completely occlude deeper OIT fragments,
even when their alpha is small. OIT itself remains an approximation.

Eligibility is collected by the existing color command jobs using this View's
culled SoA data. When the View configuration allows OIT, jobs reduce candidate, refraction and
ordering flags locally and merge them once; there is no separate eligibility
traversal. Serial generation needs no atomic merge. Ordinary jobs use a separate
C++ specialization with no eligibility code or flags output argument. This does
not add material variants. Candidate commands temporarily carry the accumulation
program key; fallback clears that marker before preparing ordinary programs.
Raster state and sorting remain ordinary until the jobs join. The main-thread program preparation
traversal converts eligible commands only when the whole View can use OIT, then
callbacks are appended and the buffer is sorted and automatically instanced once.
Offscreen objects do not force fallback. Empty OIT command ranges allocate no
accumulation targets and perform no resolve. Fallback priority is device, MSAA,
stereo, unsupported View, refraction, then ordering. BLENDING and DEPTH_STENCIL
status enum values remain reserved for API compatibility. No per-frame warning
is emitted. The viewer displays the last effective status.

The benchmark tables below describe the committed implementation before this
two-pass revision. They are historical measurements, not performance
validation of the current implementation.

## Arithmetic and limitations

Let `p` be the already shaded, premultiplied RGB, `a` its coverage, and `z` the
reversed window depth. Compute in high precision:

```
w = (0.5 + 0.5 * z)^3 / 16
RGB += clamp(p, 0, a * 65504) * w / 256
T   *= 1 - a
W   += a * w
```

RGBA16F stores RGB and T; R16F stores W. Clear to `(0,0,0,1)` and zero,
respectively. The first pass writes RGBA16F with additive RGB and multiplicative
alpha; the second writes weighted alpha to R16F with additive red blending.
Each pass binds only one color attachment. Both keep the shared depth read-only.
Resolve restores the factor of 256 and outputs premultiplied RGB and `1-T`.
The background is loaded and blended in linear space before tone mapping.
Each command selects its ordinary or OIT program independently across Views.

The bounded weight and radiance scaling provide headroom for thousands of
bright layers, but FP16 sums and products are not exact. Very small alpha can
round transmittance back to one; large overlap can lose low-order contributions
or saturate. The resolve defines a finite saturated output beyond the normal
budget, not an accurate unlimited-layer result. Non-finite input RGB is removed;
non-finite coverage contributes nothing. The fragment output explicitly uses
high precision so GLES mediump assignment cannot truncate the weighted values
before FP16 attachment storage. Since the interface is shared, eligible ordinary
TRANSPARENT shaders now also use a highp output; this does not add an attachment.
Negative radiance and nonzero emission/specular at zero coverage are outside
this coverage model. High-alpha intersecting surfaces still blend colors rather
than providing exact nearest-surface occlusion. Refraction is not reconstructed.

Targets occupy 10 bytes per pixel before driver padding/compression. At 1080p
that is about 20.7 MB, with a hypothetical full store-plus-read of 41.5 MB per
frame. These are storage estimates, not measured DRAM traffic. The separate
passes and loss of color-grading subpass fusion can dominate mobile cost.

Material version is now 79. Recompile embedded and ubershader assets as well.
The loader checks the version before interpreting shader indices, rejecting
versions 71, 72, 73, 77 and 78 before reading their incompatible shader indices.

## Two-pass specialization implementation (2026-09-26)

The two-pass path replaces the MRT variant implementation at `57dafdf51`.
It removes the second shader output and second simultaneous color attachment,
not the second texture: resolve still needs both RGBA16F and R16F images.
Geometry, rasterization and material evaluation run twice. A compiler can remove
RGB-only work from the weight specialization, but no fixed speedup is assumed.
Both passes evaluate coverage through the same material path, preserving custom
alpha logic, discard, fog and existing non-finite handling. Lighting inputs are
explicit dependencies of each frame-graph pass.

Existing MRT benchmark tables below remain historical. No mobile speedup or
bandwidth reduction has been measured for this revision.

Windows Debug validation: the engine, matc/matinfo, gltf_viewer, benchmark_oit,
test_oit, test_filament and test_filamat targets build successfully. All 21 OpenGL
pixel tests, 10 focused cache/material-variant tests, the material stage-count
test, five parser tests, and six numeric/bindings checks pass. All 12 shader/package cases pass
(GLSL and Metal interfaces inspected; SPIR-V inspection unavailable). Old package
versions 71/72/73/77/78 are rejected. The first lit package compilation timed out
across a simultaneous roughly 34-minute jump in several process timers; the six
unfinished cases passed when rerun using `--case-filter`.

The existing glTF batch reports `requested=1 effective=1 status=11`. Against the
MRT capture from the preceding rebase, OIT changes 912 image bytes by at most one
8-bit level. A repeat of the two-pass capture changes 685 bytes, also by at most
one level. Ordinary transparency is not a stable screenshot baseline here:
old/new differ in 61814 bytes (maximum 65), and the same new binary repeated
changes 68475 bytes (maximum 49), with identical captured settings. Thus this
comparison supports OIT agreement within one level, not bit-exact preservation
of every ordinary frame. Focused fallback and single-layer pixel tests pass.
Captures and comparison data are in `out/two-pass-viewer/` and
`out/two-pass-viewer-repeat/`.

The complete desktop build was attempted and remains blocked by main's unchanged
`test_gltfio` inclusion of POSIX `unistd.h` on Windows. Using the same exception-test
exclusions recorded in the rebase section, core Filament runs 218 tests: 217 pass;
the remaining death test expects a different MSVC panic message. Utils again runs
318 tests: 313 pass, one skips, and the same four failures listed below remain.
Logs are in `out/two-pass-*.log`; these are not full-suite passes. No Android,
Metal/Vulkan runtime, WebAssembly, Release benchmark or mobile performance
validation was performed for this change. No commit or push was made.


## Main rebase (2026-09-26)

Rebased the six OIT commits onto `origin/main` at `ef1a133d6`. The backup is
`codex/backup-wboit-before-main-rebase-20260926` (`d43b6121f`). This rebase keeps
OIT bit `0x100` and MRT; the proposed two-pass/spec-constant conversion is deferred.

Conflict resolution preserves main's lighting specialization constants, stereo
bit `0x02`, SSR key `0x70`, non-indexed draw support, View-owned scene cache,
post-process material program preparation, and generated Android bindings.
Bit `0x80` is reserved. Material version 78 distinguishes this combined layout
from both previous OIT packages and main's eight-bit serialized indices.
The viewer test flag now uses main's command-line configuration; mesh samples
pass buffer sizes to the new MeshReader API. The batch camera uses 33 mm because
the new App reconfigures its lens after pre-render (zero produced an empty image).

Native Windows Debug validation:

- Engine, gltf_viewer, both built OIT samples, benchmark_oit, test_oit,
  test_filament, test_material_parser, test_filamat and test_utils build successfully.
  The full ALL_BUILD was attempted but main's unchanged `test_gltfio` includes
  POSIX `unistd.h` unconditionally and cannot compile on this Windows host.
- 21 OpenGL pixel tests pass, including indexed/non-indexed OIT equivalence.
  Three variant enumeration tests, three focused program-cache tests, five
  parser tests, six numeric/bindings tests, and generated Java/JNI static parity
  checks pass. Android native and WebAssembly builds were not run.
- All 12 shader/material-package checks pass, including rejection of versions
  71, 72, 73 and 77. Generated OpenGL and Metal output interfaces are inspected;
  SPIR-V inspection is skipped because this tool build lacks that capability.
  These checks do not establish Vulkan or Metal runtime correctness.
- The glTF batch reports `requested=1 effective=1 status=11`; its off/on PPM
  outputs differ in 215953 bytes. Outputs are in
  `out/rebase-viewer/positive-focal/`.
- The full Filament test run aborts at an exception-based BufferBounds test.
  Excluding `BufferBoundsTest.*Rejects*`, `VertexBufferTest.CanceledCreationRejected*`
  and FrameGraph's `WriteRead`, `Basic`, `ImportResource`, `SubResourcesWrite`
  runs 217 tests: 216 pass. The remaining
  `LocalProgramCacheRegressionDeathTest.SurfaceVariantOnPostProcessMaterialIsRejected`
  rejects the invalid key but expects a different panic message on MSVC.
- Utils runs 318 tests: 313 pass, one skips, four fail (also on focused rerun):
  `AllocatorTest.LeakDetectorWithLeaksOnRewind` (empty Windows callstack),
  `JobSystem.JobSystemLostWakeupRace` (timeout),
  `JobSystem.JobPoolExhaustionNullJobRun` (16383 vs 16384 slots), and
  `WinPathTest.Split` (drive-root representation). Utils source and tests are
  unchanged from main. These results are not full-suite passes.

Build and test logs are under `out/rebase-*.log`. Existing MSVC/linker warnings
remain. This rebase does not provide new mobile performance measurements.

## Regression checks

The 2026-09-17 variant revision passed the full desktop Debug build, 20 OpenGL
pixel tests, 3 variant enumeration/filter tests, 6 numeric tests, the material
parser test, and 12 material-package checks. The package checks cover FL0,
ordinary/OIT output interfaces, lit/unlit counts, refraction exclusion, other
blend modes, and rejection of versions 71/72. The real pre-change v72 package
also reports a version mismatch. The glTF viewer scene with skybox enabled
reports effective OIT and produces different off/on images.

Core Filament tests pass 125 cases when excluding four existing exception-based
FrameGraph tests: Basic, ImportResource, SubResourcesWrite, and WriteRead. The
unfiltered run aborts at WriteRead because the MSVC panic path checks
`__EXCEPTIONS` and aborts instead of throwing. Utils passes 175/176; the unchanged
WinPathTest.Split expects `C:\\` but receives `C:`. These are not full-suite passes.
Build logs contain existing MSVC/linker and SPIR-V inlining warnings. SPIR-V
inspection, native Metal execution, and mobile-device performance remain
unverified; the prior benchmark tables do not measure this variant revision.

On this Windows checkout, use the existing native CMake build (no Bash toolchain
was available on PATH):

```powershell
cmake --build out --config Debug --parallel 8
out/filament/test/Debug/test_oit.exe
python -m unittest discover -s test/oit -p test_*.py
python test/oit/check_shaders.py --matc out/tools/matc/Debug/matc.exe --matinfo out/tools/matinfo/Debug/matinfo.exe --loader out/filament/test/Debug/test_oit.exe --output out/oit-shader-checks --skip-spirv-inspection
```

Remove `--skip-spirv-inspection` when matinfo is built with Vulkan support.
This checkout has `FILAMENT_SUPPORTS_VULKAN=OFF`, so its dictionary reader
cannot decode SPIR-V, even though matc can compile Vulkan shaders. The check
reports this skip explicitly. Metal shader text is checked, but a Metal runtime
test still requires a suitable host. Package version rejection uses the real
GL loader; the NOOP backend intentionally bypasses the version check.

GPU tests use a hidden offscreen surface, assert readback completion and compare
pixels. They cover ordinary/OIT single-layer agreement, material-order changes,
masked occlusion, additive/FADE fixed-pass composition, command-only depth-write
overrides, special depth state in the color pass, visible-only eligibility,
refraction/ordering/MSAA fallback, viewport/scissor, lighting/shadows/SSAO/fog,
multiple Views, zero alpha and two-sided rendering.

Validation of the uncommitted routing changes: Debug test_oit build passed;
18 GPU tests and 6 Python tests passed. The shader-interface check caught FADE
also defining BLEND_MODE_TRANSPARENT; after explicitly excluding BLEND_MODE_FADE,
the rebuild, 3 affected GPU tests and all 6 shader/package cases passed.
SPIR-V inspection was skipped for the tool limitation above. Logs are in
out/oit-policy-build.log, out/oit-policy-gpu.log, out/oit-policy-focused.log and
out/oit-policy-shaders.log. Mobile performance was not measured for this change.

Historical validation of the previous committed revision: the Windows Debug
build succeeded (existing compiler/linker warnings
remain). The 13 GPU tests, 6 Python checks, 6 shader/package cases and material
parser test passed. Core Filament passed 125 tests with four throwing FrameGraph
cases excluded: `WriteRead`, `Basic`, `ImportResource`, `SubResourcesWrite`.
Running `WriteRead` terminates on its precondition instead of reaching the
expected exception. Utils passed 175/176 tests; the untouched `WinPathTest.Split`
expects `C:\\` where this host returns `C:`. These are not all-green core results.
Java/JNI and WASM compilation was not run because their toolchains were absent;
the Python binding checks verify API/enum parity, not compiler acceptance.

Transparent-background coverage is checked by compositing the transparent
intermediate over a preceding blue View and verifying the resulting red/blue
values. This does not depend on a window alpha channel, which the tested WGL
pixel format does not provide. The optimized-material configuration passed the
12 existing GPU cases and this additional focused compositing case.

## Repeatable performance measurement

Build **Release**, then run identical workloads with OIT off and on:

```powershell
cmake -S . -B out -DFILAMENT_DISABLE_MATOPT=OFF
cmake --build out --config Release --target benchmark_oit --parallel 8
python test/oit/benchmark.py --executable out/filament/test/Release/benchmark_oit.exe --output out/oit-benchmark --width 1280 --height 720 --frames 240
```

The default matrix is 1/8/32 layers, 25/100 percent coverage, unlit/lit and
OIT off/on. Each process warms up for 60 frames. CSV records CPU `render()`
submission time and available GPU frame durations, with sample counts and
median / p95. CPU time includes the renderer frontend, not just command
generation. GPU samples are deduplicated by frame id; unavailable timings are
`NA`. Material compilation and setup precede warmup; driver shader compilation
can occur during warmup. On Windows the benchmark overrides the hidden WGL
platform's presentation callback to avoid including `SwapBuffers` / vsync waits
in GPU timings. It still renders to the same back buffer. Record clocks, thermal
state, driver, resolution, revision and build configuration with results.

To compare `66ee6ab77`, build the same `oit_benchmark.cpp` against that revision
in a separate checkout, linking `filamat` and `filament`, **without** defining
`FILAMENT_OIT_HAS_STATUS`. The workload uses APIs available in that revision.
Pass its executable as `--legacy-executable`. Without it, the runner explicitly
reports that the original variant baseline was not measured. This also permits
checking the cost of the new shader interface while OIT is disabled.

For Android, build the same target for arm64 Release with the repository Android
toolchain, and run the executable through adb on a real GLES device:

```sh
./build.sh -q arm64-v8a -Pip android release
# Ensure FILAMENT_BUILD_TESTING=ON in this build's CMake configuration.
cmake --build out/cmake-android-release-aarch64 --target benchmark_oit
adb devices -l
adb push out/cmake-android-release-aarch64/filament/test/benchmark_oit /data/local/tmp/benchmark_oit
adb shell chmod +x /data/local/tmp/benchmark_oit
adb shell LD_LIBRARY_PATH=/data/local/tmp /data/local/tmp/benchmark_oit 1280 720 8 100 240 1 0
```

The seven positional arguments are width, height, layers, coverage percent,
measured frames, requested OIT and lit shading. If shared libraries are needed,
deploy them alongside the executable and set `LD_LIBRARY_PATH` accordingly.
Capture Perfetto and GPU-vendor counters to separate command generation,
accumulation, resolve, tile spills and external bandwidth. CSV alone cannot
measure those components. Half-resolution rendering and tile-local subpasses
are future optimizations, not enabled silently by this implementation.

No Android SDK / adb was found on PATH or in the checked standard SDK locations
during this work. Desktop results are not mobile acceptance evidence. A mobile
GPU, frame budget and representative content are still needed to approve
production enablement.

## Desktop measurement, 2026-09-13

[Raw CSV](desktop-results.csv) compares engine revision `4f95c2a87` (`current`)
with `66ee6ab77` (`legacy`) using exactly this benchmark source. Windows x64,
MSVC 14.51 Release `/O2`, RTX 5070 Ti, driver 591.86, OpenGL 4.5, engine feature
level 1, 1280x720, post-processing enabled, 60 warmup and 240 measured frames.
Both builds use `FILAMENT_DISABLE_MATOPT=ON` and Vulkan backend disabled, matching
this checkout; these are not fully optimized production shader builds. The
48 cases all produced 240 GPU samples. CPU build/test work had completed before
this run. GPU clocks were not locked and no thermal soak was performed; the
pre-run snapshot was 34 C, graphics 810 MHz, memory 405 MHz, power 20.12 W.
That snapshot does not describe clocks throughout the run.

| Full coverage workload | Traditional GPU ms | Original variant GPU ms | Current OIT GPU ms |
| --- | ---: | ---: | ---: |
| 1 layer, unlit | 0.063 | 0.145 | 0.451 |
| 8 layers, unlit | 0.091 | 0.279 | 0.423 |
| 32 layers, unlit | 0.258 | 0.551 | 0.605 |
| 32 layers, lit | 0.340 | 0.560 | 0.655 |

These are medians; the CSV includes p95, coverage, CPU frontend times and both
versions with OIT disabled. The corrected implementation has a measurable GPU
regression relative to the experimental variant implementation, especially at
low layer counts. It does not establish mobile acceptability or a speedup from
removing variants. The baseline has known rendering correctness defects, so
these timings do not establish equivalent output quality either. Per-pass GPU
counters are needed to attribute the difference to shader export, blend units,
uniform updates or synchronization; the frame timer cannot distinguish them.

The final GLES interface audit found and corrected implicit mediump fragment
outputs. The revision above identifies these numbers before that correction;
the [post-correction run](desktop-final-results.csv) uses engine `da7ec3b3d`,
otherwise the same settings, with a 35 C / 802 MHz pre-run snapshot. It retains
the same observed regression.

Two small controlled experiments also used `da7ec3b3d` with identical shaders:
an independent per-View OIT uniform buffer, and an RG16F weight attachment.
Neither improved this run, so both changes were reverted. In the
[experiment CSV](desktop-experiments.csv), `current` is the named experiment
and `legacy` is the unmodified `da7ec3b3d` executable, not `66ee6ab77`.
For 8 unlit full-coverage layers, snapshot / control medians were 0.501 / 0.363
ms; RG16F / control were 0.545 / 0.491 ms. These uncontrolled-clock experiments
justify declining those changes, not concluding that uniform traffic or formats
never matter on other GPUs.

## Final optimized measurement, 2026-09-14

[Final CSV](desktop-optimized-results.csv) is the primary performance result.
Both `da7ec3b3d` and `66ee6ab77` were rebuilt with
`FILAMENT_DISABLE_MATOPT=OFF`, enabling offline optimization of embedded
materials as well as Release C++ compilation. Other workload/build conditions
match the previous run. All 48 cases returned 240 GPU samples; no builds or
tests ran concurrently. The pre-run snapshot was 36 C, graphics 990 MHz,
memory 810 MHz, 34.60 W. Clocks remained unlocked.

| Full coverage workload | Traditional GPU ms | Original variant GPU ms | Final OIT GPU ms |
| --- | ---: | ---: | ---: |
| 1 layer, unlit | 0.057 | 0.175 | 0.397 |
| 8 layers, unlit | 0.091 | 0.284 | 0.430 |
| 32 layers, unlit | 0.256 | 0.547 | 0.609 |
| 32 layers, lit | 0.339 | 0.559 | 0.677 |

Final OIT GPU medians span 0.397-0.677 ms across this matrix. CPU `render()`
medians with OIT enabled span 0.035-0.052 ms. Enabling material optimization did
not remove the observed regression. The work removes material variants and
fixes rendering semantics; it does not demonstrate a performance win over the
experimental variant path. Retain opt-in deployment until representative
mobile profiling establishes an acceptable frame budget. No measured bandwidth
or per-pass GPU counters were available, and no Nsight CLI was found on PATH;
the bottleneck and mobile acceptability remain unverified.
