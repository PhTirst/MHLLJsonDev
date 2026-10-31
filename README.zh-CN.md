# MHLLJsonDev

基于 [Fire-Head/MHLL](https://github.com/Fire-Head/MHLL) 1.3 的 **Manhunt 1 开发期插件**：游戏启动时，根据 JSON 译文和 TTF 字体生成标准 GXT、DAT、DXT3 TXD，再交给 MHLL 加载。

[English](README.md) | [上游原始说明](docs/UPSTREAM.md) | [代码来源与分发边界](docs/PROVENANCE.md)

[![Windows x86 构建与测试](https://github.com/PhTirst/MHLLJsonDev/actions/workflows/build-test.yml/badge.svg?branch=jsondev)](https://github.com/PhTirst/MHLLJsonDev/actions/workflows/build-test.yml)

**开发预览版：**默认分支 `jsondev` 包含本项目功能，`main` 保留上游基线。首次发布提供源码，不附预编译 ASI。详见[版本记录](CHANGELOG.md)及[验证边界](docs/VERIFICATION.md)。

它不是改名伪装的普通 MHLL。开发时使用 `MHLLJsonDev.asi`；玩家版使用普通 `MHLL.asi` 加生成后的资产，不需要 JSON 或 TTF。**两个 ASI 不要同时加载。**

## 当前范围

- 从上游 `ae523f162480f4200f470ef2120bb27a88cfc620`（1.3）迁移，保留其存档、失败界面等 Unicode Hook 修复。
- 按配置选择任意语言列，不写死中文；零宽断行为可选策略。
- 严格检查四个字体角色目录；默认在内存中完成中间处理，保留缓存及自动回归测试。
- Windows x86、MSVC、CMake；附最小 RenderWare ABI 声明头，不需要商业 RWSDK。
- 不附游戏程序、真实译文、游戏字库、TTF 或 ASI Loader。
- 上游当前快照没有明确许可证；正式分发修改版二进制前应向原作者确认许可，详见来源说明。
- 自动测试不等于游戏实测。Hook 仍依赖上游支持的游戏可执行版本，不适用于任意 EXE。

## 构建和测试

安装 Visual Studio 2022 的“使用 C++ 的桌面开发”、Windows SDK，以及 CMake 3.20 或更新版本。在本仓库中运行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

| 产物 | 用途 |
| --- | --- |
| `build/bin/Release/MHLLJsonDev.asi` | 开发版，启动时从 JSON/TTF 生成资源 |
| `build/bin/Release/MHLL.asi` | 普通版，不链接 JSON 解析、TTF 渲染和资源生成模块 |
| `build/Release/JsonDevTests.exe` | 独立自动测试，不启动、不注入游戏 |

GitHub Actions 在 Windows x86 上分别构建并测试 Debug、Release，不会自动发布 Release，也不会上传游戏资源或字体。项目不再依赖 Premake。

## 开发包放在哪里

游戏根目录放开发版 ASI，通过兼容的 ASI Loader 加载。语言包放在 `MHLL/` 下：

```text
Manhunt/
  MHLLJsonDev.asi
  MHLL/
    MyLanguage/
      lang.ini
      JsonDevConfig.json
      Json/
        pc_game_zh_CN.json
        pc_jury_turf_zh_CN.json
      TTF/
        console/  <唯一一个 .ttf>
        manhunt/  <唯一一个 .ttf>
        lcd/      <唯一一个 .ttf>
        loading/  <唯一一个 .ttf>
      BaseFont/   <可选，自备的基础字体工作区>
        font.xml
        console.png
        manhunt.png
        lcd.png
        loading.png
      Text/                   <生成的 GXT>
      font_zh_CN.dat          <生成>
      font_zh_CN.txd          <生成>
      .jsondev_manifest.json  <生成的缓存索引>
      JsonDev.log             <出错时写入>
```

`MyLanguage` 可以自行命名，不靠它判断目标语言。**TTF 子目录名决定字体角色，TTF 文件名不决定角色，也不决定语言。**例如 `TTF/lcd/文泉驿点阵宋体.ttf` 会用于 LCD，可以保留字体原名。

每个角色目录只允许一个普通 `.ttf` 文件，扩展名大小写不敏感；不递归寻找子目录。零个、多个都明确报错，缓存命中时也会检查，不会偷偷选第一个。说明文本等非 TTF 文件不计数。四个角色都需要提供；同一个字体允许复制到不同角色目录。不能用 OTF/TTC 代替 TTF。

[examples/MHLL/Example](examples/MHLL/Example) 是目录及多语言 JSON 示例，内容是合成的演示文本，**不是完整汉化包，不能直接拿去正常游玩**。它默认选择法语，以展示没有写死 `zh_CN`。使用时补齐自己的字体和完整文本。

## 选择目标语言

`lang.ini` 保留普通 MHLL 的字段，开发版额外接受 `JsonLanguage`，明确指定读取 JSON 的哪一列：

```ini
[LANG]
NameKey = Chinese
TextureFile = font_zh_CN.txd
DataFile = font_zh_CN.dat
TextDirectory = Text\
GameGXTName = pc_game_zh_CN.gxt
LevelGXTPostfix = _zh_CN
JsonLanguage = zh_CN
UseWholeAsciiTable = 1
WindowsLangID =
```

选择顺序：非空的 `JsonLanguage`，否则取 `LevelGXTPostfix` 并去掉开头一个 `_`，否则从 `pc_game_<语言>.gxt` 推断后缀。语言列区分大小写，可以是 `zh_CN`、`ja_JP`、`ko_KR`、`fr_FR` 等，也可以是自己的列名。源语言列不必叫 `en_US`，列数也不受限制。

游戏加载资源仍使用 Windows ANSI 文件接口，因此可分发的游戏目录、语言目录和输出文件名建议使用 ASCII，避免跨系统代码页问题。生成核心支持 Unicode TTF 文件名，测试也覆盖 Unicode 路径。

JSON 只接受以下一种结构，编码 UTF-8，可带 UTF-8 BOM：

```json
[
  { "Key": "PLAY", "en_US": "Start", "zh_CN": "开始", "ja_JP": "開始" },
  { "Key": "SAMPLE", "en_US": "Sample", "zh_CN": "示例", "fr_FR": "Exemple" }
]
```

只读取 `Key` 和选中的语言列，其他语言、备注字段不参与生成。每一行都必须有这两个字符串字段，译文允许为空，不会缺失时偷偷回退英文。

`Key` 为 1–8 个非空格的可打印 ASCII 字节。重复 Key、超长 Key 会报错，不截断；名为 `KEY` 的正常条目不会被当成表头删掉。GXT 为适应游戏查找逻辑会对键排序，源 JSON 的行顺序和内容不改动。

`Json/<文件名>.json` 生成 `Text/<文件名>.gxt`，只替换扩展名，不自动添加语言后缀。文件名需与 INI 中的全局文件名及关卡后缀一致。改目标列不等于改文件名，请同步设置。没有任何 JSON 时不生成资源，直接走普通 MHLL 的 GXT/DAT/TXD 加载路径；只有部分文件有 JSON 时，其他已有 GXT 不动。启用生成时 `TextDirectory` 必须是本地目录，不支持 `%GAMEPATH%` 布局。

## 启动时每一步做什么

1. **读取并验证输入。**读取语言列及严格 JSON 配置，先检查四个 TTF 目录，再动生成物。配置写错类型或拼错字段会报错。
2. **检查缓存。**根据生成器版本、目标语言、配置、JSON、TTF、可选 BaseFont 的内容计算指纹。指纹匹配且所有预期产物都存在时跳过生成。删一个产物也会自动重建，不必把 manifest 一起删掉。
3. **生成文本。**在内存中解析全部 JSON，只取目标语言，按显式开启的策略处理文本，编译为一代的 UTF-16LE GXT 缓冲区，并统计实际显示字符。`~...~` 控制标记不作为字形内容统计。
4. **生成字形。**每个角色先读取自备 BaseFont（如启用），保留已有 glyph 及其间距；只把缺少的字符交给该角色 TTF 渲染。关闭 BaseFont 时全部从 TTF 渲染。另加入基本 ASCII，供未翻译文本、数字等动态值使用。
5. **打包字体。**在内存中排列 atlas，构造 DAT 和 DXT3 TXD。DAT 顺序固定为 Manhunt、Console、LCD、Loading；TXD 始终有 `manhunt`、`console`、`lcd`、`lcdm`、`loading` 五张图，LCDM 与 LCD 完全一致。
6. **统一替换最终产物。**所有解析和渲染成功后，再暂存最终文件缓冲并替换 GXT/DAT/TXD，最后写 manifest。验证、渲染失败时不替换旧产物；普通文件替换失败会尝试回滚。这不是能抵御突然断电的多文件数据库事务。
7. **交还普通加载逻辑。**正常加载生成后的 GXT、DAT、TXD，并继续使用上游 1.3 的 Unicode 修复。

默认不落盘单字 PNG、atlas 工作区等中间形式。提交产物时才短暂使用最终文件旁的临时文件。不要一边依赖缓存一边手工改生成物，应该修改输入并重建。

## 可选的零宽断行

通用默认关闭：

```json
{ "useBaseGlyphs": false, "zeroWidthWrap": false }
```

中文、日文、韩文推荐开启 `zeroWidthWrap`，避免游戏把连续文字当超长单词，自动插入连字符并替换掉某个文字。其他语言可以不启用。[CJK 配置示例](examples/JsonDevConfig.cjk.json) 开启此策略，同时要求提供自己的 BaseFont。

这里的“零宽空格”是游戏可识别的 **U+0020 的宽度改为 0**，不是插入 Unicode U+200B：

- 关闭时：文本不改写，U+0020 保持正常宽度。
- 开启时：在允许的 CJK 字符边界插入 U+0020 供游戏断行，四个字体角色中的 U+0020 同时改为零宽。
- 原文真实存在的 U+0020 转为 U+2007，后面不再附加 U+0020；U+2007 的 advance 精确保留该角色原 U+0020 的宽度，因此英文单词之间仍有可见间距。
- 覆盖汉字、假名、韩文音节，并有基本的前后标点禁则；不是完整的 Unicode 排版引擎。
- `~blue~`、`~colour~`，乃至原文写成 `~colur~` 的标记都原样保留，不自动纠正，也不默认裁掉标记两侧的真实空格。
- 开关变化后 GXT 与字体一起重建。必须保留未转义的源 JSON，不要把已转换的 GXT 文本再导回当源文件。关闭选项也不会猜测并反向清洗已经写进 JSON 的转义。

不处理阿拉伯文、印度文字等复杂塑形、双向排版；暂不支持游戏字体渲染器之外的非 BMP 字符。所选译文含 NUL 或非 BMP 字符会明确报错，不生成貌似成功的错误资源。

## 配置字段

配置文件为语言目录下的 `JsonDevConfig.json`。省略字段使用下表默认值；未知字段、错误类型会报错。

| 字段 | 默认值 | 含义 |
| --- | --- | --- |
| `zeroWidthWrap` | `false` | 同步处理 GXT 和字体空格的断行策略 |
| `useBaseGlyphs` | `true` | 要求加载自备 BaseFont，已有 glyph 优先 |
| `baseFontWorkspace` | `"BaseFont"` | 存放 font.xml 及其引用 PNG 的目录 |
| `ttfDirectory` | `"TTF"` | 四个角色目录的父目录 |
| `mapChinesePunctuation` | `false` | 将 U+FF0C 的 glyph 映射为英文逗号，不改源文本 |
| `mapLowercaseToUppercaseForLcdLoading` | `false` | 仅对 LCD、Loading 将 a-z 映射为 A-Z |
| `replaceEllipsis` | `false` | 显式将一串 U+2026 编译为六个英文句号 |
| `fixedCjkWidth` | `0` | 可选的汉字 glyph 格宽；0 保持不定宽 |
| `cjkAdvanceWidth` | `0` | 可选的汉字前进宽度；0 按 glyph 宽度 |
| `workerThreads` | `0` | 0 按硬件和有效任务数选择，不会起步就开 16 线程 |
| `maxAutoWorkerThreads` | `0` | 自动线程数的可选上限 |
| `parallelGlyphRender` | `true` | 并行渲染同一角色的 glyph 批次 |
| `parallelDxt3` | `true` | 并行压缩 DXT3 块行 |
| `glyphTaskSize` | `64` | 正整数，渲染任务粒度 |
| `dxtBlockRowTaskSize` | `16` | 正整数，压缩任务粒度 |
| `debug` | `false` | 成功生成后额外输出用字列表 |
| `debugDirectory` | `"JsonDevDebug"` | 调试 codepoints.txt 的相对目录 |

目录配置必须在语言包内部，不接受绝对路径或 `..`。没有自动下载字体、猜文件名、搜索后备字体目录的行为。

保持已验证的一代角色参数：Console 12px；Manhunt、LCD、Loading 31px。Manhunt 的 DAT heightScale 为 28/512，LCD、Loading 为 31/512，Console 为 12/512；不要把字形像素高度与游戏缩放参数混为一谈。复用 BaseFont 的现有 glyph 时，其原间距优先。

## 改什么文件

- **改译文或新增汉字：**改 `Json/*.json` 的目标语言列，重新启动。生成器会统计这次新出现的字并生成字体，不需要另一个手工字表。
- **换某个角色的字体：**替换 `TTF/<角色>/` 中唯一的 TTF，文件不必改名，重新启动。要把原版英文也换掉，关闭 BaseFont 复用。
- **保留原版英文、数字、图标：**提供自己导出的 BaseFont（`font.xml` 和引用的 PNG），开启 `useBaseGlyphs`。要求复用而某角色缺失或不可读时会报错，不静默切到另一套字体。
- **换语言：**改 `JsonLanguage`，同步 JSON 文件名和 INI 后缀，并提供覆盖该语言的字体。不同语言建议分别使用不同语言包目录。
- **强制重建：**删除一个生成物或 manifest 后重启，不删源 JSON、TTF、BaseFont。
- **定位错误：**查看 `JsonDev.log`。JSON 语法错误给出文件、行列和邻近文本；结构错误给出行或 Key；TTF 数量错误列出目录及候选文件。

## 发给玩家还是发给合作者

先自行进游戏验证最终资产。**玩家包**使用普通 `MHLL.asi`，附该语言的 `lang.ini`、生成的 DAT/TXD、`Text/*.gxt`，及按其条款分发的兼容 ASI Loader；不附 JsonDev ASI、JSON、TTF、BaseFont、调试文件、manifest。

**开发包**才保留 JsonDev、JSON、TTF、配置等源输入，让合作者修改后自行生成。是否随包分发字体，仍需确认字体授权。

## 自动测试验证什么

独立测试使用合成 JSON/资源和临时复制的 Windows 字体，不打包游戏样本。覆盖语言列选择、UTF-8/UTF-16LE GXT、KEY/INPRO、空译文、坏 JSON 提示、无效键、控制符、CJK 断行、严格 TTF 数量、DAT 角色顺序及码位表/UV、五张 TXD 和 LCDM 一致、可见空格精确宽度、缓存及开关失效、输入不改动、失败不污染旧产物、串行并行产物一致。

自动测试不会启动游戏，不核验具体译文和字体授权，也不能证明所有游戏界面都正确。本地整理结果与尚需人工验证的项目会单独记录。
