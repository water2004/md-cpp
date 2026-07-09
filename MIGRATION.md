# el-md C++ 迁移文档（MIGRATION.md）

> 本文件把 `D:\el-md\project.txt`（原始规范）与 `D:\el-md\HANDOFF.md`（Rust→C++ 迁移说明）
> 的关键需求、验收、当前 C++ 迁移状态对照整理到一处，供后续开发直接引用。
> Rust 原项目位于 `D:\el-md`，C++ 移植位于 `D:\md-cpp`。

## 1. 项目目标（不变）

- 自绘 native WYSIWYG Markdown 编辑器（Windows）。
- **WinUI 3 壳**：窗口 / 菜单 / CommandBar / 侧边栏 / 状态栏 / 文件对话框。
- **自绘 EditorView**：`SwapChainPanel` + Direct2D/DirectWrite，自管文本模型/布局/选择/光标/命中测试/IME/undo/redo。
- **平台隔离**：core 不得依赖 windows crate / WinRT / DirectWrite；只有 platform 层封装 Win32/D2D/DWrite/DXGI/TSF/Clipboard。
- **Markdown 源文本是唯一可信数据源**；AST/Outline/RenderModel/LayoutTree 都是派生数据。

## 2. 绝对禁止

WebView2 / 浏览器引擎 / DOM / CSS 布局 / JS runtime / MathJax-KaTeX HTML /
RichEditBox / TextBox / RichTextBlock 作为编辑区 / 把 Markdown 转 HTML 显示 /
HtmlBlock / HtmlInline / HtmlFallbackBlock / HTML parser-sanitizer-renderer /
script 执行 / iframe / 把 `<img>` 当图片 / 把 `<table>` 当表格 /
`<span><div><kbd><mark>` 当 HTML 语义处理 / UI 层 ad-hoc 字符串 hack 命令。

## 3. 项目结构（C++ 对应）

| Rust crate | C++ 位置 | 状态 |
|---|---|---|
| shared | `src/core/{error,ids,theme,settings}.ixx` | ✅ 已迁移 |
| editor-buffer | `src/core/{buffer,utf,types}.ixx` | ✅ delta/grapheme/line_index 内联在 buffer/utf |
| markdown-model | `src/core/{ast,dialect,source_map,symbols,metadata,document}.ixx` | ✅ 已迁移 |
| markdown-parse | `src/core/parser.ixx` | ⚠️ 扩展系统已加注册表骨架（`extension.ixx`）；parser 内仍硬编码；list 仅单 item |
| editor-core | `src/core/{editor,command,transaction,undo,selection,input,scheduler,caret}.ixx` | ✅ 已迁移（命令 nullopt 项与 Rust 保持一致，editor 层处理） |
| editor-outline | `src/core/{outline,slug}.ixx` | ✅ 已迁移 |
| math-render | `src/core/math_renderer.ixx` | ✅ fallback + cache |
| editor-render | `src/core/{render_model,render_builder}.ixx` | ✅ 已迁移 |
| editor-layout | `src/core/{layout_tree,block_layout,text_measurer,hit_test,selection_geometry}.ixx` | ⚠️ caret_geometry 内联；line_break/cache 占位；virtual_scroll 缺 |
| storage | `src/core/storage.ixx` | ✅ 新增（file_io+assets） |
| export | `src/core/exporter.ixx` | ✅ 新增（markdown+html+plain_text） |
| platform-windows | `src/platform/*.ixx` + `paint.{h,cpp}` | ⚠️ DWrite factory/measurer/D2D factory/theme/native surface 接口已实现；D2D painter 因 MSVC module ICE 暂为 stub |
| app-winui | `src/app-winui/*.cpp + *.xaml` | ⚠️ WinUI 3 壳可启动；`SwapChainPanel` 已接 D3D/DXGI + Direct2D/DirectWrite 文本预览；Open/Save/Save As 文件对话框已接；未接 core parse/render/layout |

### 缺失/待补的 core 模块

- ~~`scheduler.ixx`~~：✅ 已加。
- ~~`caret.ixx`~~：✅ 已加（renderer 用）。
- ~~扩展注册表~~：✅ 已加 `extension.ixx` 骨架。
- `virtual_scroll.ixx`、`line_break.ixx`、`cache.ixx`：layout 增量/虚拟滚动接口（Rust 也是占位）。
- `commonmark-markdown-export` 测试、export HTML 安全测试：✅ export 7 个已加。

## 4. 关键数据流

```
键盘/鼠标/IME → platform input → EditorInputEvent → Command → Transaction
→ TextBuffer(revision++) → parse → MarkdownDocument+SourceMap+Diagnostics
→ build_render_model → RenderModel → layout_blocks → LayoutTree
→ Direct2D/DirectWrite 自绘
```

每层都带 revision；旧 revision 结果丢弃；undo/redo 也要 revision++（绝不重置）。

## 5. 位置类型（严格区分）

`ByteOffset` / `CharOffset` / `Utf16Offset` / `GraphemeOffset` / `LineCol` /
`TextRange<T>`。Editor-core 用 CharOffset；Windows IME/clipboard/accessibility 需
Utf16Offset；用户光标移动用 grapheme。转换必须走 buffer 的 mapper。

