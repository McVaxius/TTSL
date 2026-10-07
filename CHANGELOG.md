# Changelog

## Unreleased - Button sizing (I491)

- Use font-aware Toolbar sizing for ordinary buttons and reduce Main's Setup and Settings action heights. Preserve complete labels, fonts, native IDs/actions, requested widths and small/dense controls.
- Current Debug/x64 compilation passes. Final actual-product native checks pass 9329 assertions across 16 focused scenes and 96 pointer activations, with integer exit 0 in all 1 routes. Coverage uses English/Hindi captions, original exercised font roles, both densities, 100/150 percent scale and enlarged text; game/GPU acceptance remains separate.

## Unreleased - CJK atlas construction

- Request a 4096 x 4096 managed atlas and merge one bundled Noto CJK face per font role for the selected language, including Simplified and Traditional Chinese aliases. Preserve existing font sizes, glyph ranges, Windows and symbol fonts, and font lifecycle.

## Unreleased - Main window titlebar shortcuts

- Add Settings, Setup and the existing HUD visibility toggle to the native titlebar. Retain all body controls and re-read current HUD state on click, including while collapsed. Keep the original window identity and reserve native icon space during translated title painting.

## Unreleased - Community invite

- Update the existing Discord community action to https://discord.gg/ac6gjDvR8R.

## Unreleased - GitHub Actions dependency alignment

- Pin the existing AethertekUI Actions checkout to published revision `6c193cf06ac67f954c549cafc2033ac0efdd630a`, which includes the Hindi text host and renderer. This fixes missing-text-API compilation after a consumer is published before its library; local workflow validation and hosted build results are separate.

## Unreleased - Hindi across selected UI surfaces

- Append Hindi (`hi`, हिन्दी) to the managed HUD, native Windows server and both embedded browser pages, retaining every previous language ordinal and catalog value. Add the complete 461-key catalog through each existing resource route and extend native date formatting with `hi-IN`.
- Own the Windows shaped-text renderer with the existing managed appearance lifecycle, including its font-status window. Route translated text, measurements, native-label overlays, combo choices and Unicode input through the shared renderer while retaining original IDs, font-role sizes, native editing, saved settings and actions. Validate shaped glyphs before excluding Devanagari from the retained atlas checks. Game-rendered DTR text uses its existing English wording for Hindi; other selected-language DTR behavior is preserved.
- Retain native Windows/browser shaping and add Nirmala UI after existing browser fonts. Keep every embedded C++ UTF-8 literal within 12,000 bytes. Exact source key/nonempty/format-slot checks pass for all fifteen 461-key browser catalogs with matching Hindi resources; Python syntax, native Debug/Release builds and isolated Hindi checks for both browser backends pass. Managed Debug/x64 compilation passes with zero warnings or errors. Earlier focused Hindi selector, Config, Setup and Main checks passed with original fonts/IDs, both densities, 100%/150% scale, scrolling, sampled shaped ink and unchanged saved settings. The final frozen-library refresh builds cleanly; current compiled catalogs pass 33,444 assertions across all fifteen languages and the current Hindi selector/native controls pass 1,104 assertions with all seven original font roles and exact product/checker core bytes. Host/GPU/IME and user visual acceptance remain separate.

## Unreleased - Window appearance and transparency

- Add the Window appearance settings section with retained colour, compact and language controls, independent main-window visibility preferences, and a main transparency switch. Save opacity/fade preferences through the existing configuration: 100% normal, automatic 50% after ten unfocused seconds by default; clamp opacity to 10 to 100% and delay to nonnegative values. Apply opacity once after native End and motion restoration for each window tree, including chrome, owned content and images. Build/configuration checks and game acceptance remain separate.
- Extend the same preferences through the existing native server JSON and browser localStorage. Move native/browser colour controls into appearance settings while retaining main compact/language identities; reuse native focus/timer events and browser focus/blur/visibility events for complete-window/page fade. Preserve both browser backend routes and native/server actions. Retain existing native layered-window style, color key and alpha; compose opacity once and restore the original attributes when disabled or back at full opacity.


## Unreleased - translated status and panel bounds

- Reflow Operator's Situation cards into two columns within the client rail, keeping captions and notes inside their cards and retaining the full-width four-column overview. Extend the existing isolated browser check with complete caption bounds and the prior four-column rail as a negative control. Both intercepted backend partitions pass all fourteen locales/both densities, with 28 complete rail-caption cases each and the prior layout rejected; full surface and game acceptance remain open.

- Restore browser navigation and Inspector symbols, the Show Details switch thumb, Operator headings/count, Command party overview and counters, and Matrix's separate selected-entity/Inspector row with narrow-screen reflow. Preserve backend-specific CCTV/portrait routes, entity keys, permissions and raw values; unavailable party territory data stays unavailable. Append nine reached reference phrases to all fourteen catalogs and both browser dictionaries (453 keys), retain existing values, and bound the native page's 45 UTF-8 literals to 12,000 bytes. The unchanged managed launcher and native Debug/Release launcher pass; all fourteen current 453-key managed catalogs pass compiled checks, and both native products contain all 45 browser literals and 6,342 translation values. Both isolated browser partitions pass; full surface acceptance remains open.

