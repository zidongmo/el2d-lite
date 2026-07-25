# EL2D Lite 双语文档实施计划 / Bilingual Documentation Implementation Plan

[中文](#中文) | [English](#english)

## 中文

> **供自动化执行者：** 必需子技能：使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 逐项实施本计划。步骤使用 checkbox（`- [ ]`）语法跟踪。

**目标：** 将全部自有 Markdown 文档改为同一文件内中文和英文完整镜像的 GitHub 友好格式。

**结构：** 每份文档保留一个规范链接，顶部放置语言锚点，先给出完整中文，再给出完整英文。第三方文档不改动，命令、路径、指标、API 和许可结论在两个语言版本中保持一致。

**技术栈：** GitHub Flavored Markdown、仓库现有 Python 公开树审计、Git。

### 任务 1：入口与贡献文档

**文件：**
- 修改：`README.md`
- 修改：`CONTRIBUTING.md`

- [ ] 为每份文件加入语言导航、中文章节锚点和完整英文镜像。
- [ ] 核对命令、性能表格、仓库链接及许可声明在两种语言中一致。

### 任务 2：组件与后端文档

**文件：**
- 修改：`converter/README.md`
- 修改：`components/el2d/README.md`
- 修改：`core/official_backend/README.md`
- 修改：`core/purism_backend/README.md`

- [ ] 为仅中文文档补英文，为仅英文文档补中文。
- [ ] 统一单文件双语顺序和标题层级。

### 任务 3：核心技术文档

**文件：**
- 修改：`docs/architecture.md`
- 修改：`docs/asset-pipeline.md`
- 修改：`docs/licensing-and-assets.md`

- [ ] 翻译全部正文、表格标题和图示说明。
- [ ] 保持代码块、公式、命令参数、外部链接和许可边界不变。

### 任务 4：验证与发布

- [ ] 运行 `python tools/audit_public_tree.py .`，预期退出码为 `0`。
- [ ] 运行 `python -m pytest -q`，预期全部测试通过。
- [ ] 检查所有自有 Markdown 均含 `中文` 与 `English` 导航。
- [ ] 运行 `git diff --check`，预期无空白错误。
- [ ] 提交并推送 `main`。

## English

> **For agentic workers:** Required sub-skill: use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert every first-party Markdown document to a GitHub-friendly single-file layout containing complete, mirrored Chinese and English content.

**Architecture:** Keep one canonical URL per document, add language anchors at the top, and place the complete Chinese version before the complete English version. Leave third-party documents untouched and keep commands, paths, metrics, APIs, and licensing conclusions identical across languages.

**Tech Stack:** GitHub Flavored Markdown, the repository's Python public-tree audit, and Git.

### Task 1: Entry and Contribution Documents

**Files:**
- Modify: `README.md`
- Modify: `CONTRIBUTING.md`

- [ ] Add language navigation, a Chinese section anchor, and a complete English mirror to each file.
- [ ] Verify that commands, performance tables, repository links, and licensing notices match across both languages.

### Task 2: Component and Backend Documents

**Files:**
- Modify: `converter/README.md`
- Modify: `components/el2d/README.md`
- Modify: `core/official_backend/README.md`
- Modify: `core/purism_backend/README.md`

- [ ] Add English to Chinese-only documents and Chinese to English-only documents.
- [ ] Use the same bilingual ordering and heading hierarchy throughout.

### Task 3: Core Technical Documents

**Files:**
- Modify: `docs/architecture.md`
- Modify: `docs/asset-pipeline.md`
- Modify: `docs/licensing-and-assets.md`

- [ ] Translate all prose, table headings, and diagram explanations.
- [ ] Preserve code blocks, formulas, command arguments, external links, and licensing boundaries.

### Task 4: Verification and Publication

- [ ] Run `python tools/audit_public_tree.py .`; expect exit code `0`.
- [ ] Run `python -m pytest -q`; expect all tests to pass.
- [ ] Verify every first-party Markdown file contains `中文` and `English` navigation.
- [ ] Run `git diff --check`; expect no whitespace errors.
- [ ] Commit and push `main`.
