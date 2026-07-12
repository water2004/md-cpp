# Inline CST 迁移工作计划

基线: `ecadd1b docs: rewrite AGENTS.md` (基于 `bf94bf6`)
目标: 块级结构树 + 块内 source-backed lossless editable inline CST + 块内统一源码坐标 + TextEdit 编辑系统。旧模型彻底删除，无双轨。

## 不可变不变量（任务完成的定义）
1. 每个可编辑内容节点拥有唯一 inline source (InlineDocument.source)
2. 所有块内位置只使用该 source 的统一 offset (container_id + source_offset + affinity)
3. CST 逐字符覆盖 source (flatten(parse(s))==s 逐字符相等)
4. 所有 inline 修改通过 TextEdit 完成
5. 普通编辑只重解析受影响内容节点
6. inline 保存逐字符无损 (serialize = source)
7. 文档只有一套 Selection (TextSelection)
8. 编辑器只有一套事实来源
9. 不存在新旧模型双轨
10. 任务结束时旧模型和兼容层已彻底删除

## 任务编号 (见 TaskList)
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
11. WinUI 应用层迁移到新模型 (无法在此环境构建验证)

## 执行顺序
1 → 2 → 3 (并行可行: parser 与 serializer 都依赖 CST 类型) → 4 → 6 → 5 → 7 → 8 → 9 → 10 → 11
实际推进时按依赖串行，每完成一阶段小批量 commit。

## 环境约束
- 核心构建+测试: `cmd /c build_test.bat 2>&1` (vcvars64 + ninja + elmd_tests)
- WinUI 应用层: 无法在此环境构建 (需 MSBuild+WinUI SDK)；只保证签名/逻辑同步
- 提交纪律: 小批量分步 commit，不一次性 dump

## 进度日志
(每完成一个任务在此追加一行: 日期 - 任务# - commit hash - 状态)

- 2026-07-12 基线提交 ecadd1b (AGENTS.md 新纲领)
- 2026-07-12 测试框架迁移到仓库 fork 的 Boost.UT C++23 module (`third_party/ut`):
  - CMake: `cmake_minimum_required(VERSION 4.0)`、`file(GLOB)` 收集 `tests/*.cpp`，`elmd_tests` 链接 `Boost::ut_module`
  - 测试 TU 顺序为 `import std;`、`import boost.ut;`、再 import 所需的 `elmd.core.*` modules
  - `tests/runner.cpp` 将 `argc`/`argv` 传给 `boost::ut::cfg<>.run()`，支持 Boost.UT 过滤参数
  - fork 包含 MSVC module linkage 与命令行初始化修复；不得退回 `<boost/ut.hpp>` header-only 路径
  - 删除旧的 `tests/test_framework.h`
  - 验证: 全部核心测试套件在 module 路径下编译并通过
