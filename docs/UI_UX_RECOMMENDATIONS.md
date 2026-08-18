# Thick Thighs Save Lives UI/UX Recommendations

**Review date:** 2026-08-18  
**Scope:** UI code review only; no runtime behaviour or implementation changes are included in this document.

## Product goal

Keep essential character and party state legible when the normal game rendering setup is reduced, locally or through the web HUD.

## Reviewed surfaces

- `TTSL/Windows/MainWindow.cs`
- `TTSL/Windows/ConfigWindow.cs`
- `TTSL/Windows/SetupWizardWindow.cs`

## What is already working

- The two-column snapshot/party layout matches the product's monitoring purpose.
- The setup wizard stages mode, panels, and remote review without saving partial choices.
- Local HUD, DTR, Krangle, party enumeration, remote publishing, and radar controls are exposed consistently.

## Prioritized recommendations

| Priority | Recommendation | Rationale and completion signal |
| --- | --- | --- |
| P0 | Prioritize survival information over setup controls. | HP, MP, combat, duty, repair, and party danger should dominate the first viewport; move support links and infrequent setup actions to a compact overflow row. |
| P0 | Make the main layout responsive. | Switch the Snapshot and Party columns to a single vertical flow below a minimum width so tables and radar remain readable at small remote-window sizes. |
| P0 | Treat web permissions as a safety boundary. | Group text/slash commands, screenshots, and CCTV under a clearly labelled Remote access section with plain-language exposure summaries and an explicit disabled-by-default state. |
| P1 | Replace the remote-settings wall with Basic and Advanced views. | Keep server URL, connection health, Open Web HUD, and Download Server in Basic; move intervals, DLL path, launch details, and extraction notes to Advanced diagnostics. |
| P1 | Add actionable remote error states. | Alongside `Last error`, provide the next useful action such as retry, copy diagnostics, use local default, or open the server setup step. |
| P1 | Add a radar legend and live preview. | Show local player, party marker, range, combat/travel scale, and enumeration behaviour while radar settings change. |
| P2 | Do not rely on colour alone. | Condition, repair, publisher, and party states should pair colour with a stable icon or short text label. |

## Suggested information hierarchy

1. Critical vitals and conditions
2. Party and radar
3. Remote publisher status
4. Setup/settings actions
5. Advanced diagnostics

## Validation checklist

- A new user can identify the primary action and current blocker within five seconds.
- Every disabled control has a nearby plain-language reason and, when possible, a direct corrective action.
- Healthy, warning, error, running, and disabled states remain distinguishable without colour.
- The UI remains usable at narrow window widths and common Dalamud UI scales without clipped labels or unreachable controls.
- Destructive, global, or high-impact actions identify their scope and require confirmation or provide a safe undo.
- Empty, loading, stale-data, success, partial-success, and failure states each provide an appropriate next action.
- Settings clearly identify whether they apply globally, per account, per character, per preset, or only for the current session.
- Advanced diagnostics are still reachable but do not compete with the everyday workflow.

## Recommended implementation order

1. Implement P0 items and validate the primary workflow plus blocker recovery.
2. Implement P1 information-architecture and configuration improvements.
3. Apply P2 polish, then test at multiple UI scales with both fresh and mature configurations.
