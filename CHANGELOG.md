# Changelog

All notable changes to Thick Thighs Save Lives will be documented in this file.

## [Unreleased] - 2026-07-25

### Added
- Added a one-time, per-account three-step setup wizard for Local HUD, Local + Web, and Web-only modes.
- Added setup entrypoints in the main window, settings window, and `/ttsl setup`, `/ttsl wizard`, and `/ttsl guide`.
- Added `latestServer.zip`, containing only `ttsl-native-server.exe`, beside the existing plugin release assets.
- Added a **Download Native Server** button to Remote HUD Server settings that opens the latest GitHub release and identifies `latestServer.zip`.

### Changed
- Configuration schema v7 now persists setup-wizard dismissal per account while preserving all existing advanced settings.
- The release workflow now builds the native server through `cpp\build.bat Release` while leaving `latest.zip`, its manifest, and release-version behavior unchanged.