## 6. MarkdownDialect 默认配置

CommonMark + GFM(tables/task_lists/strikethrough/autolinks) + math(全开, fallback)
+ toc(bracket+wiki+slugs) + frontmatter(yaml/toml/json) + footnotes + definition_lists
+ callouts + wiki_links + tables(gfm_pipe+editable_grid) + images(local)
+ diagrams(mermaid/graphviz 关, fallback_to_code_block) +
raw_html = `DisabledTreatAsText`。

## 7. BlockNode / InlineNode（禁 HTML 节点）

BlockNode 15 种：Paragraph/Heading/BlockQuote/List/TaskList/CodeBlock/MathBlock/
Table/ImageBlock/Callout/FootnoteDefinition/Toc/Frontmatter/ThematicBreak/
UnsupportedMarkup/Extension。

InlineNode 14 种：Text/Emphasis/Strong/Strike/InlineCode/InlineMath/Link/Image/
FootnoteRef/WikiLink/SoftBreak/HardBreak/UnsupportedMarkup/Extension。

**raw HTML → UnsupportedMarkup**，绝不生成 HtmlBlock/HtmlInline。

## 8. 命令系统（Command）

已实现 to_transaction：InsertText/DeleteBackward/DeleteForward/DeleteSelection/
InsertNewline/Move*/ToggleStrong/Emphasis/Strikethrough/InlineCode/
SetHeading/InsertCodeBlock/InsertMathInline/Block/InsertToc/InsertTable/
InsertLink/InsertImage*/InsertFootnote/ToggleCallout/Extension/Undo/Redo/
Cut/Copy/Paste/SelectAll/ClearHeading/ToggleUnorderedList/ToggleOrderedList/
ToggleTaskList。

当前 C++ 未实现（返回 nullopt）：Undo/Redo/Cut/Copy/Paste/SelectAll/ClearHeading/
ToggleUnorderedList/OrderedList/TaskList/InsertImage/InsertFootnote/ToggleCallout/
Extension。（Undo/Redo 实际在 Editor 层处理，Command 层返回 nullopt 是设计。）

数学命令行为：空选区插 `$  $` 光标中间；选区包裹 `$sel$`；block 插 `$$\n\n$$\n`。
TOC：插 `[TOC]`。

## 9. RenderModel

`RenderModel{revision,blocks,outline,diagnostics}` + RenderBlock 11 种 +
InlineRenderItem(Text/Math/Image/Link/Marker) + BlockStyle/InlineStyle/
MarkerStyle/MarkerVisibility(Always/WhenCaretInsideNode/WhenBlockFocused/
HiddenButEditable)。

WYSIWYG：marker 不删，淡化或 caret 进入时显示；隐藏时 width=0 但 source_range 保留。

## 10. LayoutTree / Hit testing

`LayoutTree{revision,blocks,total_height,viewport}` + LayoutBlock +
LayoutItem(Line/Embedded/Table/BlockMath/Image) + TextLineLayout +
GlyphRunLayout(含 glyphs Vec<GlyphInfo>)。

**统一屏幕坐标系**：layout 把 viewport_origin 烤进所有 rect/origin；draw/hit-test
直接用，不各自加偏移。测量=绘制同一 IDWriteTextFormat。Hit test 用 glyphs advance，
返回文档 offset（`source_range.start + idx`），非局部 index。

## 11. IME / TSF（C++ 重点迁移）

走 Text Services Framework，不用 IMM。自绘 EditorView 实现 `ITextStoreACP`(~20 方法)
+ `ITfThreadMgr/DocumentMgr/Context` + `ITfContextOwner`/`ITextStoreACPSink`；
composition start/update/commit/cancel 转成 `TextInputEvent` 给 editor-core；
候选窗口通过 `GetTextExt` 跟随 caret。

core 侧只接收规范定义的 `TextInputEvent`，不碰 Windows API。

## 12. 后台调度

UI 不整篇 parse/layout；编辑后 debounce 30-80ms；worker 解析 snapshot；
ParseOutput/RenderModel/LayoutTree 都带 revision；UI 丢弃旧 revision；
viewport 优先 layout；接口支持 incremental + virtual scrolling + dirty rect repaint。

## 13. 禁止实现的类型/模块

`HtmlFallbackBlock/HtmlRenderBlock/HtmlInlineRenderItem/HtmlLayoutBlock/
HtmlSanitizer/HtmlParser/HtmlRenderer` 一律不得实现。

## 14. 现有 C++ 测试覆盖（99 个，全过）

`tests/test_buffer.cpp`(14)、`test_parser.cpp`(31)、`test_outline.cpp`(6)、
`test_math.cpp`(5)、`test_editor.cpp`(14)、`test_render_layout.cpp`(17)、
`test_export.cpp`(7)、`test_extension.cpp`(4)、`test_scheduler.cpp`(3)。 等。

