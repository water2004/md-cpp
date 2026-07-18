# Inline CST 架构落地记录（已完成）

状态：已于 2026-07-13 完成。本文保留为历史实施与验证记录，不代表仍在进行迁移。

实施基线：`ecadd1b docs: rewrite AGENTS.md`（基于 `bf94bf6`）

已落地架构：块级结构树 + 块内 source-backed lossless editable inline CST + 块内统一源码坐标 + TextEdit 编辑系统。旧模型和兼容路径已经删除；这些要求现由 `AGENTS.md` 作为长期代码规范维护。

## 长期架构不变量
1. 每个可编辑内容节点拥有唯一 inline source (InlineDocument.source)
2. 所有块内位置只使用该 source 的统一 offset (container_id + source_offset + affinity)
3. CST 逐字符覆盖 source (flatten(parse(s))==s 逐字符相等)
4. 所有 inline 修改通过 TextEdit 完成
5. 普通编辑只重解析受影响内容节点
6. inline 保存逐字符无损 (serialize = source)
7. 文档只有一套 Selection (TextSelection)
8. 编辑器只有一套事实来源
9. 不存在新旧模型双轨
10. 旧模型和兼容层不得重新引入

## 已完成任务
1. 设计并落地新核心类型模块 — inline_cst.ixx / inline_document.ixx / text_edit.ixx；改 ast.ixx
2. 实现 lossless inline CST parser
3. 重写 serializer 为 source-verbatim 无损
4. TextEdit 编辑系统 + selection/坐标统一
5. 删除热路径全文投影 + legacy selection
6. 统一块级文档树 + 结构编辑命令
7. 可逆增量事务 history
8. 渲染 + 命中测试改用新模型
9. 删除所有旧架构与兼容层
10. 重写测试套件到新模型 + property tests
11. WinUI 应用层迁移到新模型，并通过 Debug x64 MSBuild 验证

全部阶段已按依赖完成。`document_edit.ixx` 现为薄 facade，具体职责拆分到 `document_edit_support.ixx`、`document_source_edit.ixx` 与 `document_structure_edit.ixx`。

## 环境约束
- 核心构建+测试: `cmd /c build_test.bat 2>&1` (vcvars64 + ninja + FoliaTests)
- WinUI 应用层: `setup.ps1` 恢复依赖后，以 MSBuild Debug x64 构建验证
- 提交纪律: 小批量分步 commit，不一次性 dump

## 完成记录

- 2026-07-12 基线提交 ecadd1b (AGENTS.md 新纲领)
- 2026-07-12 测试框架迁移到仓库 fork 的 Boost.UT C++23 module (`third_party/ut`):
  - CMake: `cmake_minimum_required(VERSION 4.0)`、`file(GLOB_RECURSE)` 递归收集 `tests/*.cpp`，`FoliaTests` 链接 `Boost::ut_module`
  - `tests/folia_test.hpp` 使用 `import boost.ut;`；测试 TU 不再 `import std;`
  - `tests/main.cpp` 将 `argc`/`argv` 传给 `boost::ut::cfg<>.run()`；测试名过滤使用位置参数（例如 `FoliaTests.exe "*name*"`），`-t` 用于列出标签
  - fork 包含 MSVC module linkage 与命令行初始化修复；不得退回 `<boost/ut.hpp>` header-only 路径
  - 删除旧的 `tests/test_framework.h`
  - 验证: 全部核心测试套件在 module 路径下编译并通过
- 2026-07-13 完成统一块树、局部 source/CST、单一 TextPosition、可逆操作 history、局部渲染与 WinUI/TSF UTF-16 边界迁移。
- 删除 `SourceMap`、`source_structure`、`document_position`、`CharOffset`/`CharRange`、legacy selection/caret、全文增量重解析与旧 WinUI 全文映射路径。
- 验证: 核心随机/无损/编辑/history/渲染测试通过；WinUI Debug x64 构建通过。
- 2026-07-13 history 改为只保存可逆 `TextEdit`/树操作，不再保存 before/after 双快照；普通 inline 热路径在修改发生时直接记录精确操作。
- 2026-07-13 HardBreak 作为单个可视编辑单元删除，`<br>` 与 Markdown hard break 不再被 Backspace/Delete 拆成残缺源码。
