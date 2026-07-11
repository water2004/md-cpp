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

## 2. 编辑区约束

WebView2 / 浏览器引擎 / DOM / CSS 布局不得作为编辑区或参与文本布局。嵌入内容转换器
隔离、排队、只输出 SVG：单 QuickJS worker 运行 MathJax 4，单原生 Rust worker 运行
`mermaid-rs-renderer`。两路 SVG 进入 usvg 标准化管线，把文字、marker 和
不兼容节点规范为 path-only SVG，再交回 WinUI 3 的 `ID2D1SvgDocument` 原生矢量绘制；
转换器不接收键鼠输入，也不保存 Markdown 状态。

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
| markdown-parse | `src/core/parser.ixx` | ✅ CommonMark/GFM、连续列表/任务列表、表格、数学、脚注、callout、TOC、frontmatter |
| editor-core | `src/core/{editor,command,transaction,undo,selection,input,scheduler,caret}.ixx` | ✅ 已迁移（命令 nullopt 项与 Rust 保持一致，editor 层处理） |
| editor-outline | `src/core/{outline,slug}.ixx` | ✅ 已迁移 |
| math/diagram-render | `src/core/math_renderer.ixx` + `src/app-winui/{MathJaxRenderer,MermaidRenderer,SvgNormalizer}.*` | ✅ MathJax 4/QuickJS + mermaid-rs-renderer + usvg path-only 标准化 |
| editor-render | `src/core/{render_model,render_builder}.ixx` + `src/app-winui/EditorContentPreparation.*` | ✅ 语义模型与平台内联内容准备分层 |
| editor-layout | `src/core/layout_plan.ixx` + `src/app-winui/EditorInteractionMap.*` | ✅ 全块预分配、DirectWrite 测量/命中测试、视口虚拟化、高度缓存 |
| storage | `src/core/storage.ixx` | ✅ 新增（file_io+assets） |
| export | `src/core/exporter.ixx` | ✅ 新增（markdown+html+plain_text） |
| platform-windows | `src/platform/*.ixx` + `src/app-winui/{EditorRenderResources,EditorStyleSheet}.*` | ✅ D3D/DXGI/Direct2D/DirectWrite/WIC/TSF/clipboard/theme 已接入，core 仍保持纯 C++ |
| app-winui | `src/app-winui/*.cpp + *.xaml` | ✅ WinUI 3 壳、原生编辑面、独立滚动与 TSF/IME 控制器生命周期、文件操作、outline/diagnostics、MathJax、Mermaid、Tree-sitter |

### 缺失/待补的 core 模块

- ~~`scheduler.ixx`~~：✅ 已加。
- ~~`caret.ixx`~~：✅ 已加（renderer 用）。
- ~~扩展注册表~~：✅ 已加 `extension.ixx` 骨架。
- ~~虚拟滚动与布局缓存~~：✅ 集成在原生 renderer；离屏块只保留轻量占位和高度缓存。
- `commonmark-markdown-export` 测试、export HTML 安全测试：✅ export 7 个已加。

## 4. 关键数据流

```
键盘/鼠标/IME → platform input → EditorInputEvent → Command → Transaction
→ TextBuffer(revision++) → parse → MarkdownDocument+SourceMap+Diagnostics
→ build_render_model → RenderModel → DocumentLayoutPlan → block measure/paint → LayoutTree
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

Undo/Redo、剪贴板、全选由 Editor/App 层执行；标题、无序/有序/任务列表、图片、脚注、
callout、链接、行内样式、代码块、数学块、TOC 和表格命令均已贯通 semantic edit。
Extension 仍由扩展注册表接管，Command 层返回 nullopt 是设计。

数学命令行为：空选区插 `$$` 并把光标置于中间；选区包裹 `$sel$`；block 插
`$$\n\n$$\n`。编辑时原文保持唯一光标域，渲染预览不占用源文本位置。
TOC：插 `[TOC]`。

自动配对由 core 的语义事务处理，支持 `*`、`_`、`$`、反引号、`~~`，并在行首第三个
反引号输入时提升为 fenced code block；WinUI 输入层不直接改写 Markdown 源码。

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

打开文件后在后台线程构建文档；编辑走增量 parser。ParseOutput/RenderModel/LayoutTree
均带 revision，旧 revision 结果丢弃。纯 core 的 `DocumentLayoutPlan` 在绘制前为所有
block 分配位置，并分别给出测量窗口和嵌入资源请求窗口。原生 renderer 只为 viewport 及预取区建立
DirectWrite layout，离屏块复用高度缓存；可见块的实测高度进入下一帧计划，不在当前绘制过程中推动
后续块。Tree-sitter 只高亮可见代码块。MathJax 与
Mermaid 各自单队列去重生成 SVG，后者在同一个 Rust 调用内直接经 usvg 标准化；队列、字符串缓存和
Direct2D SVG 文档缓存都有硬预算，只有屏幕内 SVG 才创建原生文档。

## 13. 禁止实现的类型/模块

`HtmlFallbackBlock/HtmlRenderBlock/HtmlInlineRenderItem/HtmlLayoutBlock/
HtmlSanitizer/HtmlParser/HtmlRenderer` 一律不得实现。

## 14. 现有 C++ 测试覆盖（274 个，全过）

`tests/test_buffer.cpp`(13)、`test_parser.cpp`(53)、`test_outline.cpp`(6)、
`test_math.cpp`(5)、`test_editor.cpp`(64)、`test_render_layout.cpp`(25)、
`test_export.cpp`(7)、`test_extension.cpp`(4)、`test_scheduler.cpp`(3)。 等。

测试框架：`tests/test_framework.h`（header-only 宏，不用 C++ module export 宏）。
每个测试 TU 顺序：`import std; #include "test_framework.h"; import elmd.core.*;`。

## 15. 回归覆盖

193 项 core 测试覆盖增量编辑、Unicode/光标、CommonMark/GFM parser、数学分隔符、
列表/任务、表格结构编辑、outline、render/source map、导出安全和大文档增量修改。
另有 MathJax QuickJS 500 公式压力用例，以及 Tree-sitter 十种语言的原生解析 smoke。

## 16. 平台/应用层迁移（已贯通）

应用使用 WinUI 3 + C++/WinRT 壳和 `SwapChainPanel` 自绘编辑面，D3D11/DXGI、
Direct2D/DirectWrite、WIC 图片、TSF/IME、clipboard、DPI、光标、主题、outline、
diagnostics、状态栏和文件对话框均已接入。MathJax/Mermaid 生成的 SVG 统一经 usvg
转换为 Direct2D 可消费的 path-only SVG，并由有界 LRU 原生矢量缓存绘制；代码块由
Tree-sitter 0.26.9 在视口内高亮 C/C++、JavaScript、TypeScript、Python、Rust、Go、
Java、JSON、HTML 和 CSS。

CMake：`-DELMD_BUILD_WINUI=ON` 才构建 app（需 Windows App SDK + C++/WinRT 头）。

## 17. 构建命令

```
cmake -B build -G Ninja
cmake --build build --target elmd_tests     # core + 180 tests
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

## 19. 后续维护重点

1. 用真实超大文档持续采样 frame time、峰值内存和缓存命中率。
2. 为 WinUI/DWrite 命中测试、TSF composition、表格悬浮控件补自动化 UI 回归。
3. 在不改变“Markdown 源是唯一可信数据源”的前提下扩展插件命令。

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
