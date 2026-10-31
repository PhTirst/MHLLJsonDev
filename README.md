# MHLLJsonDev

Development-time JSON and TTF resource generation for **Manhunt 1**, built on [Fire-Head/MHLL](https://github.com/Fire-Head/MHLL) 1.3.

[中文说明](README.zh-CN.md) | [Upstream instructions](docs/UPSTREAM.md) | [Provenance and licensing status](docs/PROVENANCE.md)

[![Windows x86 build and test](https://github.com/PhTirst/MHLLJsonDev/actions/workflows/build-test.yml/badge.svg?branch=jsondev)](https://github.com/PhTirst/MHLLJsonDev/actions/workflows/build-test.yml)

**Development preview:** the default `jsondev` branch contains these additions; `main` is retained as the upstream baseline. The initial source release does not bundle prebuilt ASI binaries. See [release notes](CHANGELOG.md) and [verification boundaries](docs/VERIFICATION.md).

This is a development plugin, not a replacement name for standard MHLL. It generates ordinary GXT + DAT + DXT3 TXD assets before MHLL loads a language. For a player release, remove the development inputs and use standard `MHLL.asi` with the generated assets. Do not load both plugins simultaneously.

## Status and Scope

- Fresh fork based on upstream commit `ae523f162480f4200f470ef2120bb27a88cfc620`; the upstream 1.3 Unicode hooks are preserved.
- Language-neutral JSON selection, optional CJK word-break workaround, strict TTF discovery, in-memory compilation, cache and synthetic regression tests.
- Windows x86 / MSVC. No proprietary RWSDK required; a minimal ABI declaration header is included.
- No game assets, actual translations, fonts or ASI loader are bundled.
- Upstream has no explicit license at this snapshot. See [distribution boundaries](docs/PROVENANCE.md) before publishing modified binaries.
- Automated tests are not a substitute for testing the hook in the supported game executable.

## Build and Test

Install Visual Studio 2022 with Desktop development with C++ and a Windows SDK, plus CMake 3.20 or later. From this repository:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Build products:

| File | Purpose |
| --- | --- |
| `build/bin/Release/MHLLJsonDev.asi` | Development plugin: JSON/TTF -> generated resources at startup |
| `build/bin/Release/MHLL.asi` | Standard loader: no JSON parser, TTF renderer or generation code linked |
| `build/Release/JsonDevTests.exe` | Standalone tests; does not launch or patch the game |

The GitHub Actions workflow builds and tests Debug and Release on Windows x86. It does not publish a release or game/font assets. There are no Premake build steps.

## Language Package Layout

Copy a language package under the game's `MHLL/` directory, and install one compatible ASI loader separately.

```text
Manhunt/
  MHLLJsonDev.asi
  MHLL/
    MyLanguage/
      lang.ini
      JsonDevConfig.json
      Json/
        pc_game_fr_FR.json
        pc_jury_turf_fr_FR.json
      TTF/
        console/  <exactly one .ttf>
        manhunt/  <exactly one .ttf>
        lcd/      <exactly one .ttf>
        loading/  <exactly one .ttf>
      BaseFont/   <optional user-supplied workspace>
        font.xml
        console.png
        manhunt.png
        lcd.png
        loading.png
      Text/                   <generated>
      font_fr_FR.dat          <generated>
      font_fr_FR.txd          <generated>
      .jsondev_manifest.json  <generated>
      JsonDev.log             <only when an error is reported>
```

`MyLanguage` is an arbitrary package directory, not a language detector. The four TTF subdirectory names select **font roles**, not a specific font family. Keep the font's original filename. A role folder may contain notes, but must contain exactly one regular file whose extension is `.ttf` (case-insensitive). Subdirectories are not searched. Zero or multiple TTFs is an error, including on a cache hit; the first file is never silently selected. All four roles are required when JSON generation is active. The same font may be copied into different role folders.

[examples/MHLL/Example](examples/MHLL/Example) demonstrates the layout and multilingual JSON schema. Its text is synthetic and intentionally incomplete: it is NOT a ready-to-play translation pack. Add your own fonts and complete game text before using it in the game.

## Select a Language Column

`lang.ini` keeps the standard MHLL settings. JsonDev additionally accepts `JsonLanguage` to select any named JSON column:

```ini
[LANG]
NameKey = MyLanguage
TextureFile = font_fr_FR.txd
DataFile = font_fr_FR.dat
TextDirectory = Text\
GameGXTName = pc_game_fr_FR.gxt
LevelGXTPostfix = _fr_FR
JsonLanguage = fr_FR
UseWholeAsciiTable = 1
WindowsLangID =
```

Selection order: nonempty `JsonLanguage`, otherwise `LevelGXTPostfix` with one leading underscore removed, otherwise the suffix in `pc_game_<language>.gxt`. No hardcoded `zh_CN`, source-language name or column count. The selected column name is case-sensitive. Package/output paths should use names supported by the game's Windows ANSI file API; use ASCII folder and output names for portable packs. TTF filenames and core test paths support Unicode.

Supported JSON shape, UTF-8 (an optional UTF-8 BOM is accepted):

```json
[
  { "Key": "PLAY", "en_US": "Start", "fr_FR": "Démarrer", "zh_CN": "开始" },
  { "Key": "SAMPLE", "en_US": "Sample", "fr_FR": "Exemple", "ja_JP": "例" }
]
```

Only `Key` and the selected language are compiled; other columns and metadata are ignored. Every row must have both as strings. Empty translated strings are allowed. `Key` is 1-8 printable ASCII bytes; duplicate/overlong keys are rejected, never truncated or treated as CSV headers. A legitimate key named `KEY` is retained. GXT keys are sorted for the game's lookup; source JSON row order is never rewritten. Missing translations do not silently fall back to English.

`Json/<name>.json` produces `Text/<name>.gxt`: filenames are retained, with only the extension replaced. The basename/suffix must already agree with `GameGXTName` and `LevelGXTPostfix`; changing the column does not rename files. If there are no JSON files, JsonDev leaves existing GXT/DAT/TXD alone and uses standard MHLL loading. If only some GXT files have JSON sources, the other existing GXT files remain untouched. Active generation requires a local `TextDirectory`, not `%GAMEPATH%`.

## What Happens at Startup

1. Read the language package, target column and typed JSON config. Check all four TTF directories before any generated output is changed.
2. Fingerprint the builder version, language, config, JSON contents, TTF contents and optional BaseFont inputs. Matching manifest plus all expected output files means no rendering. Deleting one generated file triggers a rebuild; there is no need to delete the manifest.
3. Parse every JSON and compile the selected values to Manhunt 1 UTF-16LE GXT buffers in memory. Apply only explicitly enabled text transformations and collect actual display codepoints, excluding `~...~` control tags.
4. For each font role, reuse BaseFont glyphs if enabled, preserving their advance widths. Render only missing glyphs from that role's TTF. With BaseFont disabled, render all needed glyphs from TTF. Basic ASCII is included for untranslated text and dynamic game values.
5. Pack atlases and build DAT and DXT3 TXD in memory. DAT role order stays Manhunt, Console, LCD, Loading. TXD always contains `manhunt`, `console`, `lcd`, `lcdm`, `loading`; LCDM duplicates LCD exactly.
6. Stage the final file buffers, then replace GXT/DAT/TXD and write the manifest last. Validation/render errors leave prior outputs intact. Ordinary replacement failures are rolled back; this is not a power-loss-safe multi-file database transaction.
7. Continue through the normal MHLL GXT/DAT/TXD loader, including the upstream 1.3 Unicode fixes.

There are no glyph PNG, atlas workspace or per-character intermediate files in the default path. Temporary final-file siblings are used only while committing outputs. Do not manually edit generated assets while relying on the cache: rebuild from the inputs instead.

## Optional Zero-Width CJK Wrapping

The generic default is **off**:

```json
{ "useBaseGlyphs": false, "zeroWidthWrap": false }
```

For Chinese, Japanese and Korean packs, `"zeroWidthWrap": true` is recommended to avoid the game's long-word hyphenation/replaced-character behavior. The [CJK example config](examples/JsonDevConfig.cjk.json) enables it; it also expects a user-supplied BaseFont workspace.

This is an ASCII-space workaround, NOT insertion of Unicode U+200B:

- With the option off: text is unchanged; U+0020 keeps a normal advance.
- With it on: eligible CJK boundaries get U+0020 break delimiters, and **all four font roles** give U+0020 zero advance.
- Real U+0020 spaces already in JSON become U+2007 only (no trailing U+0020). U+2007 uses exactly the normal U+0020 advance from that font role, so English word spacing remains visible.
- Han characters, kana and Hangul syllables are covered, with basic opening/closing punctuation restrictions. This is not a full Unicode line-breaking/shaping engine.
- Control tags such as `~blue~`, `~colour~`, existing `~colur~` and newline tags are copied exactly. No language-specific punctuation correction or whitespace trimming around tags is done implicitly.
- Changing the option rebuilds GXT and fonts together. Always keep clean, unescaped source JSON; do not feed already-converted GXT text back as source. Disabling the option does not reverse transformations manually embedded in JSON.

Other languages can leave it off. Complex-script shaping, bidirectional layout, Arabic/Indic joining and non-BMP glyph rendering are outside this project's scope. NUL/non-BMP selected text is rejected rather than generating misleading assets.

## Configuration Reference

All settings are in the language package's `JsonDevConfig.json`. Omitted settings use these defaults; unknown names and wrong types are errors.

| Setting | Default | Meaning |
| --- | --- | --- |
| `zeroWidthWrap` | `false` | Coupled GXT/font space workaround described above |
| `useBaseGlyphs` | `true` | Require and reuse the user-supplied BaseFont workspace |
| `baseFontWorkspace` | `"BaseFont"` | Workspace directory containing font.xml and referenced PNGs |
| `ttfDirectory` | `"TTF"` | Parent of the four role directories |
| `mapChinesePunctuation` | `false` | Map U+FF0C glyph to the ASCII comma glyph; does not rewrite source text |
| `mapLowercaseToUppercaseForLcdLoading` | `false` | Map a-z to A-Z in LCD and Loading only |
| `replaceEllipsis` | `false` | Explicitly convert a run of U+2026 to six ASCII periods in generated GXT |
| `fixedCjkWidth` | `0` | Optional Han glyph-cell width; 0 keeps variable width |
| `cjkAdvanceWidth` | `0` | Optional Han advance width; 0 uses glyph width |
| `workerThreads` | `0` | 0 selects from detected hardware and useful tasks, not a minimum of 16 |
| `maxAutoWorkerThreads` | `0` | Optional cap for automatic worker selection |
| `parallelGlyphRender` | `true` | Parallel per-role glyph batches |
| `parallelDxt3` | `true` | Parallel DXT3 block rows |
| `glyphTaskSize` | `64` | Positive task granularity |
| `dxtBlockRowTaskSize` | `16` | Positive DXT task granularity |
| `debug` | `false` | Write the collected codepoint list after successful generation |
| `debugDirectory` | `"JsonDevDebug"` | Relative directory for the optional codepoints.txt |

Directory settings must stay inside the language package. Relative configuration avoids machine-specific font paths. There is no automatic font download, filename guessing, or fallback font directory.

Role rendering sizes remain Console 12px, Manhunt/LCD/Loading 31px. The known-working Manhunt DAT height scale remains 28/512; LCD/Loading use 31/512 and Console 12/512. Existing BaseFont glyph metrics take precedence. These are game-specific metrics, not language selection rules.

## Common Edits and Release Procedure

- **Change translations/add characters:** edit `Json/*.json` in the selected column; restart. The same build collects the new characters and regenerates fonts.
- **Change font:** replace the one TTF in the relevant role directory, keep any filename, restart. To replace original ASCII too, disable BaseFont reuse.
- **Keep original ASCII/icons:** supply your own BaseFont workspace exported as `font.xml` plus referenced PNG atlases, enable `useBaseGlyphs`. A missing/invalid requested role is an error, not silent fallback.
- **Switch language:** change `JsonLanguage`, align filenames and lang.ini suffixes, and supply fonts covering that language. Use separate package folders for separate language outputs.
- **Force regeneration:** delete one generated asset or the manifest, then restart. Do not delete source JSON/TTF/BaseFont.
- **Report an error:** attach `JsonDev.log`. JSON syntax errors include the filename, line, column and nearby text; schema errors include the key/row; TTF count errors list candidates.

For a player pack, first validate the generated assets in the game. Ship standard `MHLL.asi`, the language's `lang.ini`, generated DAT/TXD and `Text/*.gxt`, plus a compatible ASI loader under its own terms. Omit `MHLLJsonDev.asi`, `Json/`, `TTF/`, BaseFont, debug files and manifest. Keep a separate development package with sources if collaborators need to edit it.

## Test Coverage

The standalone test program uses synthetic JSON/resources and a temporary local Windows font, never redistributed game fixtures. It checks multiple language columns, UTF-8/UTF-16LE GXT structure, KEY/INPRO preservation, empty values, bad JSON diagnostics, invalid keys, control tags, CJK wrapping, strict TTF counts, role order, DAT maps/UVs, five TXD textures and LCDM equality, exact visible-space advances, cache invalidation/rebuilds, unchanged inputs, failure isolation, and serial/parallel output equality.

Tests do not launch the game, validate a particular translation/font license, or prove every in-game screen correct. Local build/test results and remaining manual checks are recorded separately when the working tree is verified.
