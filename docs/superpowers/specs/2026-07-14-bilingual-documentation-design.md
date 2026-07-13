# EL2D Lite 双语文档设计 / Bilingual Documentation Design

[中文](#中文) | [English](#english)

## 中文

### 目标

将仓库内全部自有 Markdown 文档统一为单文件中英双语格式，让中文和英文读者都能从原始链接获取完整、等价的项目信息。

### 范围

- `README.md`
- `CONTRIBUTING.md`
- `docs/architecture.md`
- `docs/asset-pipeline.md`
- `docs/licensing-and-assets.md`
- `converter/README.md`
- `components/el2d/README.md`
- `core/official_backend/README.md`
- `core/purism_backend/README.md`

`third_party/` 下的上游 README、许可证和归属文件保持原样。

### 格式

每份文档顶部放置 `中文 | English` 锚点导航，随后依次提供完整中文和完整英文。两个语言版本保持相同的章节层级、命令、链接、限制条件和技术结论。代码块不翻译标识符；表格标题与解释性文字分别本地化。

### 质量要求

- 不改变 API、命令、性能数据、许可边界或项目承诺。
- 不把 Live2D、Cubism、PurismCore 等专有名词改写成不准确的译名。
- 所有相对链接在原文件位置继续有效。
- Markdown 链接和仓库公开树审计通过。
- 英文应为自然技术写作，不做逐字直译。

## English

### Goal

Convert every first-party Markdown document in the repository to a bilingual single-file format so Chinese and English readers receive complete, equivalent project information from the same canonical link.

### Scope

- `README.md`
- `CONTRIBUTING.md`
- `docs/architecture.md`
- `docs/asset-pipeline.md`
- `docs/licensing-and-assets.md`
- `converter/README.md`
- `components/el2d/README.md`
- `core/official_backend/README.md`
- `core/purism_backend/README.md`

Upstream READMEs, licenses, and attribution files under `third_party/` remain unchanged.

### Format

Each document starts with `中文 | English` anchor navigation, followed by a complete Chinese version and a complete English version. Both versions retain matching section hierarchy, commands, links, constraints, and technical conclusions. Identifiers in code blocks remain unchanged; table headings and explanatory prose are localized for each language.

### Quality Requirements

- Do not change APIs, commands, performance figures, licensing boundaries, or project commitments.
- Keep product and project names such as Live2D, Cubism, and PurismCore technically accurate.
- Preserve every relative link from its existing document location.
- Pass Markdown link checks and the public-tree audit.
- Write natural technical English instead of literal sentence-by-sentence translation.
