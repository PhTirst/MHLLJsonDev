# Local Verification / 本地验证

Date: 2026-10-31 (Asia/Shanghai).

## Repository / 仓库

- Fork: https://github.com/PhTirst/MHLLJsonDev (parent: Fire-Head/MHLL).
- Local branch: `jsondev`; upstream baseline: `ae523f162480f4200f470ef2120bb27a88cfc620` (1.3).
- `git diff --exit-code upstream/main -- src/dllmain.cpp src/manhunt/game.cpp src/manhunt/game.h`: passed; upstream Unicode hook implementation is unchanged.
- Development branch: `jsondev`; `main` is retained as the upstream baseline. This record describes the local preparation checks before the initial source publication.
- The previous `reverse/sources/MHLL` worktree, including its uncommitted DynamicFont experiment, was not modified.

## Build / 构建

- Visual Studio 2022, MSVC 19.44, Windows SDK 10.0.26100.0, Win32.
- CMake Debug and Release builds: passed, both `MHLL.asi` and `MHLLJsonDev.asi`.
- Tests: Debug and Release each passed the `JsonDevTests` suite, 32,054 assertions per run. This number includes per-character map/UV checks; it does not mean 32,054 independent test cases.
- Release dependency inspection: standard MHLL uses SHLWAPI/KERNEL32 and does not link the JSON/font engine or GDI+. JsonDev additionally uses USER32 and delay-loads GDI+. No external Visual C++ runtime DLL is required (`/MT`).
- Existing upstream warnings remain in `dllmain.cpp`: legacy wide printf overloads (C4996) and a 16-bit patch constant conversion (C4309). These lines were not changed during migration.

## Tested Behavior / 已验证行为

- Explicit/inferred target-language column selection; en_US, fr_FR, ja_JP, ko_KR and Chinese text fixtures.
- Disabled wrapping preserves text; enabled wrapping covers Han/kana/Hangul and keeps control tags unchanged.
- GXT UTF-16LE payloads, sorted keys, empty strings, KEY/INPRO retention, duplicate/overlong key rejection, filename/line/column syntax diagnostics.
- TTF count validation (zero/multiple), invalid TTF contents, mixed-case extension and Unicode filenames.
- BaseFont priority, preservation of original advances, retention of base-only glyphs, rendering missing glyphs from TTF.
- Manhunt/Console/LCD/Loading DAT order; maps/UV bounds; five DXT3 TXD textures; exact LCD/LCDM equality.
- DAT UVs resolve to expected synthetic glyph pixels in the corresponding compressed texture, not only plausible coordinate ranges.
- Enabling wrapping gives U+0020 zero advance; U+2007 exactly matches the prior normal-space advance for every role, including BaseFont reuse.
- Disabling wrapping and parallel generation reproduce the normal serial font bytes exactly in synthetic fixtures.
- Content-based cache hit/invalidation, missing-output rebuild, untouched source JSON, no default debug intermediates.
- Bad inputs do not replace old assets. A locked DAT file forces a mid-commit failure; previously replaced GXT is rolled back and the prior font/TXD/manifest remain intact.
- Decimal-point JSON parsing is independent of the Windows numeric locale.

## Not Yet Verified / 未声称完成的验证

- No game was launched or modified in this task. In-game startup, save/load, failure/completion screens, all four font roles and a CJK wrapping scene still require a separate manual integration test with the supported game executable.
- Current remote build/test results are available in [GitHub Actions](https://github.com/PhTirst/MHLLJsonDev/actions/workflows/build-test.yml). The local results above are separate from those remote runs.
- No real translation quality, chosen TTF coverage, font redistribution rights or general upstream redistribution license is certified by these tests.

首次源码发布定位为开发预览版，不附预编译 ASI。本记录确认的是本地迁移、编译及合成资源回归，没有部署宿主机游戏或完成新版本的游戏内验证。远端构建结果以 GitHub Actions 为准；稳定二进制发布前仍需游戏实测及相应分发许可确认。
