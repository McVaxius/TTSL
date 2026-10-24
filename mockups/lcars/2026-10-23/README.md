# Thick Thighs Save Lives LCARS concepts

Approved LCARS design concepts. Implementation is selected for the repositories in Z:\myrepos.txt, with the rollout tracked in Z:\!todo\2026-10-22GOATFUCK0002.md. These images are design references, not runtime or test evidence.

- [Main window comparison](01-main-regular-compact.png)
- [Settings and supporting owners](02-settings-mini-and-auxiliary.png)
- [Exact source-checked prompts](PROMPTS.md)

The image links are the planned sheet filenames; root review publishes the final generated artwork. Regular/compact compare spacing at the same readable text size. A separate Mini, wizard, access loader or browser view stays a separate owner. Selected supporting views are shown at useful scale; other source windows and conditional states retain their existing layouts and actions, rather than each receiving an illegible thumbnail. Generated text and sample values are proposals, not test results.

## Source scope

Source commit: `96f8dde8194b24d1b2e623d24c675818efecf8e7`. Existing dirty paths were inspected by count (0); none are included or changed by this mockup task.

TTSL has separate plugin, browser HUD, and server manager surfaces; this pair prioritizes the selected plugin and browser reference. No Mini exists in the inventoried plugin.

## Verified controls and owners

- TTSL/Windows/MainWindow.cs:167 — toolbar; Snapshot at 218; Open Web HUD at 296.
- TTSL/Windows/ConfigWindow.cs:47 — two Settings tabs; general controls at 95; radar fields at 144.
- TTSL/Windows/SetupWizardWindow.cs:137 — three setup modes and panel step at 170.
- server/ttsl_server.py — existing Web HUD browser; cpp/src/main.cpp — distinct native server manager, excluded from the plugin owner.

## Existing UI source inventory

- `TTSL/Windows/ConfigWindow.cs`
- `TTSL/Windows/MainWindow.cs`
- `TTSL/Windows/SetupWizardWindow.cs`

## Shared appearance proposal

Auto / Picker / LCARS remain in that order, with LCARS third. Preserve native actions, IDs, saved scope and semantic status colours. Krangle obfuscates identifying text. Proposed shared UI tones default off; titlebar speaker and Settings use the same value, with no volume slider. Browser controls remain in the browser header; existing script-free owners do not gain unsupported timed fade. This appearance contract is approved for the allowlisted rollout; implementation and local verification remain separate from artwork approval.