- Give populated browser party rows all seven desktop grid tracks in both server mirrors, keeping Distance on its existing member row and retaining the narrow-screen layout. Both isolated backend partitions pass 28 populated party-row cases each, preserving raw slots/distance and rejecting the former six-track layout; broader live-data acceptance remains open.

- Constrain Setup's native auto-fit width to its existing 560-unit width before Begin, retaining automatic height, draft/navigation behavior and the original native identity. This prevents wrapped-content feedback from progressively narrowing the wizard at 150% scale. The unchanged launcher builds cleanly, and all fourteen languages pass 238,166 native assertions across 560 stage/reopen scenes, retaining safe draft edits, Back/Cancel, raw URLs and untouched saved settings. Finish and first-run dismissal are excluded; game acceptance remains separate.

- Skip unrelated status templates by their required authored prefix before running the existing bounded matcher. Use absolute anchors to retain complete terminal LF/CRLF details. This fixes the Vietnamese Settings Draw timeout while preserving template order, raw captures and the 20-ms limit. The unchanged launcher builds cleanly; current fourteen-language catalog/helpers pass 30,207 checks and complete Settings passes 216,158 assertions in 224 scenes. Complete Setup also passes; game acceptance remains separate.

- Center and wrap the empty radar caption at its original font in both embedded browser pages; require a real origin for its marker and retain populated-point coordinates. The isolated browser pass verifies 392 empty/populated canvas cases alongside 240 layout scenes. Confine managed card content and separators to its inner padding, restoring caller layout and clipping afterward. Fourteen-language Main checks pass at both densities, scales and widths; the unchanged native launcher rebuilds Debug and Release with the corrected page. Game acceptance remains separate.

- Translate Settings' missing-account fallback with Main's existing display helper while keeping real account IDs unchanged. Correct the retained Duty label in Brazilian Portuguese, Indonesian and Polish across the managed, native and browser catalogs. Source parity and current fourteen-language embedding and Settings checks pass; complete Setup passes and managed-host/game acceptance remain pending.

- Add the authored Yes/No condition values to all fourteen managed, native and browser catalogs. Retain every existing value, language ordinal, native control and runtime condition; exact source coverage passes at 444 keys. Unchanged launchers build managed Debug and native Debug/Release successfully; the isolated browser regression passes 240 desktop/narrow layout scenes, translated Yes/No lookup, seven accents and saved appearance restoration. Actual compiled native helpers pass 256 checks across fourteen languages. Current managed embedding, Main and Settings native checks pass; complete Setup passes and game acceptance stays separate.

## 2026-10-05 - Rounded outer window chrome

- Reserve translated label widths in Main snapshot, remote and equipment tables and use the approved denser metric rows. Keep radar labels inside their canvas while preserving the displayed names, positions, native IDs and saved preferences.
- Translate Main's unavailable area/account labels using the existing catalog. Preserve resolved place names, published account IDs and the original `Territory <id>` telemetry fallback.
- Show the existing TTSL project version in the native companion's title bar; native builds read that version from the managed project instead of maintaining a second version value. Preserve the window class and control identities.
- Adopt shared rounded chrome and native minimize in the managed Main, Config and font-status windows, preserving native IDs, constraints, saved geometry and actions. Setup retains NoCollapse and gains rounded chrome. Managed native and game verification remain pending.
- Retain the Win32/GDI companion's Windows frame, title controls and native minimize/restore. Request rounded DWM corners and theme-relative caption/border colours on supported Windows; round native actions, restore the server/client status strip and use semibold headings with matching surface fills. Preserve control IDs, callbacks, JSON preferences and runtime routes.
- Replace native white editor borders with measured rounded frames, draw existing checkboxes and the language selector in the approved appearance, and fill the retained action row when it fits. Route header painting through its actual list parent and reset non-status text colours so dates stay neutral. Preserve editing, focus, native choice strings, callbacks and saved values; compact mode retains a usable native minimum height.
- Align the native caption, padded configuration sections and measured status grouping with the approved references. Retain native list-view input while providing 47-pixel regular and 41-pixel compact rows; measure complete headings, names and dates, keep wider column widths and expose overflow through native horizontal scrolling.
- Refresh the existing server/client status after accent changes so a running server retains its live status and Stop Server action immediately. The focused native regression passes 25 assertions.
- Native Release build and the bounded owned-window check pass: 4,329 assertions across fourteen locales, both densities and large/narrow windows in 12.328 seconds, with 26.777 MiB peak owned-process memory. Verified dropdown/keyboard callbacks, JSON saves and restart restoration, localized dates, retained control IDs/bounds, OS minimize/restore and the DWM rounded-corner preference.
- Verify native colour-menu presets, custom RGB commit/cancel, relative and neutral palettes, independent status colours, input/action contrast and saved-value restoration in 392 scenes across seven accents, fourteen locales, both densities and large/narrow windows. Three bounded runs pass 10,051 assertions; the longest takes 57.937 seconds, with a 77.922 MiB combined checker/owned-process peak upper bound.
- Confirm the actual Segoe UI title and semibold heading/status font descriptions against captured native ink and locally cloned fonts. Inspected fresh English regular/compact, Japanese narrow and neutral/stopped GDI client-area renders against the approved native references.
- Actual native-window and available-monitor DPI is 96. Higher-DPI coverage and compositor/user visual acceptance remain pending; GDI captures do not establish DWM corner pixels or animation appearance.

