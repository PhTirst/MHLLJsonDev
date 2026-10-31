# Provenance and Distribution Boundaries

## Upstream

- This repository is a GitHub fork of [Fire-Head/MHLL](https://github.com/Fire-Head/MHLL), by Fire-Head.
- Migration baseline: release 1.3, commit `ae523f162480f4200f470ef2120bb27a88cfc620`.
- `src/dllmain.cpp`, `src/manhunt/game.cpp`, `src/manhunt/game.h` and the upstream hook fixes are retained unchanged.
- The original README is preserved as [UPSTREAM.md](UPSTREAM.md).
- Build organization is now CMake; the bundled Premake executable and scripts have been removed.

## Local Additions

- JsonDev cache/font builder and startup integration are migrated from the author's previous local MHLL development tree.
- The experimental runtime DynamicFont renderer is NOT part of this project.
- The JSON/Unicode parser originated in the author's `manhunt2-gxt-json` project. It is shared infrastructure only; the Manhunt 2 GXT codec is not used here.
- `third_party/rwsdk36_minimal/include/rwcore.h` contains minimal ABI declarations for calls into the game's RenderWare implementation. It is not the proprietary RenderWare SDK and contains no RenderWare implementation or libraries.

## Licensing Status

The upstream snapshot has no explicit repository license. Forking it on GitHub does not establish an MIT or other redistribution license. No blanket license is asserted for this fork. Before publishing modified binaries or using the code outside the permissions already available, obtain clarification/permission from the upstream author. Preserve authorship and upstream history.

This source tree does not bundle game executables, GXT/DAT/TXD assets, translated game text or TTF fonts. Examples use short synthetic text; tests create synthetic resources and temporarily copy a local Windows font. Font files are never embedded in binaries or CI artifacts. Users must supply fonts and game assets they are entitled to use and separately check each font's redistribution terms.

## 来源及分发边界

本项目 Fork 自 Fire-Head 的 MHLL，以 1.3 的上述提交为迁移基线，保留其 Unicode Hook 修复及作者归属。旧本地仓库中的 DynamicFont 实验未迁入。JSON 解析器复用自作者自己的二代文本工具，但 GXT 编码器是独立的一代实现。最小 RenderWare 声明头不是商业 RWSDK。

目前上游没有明确的仓库许可证，不能据此给整个 Fork 标注 MIT，也不能把 GitHub 可 Fork 等同于任意再分发许可。正式发布修改版二进制之前应向原作者确认许可。本仓库不附带游戏资源、真实译文或商业字体；使用者自行提供有权使用的资源，并确认字体是否允许随补丁分发。
