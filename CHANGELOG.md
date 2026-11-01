# Changelog / 版本记录

## 0.1.0 Binary Supplement - 2026-11-01 / 预编译包补充

- Add a Windows x86 development-preview package built from the unchanged `v0.1.0` plugin sources. The existing tag is not moved.
- Include `MHLLJsonDev.asi`, a separate `standard-loader/MHLL.asi` alternative, a synthetic test executable, examples and bilingual instructions. No game/font assets or ASI loader are bundled.
- Add reproducible packaging, per-file hashes, x86 PE checks, extracted-test verification and a Release CI artifact.
- Preserve upstream attribution and the unresolved licensing notice. Automated regression is not new in-game certification.

补充 Windows x86 预编译 ASI，沿用原标签的插件源码，不改 Hook 或字体生成逻辑、不移动标签。普通版放在单独目录，避免双插件误加载。附打包脚本、校验清单、测试和中英文说明；仍保留开发预览及原作者许可边界提示。

## 0.1.0 - Development Preview / 开发预览

Initial source-only release, based on Fire-Head/MHLL 1.3 (`ae523f162480f4200f470ef2120bb27a88cfc620`).

- Preserve upstream Unicode hooks and add startup JSON/TTF resource generation in the separate `MHLLJsonDev.asi` target.
- Select any named JSON language column through `lang.ini`; keep unrelated language columns and source files untouched.
- Make zero-width CJK wrapping opt-in, coordinating generated GXT and font advances. Recommended for Chinese, Japanese and Korean packs.
- Require exactly one TTF in each of the console, manhunt, lcd and loading directories, with actionable errors.
- Reuse optional BaseFont glyphs before rendering missing characters; generate four DAT roles and five DXT3 textures, including LCDM.
- Compile intermediate resources in memory; add content-based cache checks and rollback on ordinary output replacement failures.
- Use CMake/MSVC x86, a static C++ runtime and minimal RenderWare ABI declarations instead of a proprietary SDK or Premake.
- Add English/Chinese documentation and Windows Debug/Release synthetic regression tests.

This preview contains no prebuilt ASI, game assets, real translation corpus, TTF fonts or ASI loader. Build instructions are in the README. Local automated tests do not certify in-game hook compatibility. See `docs/PROVENANCE.md` for upstream attribution and unresolved licensing boundaries.

首个版本仅发布源码，基于上游 1.3，保留其 Unicode 修复；增加多语言 JSON 列选择、可选零宽断行、四个目录唯一 TTF 检查、基础 glyph 复用、内存生成、缓存和失败回滚。提供中英文说明、CMake 构建及 Windows 自动测试。

不附预编译 ASI、游戏资源、真实译文、字体或加载器；这不是已经完成游戏内实测的稳定二进制版本。构建方法、上游归属及许可边界详见 README 与 `docs/PROVENANCE.md`。
