# Whitegram application icons

Desktop uses `desktop-master.png`, edited with the built-in image_gen tool from
the user's `ayugram-reference.png` on 2026-11-18. It follows the reference's rounded
airplane silhouette with a white W ribbon cut through the black mark. The exact
initial generation prompt and final W correction instructions are recorded in
`desktop-prompt.txt`. The corrected W restores the previous design's lower,
level valleys and narrow tapered ends instead of the first edit's rising band.

`light-master.png` and `dark-master.png` retain the previous design and are still
used for Android. The dark master was produced with the built-in image_gen tool
using the prompt in `dark-prompt.txt`.

## Desktop (Windows)

- `whitegram_light`: default for settings without a saved icon selection.
- `whitegram_dark`: optional dark icon.
- `whitegram`: original icon, unchanged and still selectable.
The three icons appear in Whitegram → Appearance → Application icon. The approved
light and dark artwork replaces the old pair. Saved comparison selections map to
the corresponding light or dark option. The executable uses the light icon. The runtime PNGs have circular backgrounds
and transparent corners; ICO files contain 16, 20, 24, 32, 40, 48, 64, 96, 128
and 256 px frames.

Changes live in the existing `ayu` icon picker/settings implementation. The
source atlas does not currently name an area covering this fork-specific tree.

## Android

The existing `DefaultIcon` component now uses the light design. The new disabled
`WhitegramDarkIcon` component is selected by the existing app icon picker in
the appearance/chat settings. Both are available without Premium. Other icon
components remain available; their saved Android component states are retained.

Resources include five density sets, round and rounded-square legacy icons,
adaptive backgrounds/foregrounds and a monochrome layer. The application
metadata uses the light version in debug, release, SDK23 and standalone flavors.
Labels are supplied in English and Russian through `ayu.xml` so they also enter
the app's generated localization assets.

## Regeneration and checks

Run `powershell -NoProfile -ExecutionPolicy Bypass -File export.ps1` from this
directory to regenerate the desktop icons, embedded light icon and Android resources.
`ExportIcons.cs`
normalizes near-black/near-white noise, retains edge antialiasing, and applies
platform masks. Desktop exports use the desktop master's alpha mask for both
colors, preserving exactly the same silhouette and W cutout. The mark is fitted
uniformly into the supplied reference's 312 × 312 px box on its 640 px canvas,
centered at (300, 341). This supersedes the earlier estimate from the tiny taskbar
screenshot. The generated silhouette is a close visual match, not an exact pixel
trace. Aspect ratio is preserved. Android exports retain their original masters
and placement.

`install-android.ps1` is the one-time integration script for the original
`D:\Whitegram4A` checkout. It intentionally refuses a repeat installation.
For later artwork-only updates, copy the generated `android-res` files to
`D:\Whitegram4A\TMessagesProj\src\main\res`.

Run `powershell -NoProfile -ExecutionPolicy Bypass -File verify.ps1` to check
PNG sizes/transparency, all ICO frames, hashes of the preserved original icon,
Qt resource paths, launcher defaults, and Android build flavor metadata.

## 2026-11-18 desktop reference artwork validation

- Regenerated the two comparison PNG resources and their 20 light/dark ICO frames.
- `verify.ps1`: passed after regeneration.
- Compared light/dark alpha and inverse silhouette pixels at all ten ICO sizes:
  passed (up to two levels of rasterization rounding in opaque pixels).
- Visually checked the exported 32 px ICO frames and enlarged before/after views.
- The original three desktop variants remain available with their original files.

## 2026-11-19 comparison build

- Added `whitegram_ayu_light` and `whitegram_ayu_dark` as separate selectable icons
  in the second row of Whitegram → Appearance → Application icon.
- Restored the original light/dark resources and embedded executable icon so
  existing choices keep their previous appearance during comparison.
- Release build completed successfully with `cmake --build out --config Release
  --target Telegram --parallel 6`. Output: `D:\WhitegramProject\out\Release\Whitegram.exe`.
- `verify.ps1` passed for all four light/dark variants (40 ICO frames), Qt resource
  paths and the unchanged original legacy icon. The new UI was not launched.
- Local build only; nothing was uploaded or deployed to the VDS.

## Original integration validation results (before the 2026-11-18 size adjustment)

- `verify.ps1`: passed.
- Desktop: `cmake --build out --config Release --target Telegram --parallel 6`
  completed successfully. Output: `D:\WhitegramProject\out\Release\Whitegram.exe`.
  The extracted 32 px executable icon matches the light PNG pixel for pixel.
- Android: `:TMessagesProj:compileDebugJavaWithJavac` and
  `:TMessagesProj_AppStandalone:processAfatStandaloneResources` completed
  successfully with Gradle offline (272 tasks). The merged standalone manifest
  has the light application icon, one enabled `DefaultIcon`, and the disabled
  selectable `WhitegramDarkIcon`.
- The applications were not launched and no saved user icon preference was changed.
  Android APK assembly/installation was not part of these validation tasks.
