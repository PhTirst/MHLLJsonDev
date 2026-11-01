# Windows ASI Package / Windows ASI 工具包

The `v0.1.0` release now provides `MHLLJsonDev-0.1.0-windows-x86.zip` and its SHA-256 sidecar. The ASIs are built from the existing `v0.1.0` plugin sources; the source tag has not been moved. The package manifest records both the plugin source commit and the packaging checkout. This remains a development preview, not a newly game-certified stable release.

## Install for Development

1. Extract the complete ZIP into a tools directory. Do not copy every included file into the game.
2. Install a compatible ASI loader separately. Copy only the root `MHLLJsonDev.asi` beside the supported Manhunt 1 executable. Remove or move any other MHLL plugin out of the game first. Do not load both ASIs together.
3. Supply your own complete language package under `MHLL/<package>/`: `lang.ini`, multilingual `Json/*.json`, `JsonDevConfig.json`, and exactly one TTF in each of `TTF/console`, `TTF/manhunt`, `TTF/lcd`, `TTF/loading`.
4. Choose the target column in `lang.ini`. Use `useBaseGlyphs: false` if you have no BaseFont workspace; enable it only after supplying your own exported BaseFont files. CJK packs may enable `zeroWidthWrap` using the documented settings.
5. Start the game, select the language, and verify the generated Text GXT, DAT and TXD assets. After editing JSON or TTF, restart. Check `JsonDev.log` if generation fails.

The `examples/` folder contains synthetic layout/schema examples, not a playable translation pack. It intentionally contains no fonts. Follow the full [English](../README.md) or [Chinese](../README.zh-CN.md) instructions. The plugin is x86 because Manhunt is 32-bit, including on 64-bit Windows. Its C++ runtime is statically linked; no separate Visual C++ runtime installation is required.

## Standard Loader Alternative

`standard-loader/MHLL.asi` is the ordinary loader, kept in a separate directory to avoid loading both plugins accidentally. It does not generate resources from JSON or TTF. For a player pack, replace the development ASI with this one and ship the validated generated GXT/DAT/TXD plus `lang.ini`. Do not rename MHLLJsonDev to MHLL.

## Verify

Compare the ZIP hash with the `.sha256` file:

```powershell
Get-FileHash .\MHLLJsonDev-0.1.0-windows-x86.zip -Algorithm SHA256
```

After extraction, optionally run the packaged verifier against the original ZIP:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\verify-package.ps1 -Archive ..\MHLLJsonDev-0.1.0-windows-x86.zip
```

It checks the manifest, every file hash, x86 PE headers, forbidden payloads and the extracted `diagnostics/JsonDevTests.exe`. The test uses synthetic resources and a temporary local Windows font. It never launches or modifies the game, loads either ASI, or includes a font in the archive. It leaves test logs in the reported temporary directory. Hash checks detect corruption, not publisher identity; this is not a signed package.

## 中文使用说明

`v0.1.0` 现已补充预编译 Windows x86 包及 SHA-256。ASI 使用原 `v0.1.0` 标签的插件源码构建，没有移动标签；清单分别记录插件源码和打包脚本的提交。它仍是开发预览版，不代表新增游戏内实测结论。

1. 完整解压到工具目录，不要把整个压缩包直接覆盖游戏。
2. 单独准备兼容的 ASI Loader，只把根目录的 `MHLLJsonDev.asi` 放到支持的 Manhunt 1 EXE 旁。先移走游戏中的其他 MHLL 插件，两份 ASI 不能同时加载。
3. 在 `MHLL/<语言包>/` 提供自己的完整 JSON 译文、`lang.ini`、配置及四个字体角色目录，每个目录必须且只能有一个 TTF。
4. 没有 BaseFont 工作区时设置 `useBaseGlyphs: false`；启用原 glyph 复用时必须自行提供工作区。中日韩语言可按主文档启用 `zeroWidthWrap`。
5. 启动游戏选择语言，检查生成的 GXT、DAT、TXD。修改 JSON 或 TTF 后重启；有错误查看 `JsonDev.log`。

示例只是格式和目录演示，不是完整汉化包，也没有字体。`standard-loader/MHLL.asi` 是普通版替代品，只读取已经生成的资产；给玩家发布时用它替换开发版，绝不能把开发版改名伪装成普通版。

上面的校验命令不会启动或修改游戏。合成资源测试不能代替主菜单、存读档、失败/完成界面及断行等游戏内验证。包内不含 ASI Loader、商业字体、游戏原始资源或真实译文。上游作者归属及尚未明确的许可证情况仍见 [PROVENANCE.md](PROVENANCE.md)，本次二进制打包不改变这些边界。

## Reproduce from Source

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DBUILD_TESTING=ON
cmake --build build --config Debug --parallel 2
ctest --test-dir build -C Debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package.ps1 -SourceTag v0.1.0 -Jobs 2
```

The packaging script builds/tests Release, checks that plugin/test sources match the requested tag when supplied (otherwise HEAD), writes a file manifest and ZIP hash, and verifies the packaged regression executable. Negative tests reject damaged hashes, unsafe paths, duplicate files, font payloads, unlisted/missing files and non-x86 binaries. Output: `build-package/release/`. Existing archives are not overwritten automatically. GitHub Actions also runs this packaging check for Release but does not upload to GitHub Releases automatically.
