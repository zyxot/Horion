# Modern Bedrock compatibility work

This branch is for bringing the archived Horion source forward to current Minecraft Bedrock for Windows.

## Current state

The original source builds successfully, but a DLL built from the archived code crashes current Minecraft Bedrock shortly after injection. The likely causes are stale signatures, vtable indices, object layouts, and assumptions from the old UWP-era game.

Minecraft for Windows later moved to the newer GDK distribution/storage model, so the first phase is crash-safe startup diagnostics before changing hooks blindly.

## Diagnostic log

This branch writes `logF(...)` messages to:

`%TEMP%\HorionCompat.log`

To open it:

1. Press `Win + R`.
2. Enter `%TEMP%`.
3. Open `HorionCompat.log`.

Delete the file before each test if you want a clean trace.

## Test procedure

1. Build `Release | x64`.
2. Start Minecraft Bedrock and wait at the main menu.
3. Use the source-built injector's local-DLL option.
4. Select the newly built `Horion.dll`.
5. If Minecraft closes, open `%TEMP%\HorionCompat.log` and use the last line to identify the failing initialization stage.

## Modernization rules

Do not copy anti-debugging, security-bypass, or arbitrary third-party injector code into this fork. Compatibility changes should be limited to game-version signatures, layouts, hooks, rendering/input integration, and crash-safe diagnostics.