## 2026-10-05 - Formatted-number font coverage

- Include the selected UI culture's number-group separator in the existing font requirements so mana and update-cadence values retain locale spacing. Preserve the original font roles, sources and merge order.

## 2026-10-03 - Approved UI adoption

- Preserve already-formatted service values, leading zeroes, empty arguments and numbered placeholders in translated managed UI messages; typed UI numbers retain selected-culture formatting.
- Add measured HUD panels and rectangular radar, cached AethertekUI colours and managed Segoe UI/CJK/symbol fonts, shared language/colour preferences, and compact mode through the existing configuration.
- Embed nine-language UI resources; keep native window/control IDs, account-owned publisher settings, command tokens, logs, and web permission checks.
- Replace fixed native-server geometry with a resizable Win32 composition, native appearance controls, real client columns, and locale-formatted timestamps.
- Apply the approved browser appearance to both page producers, add independent compact/language/colour preferences, and retain all four modes and five Inspector tabs.
- Complete 442 keyed phrases in every locale and synchronize the native/browser resources. Localize dynamic UI summaries, extraction and relay outcomes, native start-error wrappers, dates, counts and action tooltips while retaining raw diagnostic details and telemetry identities.
- Append Vietnamese, Brazilian Portuguese, Indonesian, Polish and Turkish to the retained nine language choices across the plugin, native monitor and both browser page producers. Embed 442 complete phrases per addition, extend native date locales and selection through the existing save paths, and retain original language order, raw commands, endpoints and permissions.
- Restore party headers, preserve the main controls' window ID root, size panel surfaces from measured content, and wrap translated toolbar groups. Keep the saved radar-size preference effective in the rectangular layout.
- Reflow managed fields and actions on exhausted rows; measure numeric editors with both step buttons and size DTR choices for their translated text while retaining native control identities.
- Use the shared monitor mesh in the HUD toolbar, add heading and job-role symbols, and show compass labels without changing game actions or status-colour meaning.
- Remove superseded per-mode browser palette overrides, theme native list headers, and reduce native title size in compact mode. Keep C++ page literals below MSVC's UTF-8 literal limit.
- Provision the sibling library in GitHub builds while retaining the native batch entrypoint and the two-ZIP public release contract.
- Keep version 0.0.1.16. Build/source checks are diagnostic; game, native, and browser visual acceptance remains pending.


## 2026-10-02 - Build and release repair

- Pin GitHub builds to SDK 10.0.201 and pass the downloaded Dalamud library path. Restore and build plugin projects with matching configuration, platform and runtime; stop on restore failure.
- Keep build tokens read-only and release writes in a separate job. Use packaged manifest versions for untagged releases.
- Local launchers build the plugin directly in the pinned environment and return its exit status.

All notable changes to Thick Thighs Save Lives will be documented in this file.

## [Unreleased] - 2026-07-25

### Added
- Added a one-time, fresh-install three-step setup wizard for Local HUD, Local + Web, and Web-only modes.
- Added setup entrypoints in the main window, settings window, and `/ttsl setup`, `/ttsl wizard`, and `/ttsl guide`.
- Added `latestServer.zip`, containing only `ttsl-native-server.exe`, beside the existing plugin release assets.
- Added a **Download Native Server** button to Remote HUD Server settings that opens the latest GitHub release and identifies `latestServer.zip`.

### Changed
- Configuration schema v8 now persists an install-wide initial setup-wizard marker; v7-and-earlier configurations migrate as already prompted, and later wizard access remains manual while preserving per-account dismissal and existing advanced settings.
- The release workflow now builds the native server through `cpp\build.bat Release` while leaving `latest.zip`, its manifest, and release-version behavior unchanged.
- The release workflow now pins its native build to the Visual Studio 2022-compatible `windows-2022` runner.
- GitHub releases now expose exactly two public ZIP assets, `latest.zip` and `latestServer.zip`; `TTSL.json` remains inside `latest.zip` and in the private build artifact used for version detection.
