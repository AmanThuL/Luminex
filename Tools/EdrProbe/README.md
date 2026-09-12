# Isolated EDR display probe

This branch is experimental and must never merge. It changes only the display/presentation boundary;
scene, exposure and temporal algorithms remain the baseline. The normal editor renders an SDR
reference without `--edr`, and extended-linear sRGB with `--edr`. Both use a float intermediate in
this experiment; only the EDR drawable is float. The probe does not establish production SDR cost.

## Build and run

Fetched dependencies and assets may be symlinked from the production checkout, **except ImGui**:
copy ImGui privately before applying `Tools/EdrProbe/imgui-linear.patch` with `patch -p1`.
Never patch the production checkout or a shared ImGui symlink. Normal setup restores its own
maintained patches, so reapply this experiment patch after setup.

```bash
xmake f -P . -m release -y
xmake build -P . App
cd build/macosx/arm64/release
MTL_DEBUG_LAYER=1 ./App --windowed --scene material-lab --calibration --edr
```

For the SDR window run the same command without `--edr`. `Tools/EdrProbe/launch.py --output <new-dir>`
creates independent temporary app bundles under `/tmp` for the pair so their logs, captures and workspace files
cannot overwrite one another. It opens both windows and records process IDs. Close each normally.
Only one main window per process is extended range; detached Render Graph windows remain SDR.

## Observe

Place the two windows on the built-in XDR display. The probe panel identifies their domain and
shows SDL/NSScreen current headroom, potential/reference headroom and the smoothed output peak.
Top calibration patches are 0.25, 0.5, 1, 2, 4 and current peak, clipped to the selected peak.
Middle is a linear 0–4 ramp; bottom is the proposed tone-mapped 0–4 ramp. Turn calibration off to
compare MaterialLab or select another scene. The white probe text is authored at SDR white 1.

At low, medium and high display brightness in the default and reference presets, record which
patches are distinguishable, whether white text matches SDR white, headroom values and time to
settle. Photograph the actual display for extended-range visibility evidence; ordinary screenshots
must not be treated as proof of physical brightness. Observe changing brightness and moving the
window where a second display is already available. A second display is not required.

The Force headroom 1 checkbox exercises range fallback while retaining a linear float layer; it
does not exercise drawable recreation or an actual OS headroom event. Unsupported float swapchain
creation falls back to the encoded SDR drawable. UI colors are decoded only for the float pipeline;
font white stays 1 and the scene texture remains linear. Encoded-space and linear-space alpha
blending can still produce different edge/background appearance; this requires observation.

The Save display float readback button writes uniquely named `.rgba16f` files and JSON sidecars
containing view, transfer, primaries, dimensions, peak/reference white and frame number. Frame 64
also saves one automatically. Each pixel is four little-endian binary16 channels, top down, no UI.
Take a macOS window screenshot separately to compare what the operating-system capture preserves.
Logs contain ten retired display/UI pass timings starting after frame 32 plus periodic headroom
and HDR event observations. These raw samples are not a speedup or production-cost conclusion.

## Evidence limits and policy questions

The specified formula `peak * PbrNeutral(input / peak)` shifts midtones because Neutral includes
a black offset; it does not preserve the SDR midtone value exactly. The probe intentionally tests
that formula, without changing scene-linear or exposure inputs. Peak follows NSScreen current
headroom, rises with a 0.5 s time constant and drops immediately to stay within current headroom, capped at four reference whites.

SDL 3.4.12 reports potential headroom when NSScreen current headroom is at most one:
[upstream Cocoa implementation](https://github.com/libsdl-org/SDL/blob/release-3.4.12/src/video/cocoa/SDL_cocoamodes.m).
Consequently SDL headroom alone does not prove currently visible extended range. The short smoke
run observed SDL 16 against NSScreen current 1/potential 16/reference 0. The final probe uses
NSScreen current headroom for the actual peak policy, and retains SDL only for observation. The UI shows both values deliberately. The earlier short smoke used SDL
as its provisional policy source; its >1 float readback does not validate the final current-headroom policy.

The synthetic ramps are display-only probe patterns, not the production half-float golden fixture.
App offscreen screenshot/sequence commands and the production test expectations are not supported
on this branch after the display target changes to float. Validate the windowed probe specifically;
production tests run on the production branch. Raw evidence stays outside this worktree.