测试框架：`tests/test_framework.h`（header-only 宏，不用 C++ module export 宏）。
每个测试 TU 顺序：`import std; #include "test_framework.h"; import elmd.core.*;`。

## 15. 待补测试（对标 Rust 105 + 规范要求）

- math：`\(...\)` / `\[...\]` 分隔符识别；code span 内 `$` 不识别。
- outline：修改 heading 后 outline revision 更新。
- frontmatter：TOML/JSON、只文档开头识别、重复 footnote label diagnostic。
- export：`<script>` escape、HTML img 不当图片、raw HTML export 不可注入。
- extension registry、markdown-model 独立测试。
- layout：heading/code block LayoutBlockKind、inline math fallback line、
  selection geometry source range、caret rect 跟随、DPI scale invalidation。

## 16. 平台/应用层迁移（未开始）

platform 层需实现：DWrite factory/text_format/text_layout/measurer/glyph_run、
D2D factory/render_target/brushes/geometry、DXGI device/factory/swap_chain、
composition surface、TSF ime_bridge、clipboard、dpi、cursor、theme、dispatcher。

app-winui 需实现（**必须用 WinUI 3 + C++/WinRT，禁止 Win32 WndProc 壳**）：
MainWindow、MenuBar/CommandBar、OutlinePanel(TreeView 原生)、DiagnosticsPanel、
AssetsPanel、SearchPanel、StatusBar、文件 open/save 对话框、最近文件、
SwapChainPanel 自绘 EditorView(windows-rs canvas 不可用，C++ 直接 D3D11/D2D/DWrite)、
输入桥接、Command→Editor.execute_command。

CMake：`-DELMD_BUILD_WINUI=ON` 才构建 app（需 Windows App SDK + C++/WinRT 头）。

## 17. 构建命令

```
cmake -B build -G Ninja
cmake --build build --target elmd_tests     # core + 85 tests
cmake --build build --target elmd_core       # 纯 core 静态库
cmake --build build --target el_md           # WinUI 3 app（需 ELMD_BUILD_WINUI）
```
便捷：`build_test.bat`（vcvars64 + 构建 tests）、`build_only.bat`。

## 18. 验收标准摘要（34 条，project.txt 行 2299）

核心：能在 Windows 编译；app-winui 启动 WinUI 3 窗口；编辑区是自绘 EditorView；
core 不依赖 windows；platform 封装所有 D2D/DWrite/WinUI/IME/Clipboard；
Markdown 源可打开/编辑/保存；键盘/鼠标/caret/selection/undo/redo 可用；
parser 输出 MarkdownDocument（含 MathBlock/InlineMath/Toc/Table/Frontmatter/
FootnoteDefinition/Callout）；RenderModel 表达 block/inline/embedded；
LayoutTree 表达 block/line/glyph/caret/selection geometry；OutlinePanel 原生 WinUI；
raw HTML 不渲染/不执行/不注入；HTML export escape；基本测试覆盖；
UI 与 editor-core 分离清楚。

## 19. 下一步迁移优先级

1. 完善 platform 层 DWrite/D2D 真实测量+绘制（占位 StubMeasurer 已够测试）。
2. core parse/render/layout 接入 Direct2D EditorView 绘制 pipeline。
3. WinUI 3 原生 OutlinePanel + 文件对话框。
4. TSF/IME composition（C++ 重点，Rust 卡住项）。
5. 补 core scheduler + extension registry 骨架。
6. 补 export/extension/markdown-model 测试。
7. command 层实现剩余 ToggleList/InsertImage/Footnote/Callout。
8. virtual scrolling / incremental layout 接口实现。

## 20. 踩坑清单（C++ 必须避免）

| 坑 | C++ 对策 |
|---|---|
| run 重叠 | 测量=绘制同一 IDWriteTextFormat |
| trailing whitespace 丢 | 用 advance 之和 |
| 单行 run 被切碎换行 | DrawTextLayout + NoWrap |
| 点击错位 | 统一屏幕坐标（viewport_origin 烤进 rect） |
| local char_index | 用 `source_range.start + idx` |
| 列表重复 | rest_of_line 只读，advance_n(len) 推进 pos |
| `*x*` 当 strong | strong 判定需 `**` |
| revision 重置 | undo/redo 用 apply_delta 永远++ |
| 中文 frontmatter panic | 统一 char 偏移，禁止 byte 切 char 索引 |
| module 名含 `export` 关键字 | 用 `elmd.core.exporter` 不用 `elmd.core.export` |
| C++ module ABI 跨单元前向声明 | 同一 module interface 单元内完成完整定义 |
| test 宏跨 module 不可见 | header-only `test_framework.h`，文本包含 |
| MSVC C1001 ICE on platform TU mixing `#include <d2d1.h>` + `import elmd.core.layout_tree;` + nested loops | `src/platform/paint.cpp` 暂为 stub；接口 `paint.h` 稳定；等 MSVC 修 IFC import 后恢复实现 |
| Windows SDK include path not in CMake | `CMakeLists.txt` 自动探测 `C:/Program Files (x86)/Windows Kits/10/Include/10.0.*` 并加到 `elmd_platform` |
