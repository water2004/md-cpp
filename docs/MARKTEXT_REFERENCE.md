# MarkText 编辑交互参考

## 目标

MarkText/Muya 仅作为编辑语义和架构参考，不复制其 Electron、DOM 或 JavaScript 实现。

本项目必须保持原生 C++23、WinUI 3、DirectWrite 和 Direct2D 架构。

## 核心原则

`EditorDocument` 及其 Markdown AST 是文档解析完成后唯一可信的编辑状态。

```text
Markdown -> Parser -> EditorDocument
EditorDocument -> 编辑命令 -> EditorDocument
EditorDocument -> Renderer -> 界面
EditorDocument -> Serializer -> Markdown
```

Markdown 文本、RenderModel、LayoutTree 和屏幕坐标均为派生数据。文档不保存全文 SourceMap；渲染与命中测试使用 `TextPosition`/`TextSpan`。

常规编辑交互不得依赖扫描 Markdown 行前缀、统计缩进或临时拼接 Markdown 字符串。Markdown 语法识别属于 Parser，Markdown 文本生成属于 Serializer，键盘交互属于 AST 结构变换。

## 参考范围

克隆最新版 `marktext/marktext` 到本地未跟踪目录，并记录参考 commit。重点研究 Muya 中以下行为：

当前参考 commit：`43bd8b77795fb27b1a9512737c000f7362031ea0`（2026-07-06）。

* Enter、Backspace 和 Delete；
* 段落拆分与合并；
* 列表项创建、退出、缩进和反缩进；
* 引用拆分、退出和嵌套；
* 空段落及空容器处理；
* 跨块选区删除；
* Selection 映射；
* Transaction、Undo 和 Redo；
* Markdown 解析与序列化边界。

理解其背后的结构不变量，不要逐行翻译代码。

## 实现要求

编辑位置应基于稳定 `NodeId` 和节点内偏移。

编辑命令应表达为节点的拆分、合并、插入、删除、移动、提升和包裹等结构操作。

操作完成后必须：

* 规范化文档树；
* 验证 AST 不变量；
* 修复并恢复 Selection；
* 更新派生渲染和布局状态。

旧的源码驱动编辑逻辑可以在迁移期间暂时保留，但不得继续扩展；对应 AST 编辑路径完成并测试后应删除旧实现。

## 优先级

优先完成：

1. Enter；
2. Backspace 和 Delete；
3. Tab 和 Shift+Tab；
4. 嵌套列表与嵌套引用；
5. 空列表项和空引用退出；
6. 跨容器范围删除；
7. Undo/Redo 和 Selection 恢复。

## 测试要求

每项交互同时验证：

* AST 结构；
* Selection 位置；
* 文档不变量；
* 序列化 Markdown；
* Undo/Redo；
* parse–serialize–parse 的语义一致性。

必须覆盖列表和引用中唯一、首个、中间、末尾以及多层嵌套情况。

## 工作方式

每次修改前：

1. 找到 MarkText/Muya 中对应交互；
2. 总结其结构语义和不变量；
3. 检查本项目当前实现；
4. 编写适合本项目架构的实现；
5. 增加结构化回归测试。

不要为了通过单个示例继续增加源码字符串特殊分支。

## 已确认的 Muya 结构语义

Muya 将段落、列表、列表项和引用表示为可直接插入、移除、拆分及移动的树节点。Selection 绑定内容节点与节点内偏移；Enter 先按容器关系选择结构操作，再恢复到目标内容节点，而不是先生成 Markdown 前缀。

已确认的 Enter 不变量：

* 普通段落保留光标前内容，并把光标后内容放入新的相邻段落；
* 非空列表项拆分为相邻列表项；
* 空列表项按其在列表中的位置退出或拆分列表；
* 空引用段落按其在引用中的位置退出或拆分引用；
* 操作后容器不得留下没有内容子节点的列表项；
* Selection 恢复到新目标节点的节点内偏移，而不是重新猜测序列化字符位置。

已确认的 Backspace/Delete 不变量：

* 段落内部删除只修改对应的 inline 子树并保留未受影响的节点身份；
* 段首 Backspace 合并同一容器中的前一段，或把首段从引用、列表项中提升一层；
* 多层引用每次只解除一层容器；
* 首列表项提升后其余列表项必须保留，中间列表项则连同全部子块并入前一项；
* 段尾 Delete 合并下一段；跨列表项合并时必须移动下一项的完整子树，不能遗留嵌套列表；
* 合并后的 Selection 位于连接点，Undo/Redo 恢复原树和原 Selection。

已确认的 Tab/Shift+Tab 不变量：

* Tab 只缩进非首列表项，并把整个列表项子树移动到前一项的嵌套列表；
* 前一项已有兼容的嵌套列表时追加到该列表，否则创建新的嵌套列表节点；
* Shift+Tab 每次只提升一层，后续同级嵌套项仍作为被提升项的子列表保留；
* 列表层级变化不得重建当前段落节点，Selection 的节点身份和节点内偏移保持不变；
* 顶层列表项和首列表项不能缩进或继续提升时，结构命令必须无操作。

跨节点范围删除不变量：

* 范围顺序由 AST 中的文档顺序决定，而不是由序列化 Markdown 的全局字符偏移决定；
* 保留首段 Selection 起点之前的内容和末段终点之后的内容，并在首段节点中连接两部分；
* 范围内部的完整块、原子块、列表项和容器子树应被删除，删除后空容器应被剪除；
* Selection 收拢到首段的连接点，首段节点身份保持稳定；
* Undo/Redo 必须恢复原容器树、原节点身份和原跨节点 Selection。

输入与投影不变量：

* 普通输入和选区替换创建块内 `TextEdit`，修改目标 `InlineDocument.source` 后只重解析该内容节点的 lossless CST；
* 加粗、斜体、删除线、行内代码和行内公式通过块内源码编辑插入、删除或替换原有定界符，未触及的 marker 拼写保持不变；
* 自动配对定界符也是块内源码编辑；其 CST 结构由当前内容节点的局部重解析识别，不直接改写语义 inline 节点；
* 表格单元格输入、删除和跨单元格范围删除必须保留表格结构及未受影响的单元格节点身份；
* serializer 遍历权威块树生成 Markdown；inline 内容始终直接写出节点的 `InlineDocument.source`，不生成或恢复全文 SourceMap；
* 未完成的定界符仍属于当前文档树中的可编辑内容，不得因派生投影而改变块类型；
* 输入后的 Undo/Redo 恢复原文档树、稳定节点身份和原 Selection。

块级格式不变量：

* 标题级别直接改变 Paragraph/Heading 节点种类并保留节点身份；
* 引用和列表命令包装、解包或转换现有块子树，不得扫描或改写 Markdown 行前缀；
* 列表种类转换保留列表项内容子树，任务复选框只修改对应 TaskListItem 状态；
* 块级格式化后的 Selection 和 Undo/Redo 继续绑定原内容节点。
* 链接与行内图片命令生成块内源码编辑并局部重解析 CST；代码块、数学块、目录和表格命令直接插入对应 block 节点，并由 serializer 生成 Markdown。
* 表格导航绑定 TableCell 身份；行列插入、删除、移动和对齐直接变换 Table、TableRow 与 TableCell，空单元格不得用空格文本占位。
* Callout 包装和解包现有块子树；脚注命令从 AST 中分配标签并同时创建 FootnoteRef 与 FootnoteDefinition，不得扫描序列化文本。
