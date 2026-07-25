# EL2D Lite 贡献指南 / Contribution Guide

[中文](#中文) | [English](#english)

## 中文

感谢参与 EL2D Lite。项目仍处于技术预览期，优先接受可复现的兼容性、性能和资产自动化改进。

### 开发环境

```powershell
python -m pip install -e ".[dev]"
cmake -S . -B build -DEL2D_BUILD_TESTS=ON -DEL2D_BUILD_TOOLS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python -m pytest -q
python tools\audit_public_tree.py .
```

### 变更要求

- 新行为先增加失败测试，再实现最小改动。
- Runtime API 保持 C11 和业务无关，不加入产品状态、LCD 或网络依赖。
- 性能优化需记录设备、编译选项、模型规模和 P50/P95；不要只提交单帧最佳值。
- 转换器功能使用临时生成的 synthetic fixture 测试。
- 不提交模型、官方 runtime、模型派生资产或外部产品代码。
- 新第三方依赖必须说明许可证、上游 URL、固定 revision 和不使用系统库的原因。

提交信息建议使用 Conventional Commits，例如 `feat: add binary asset bounds checks` 或 `fix: preserve mask topology during lod`。

## English

Thank you for contributing to EL2D Lite. The project is still a technical preview and prioritizes reproducible improvements to compatibility, performance, and asset automation.

### Development Environment

```powershell
python -m pip install -e ".[dev]"
cmake -S . -B build -DEL2D_BUILD_TESTS=ON -DEL2D_BUILD_TOOLS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
python -m pytest -q
python tools\audit_public_tree.py .
```

### Change Requirements

- Add a failing test for new behavior before implementing the smallest necessary change.
- Keep the runtime API C11-compatible and independent of business logic; do not add product-state, LCD, or network dependencies.
- Performance optimizations must record the device, compiler options, model size, and P50/P95 figures; do not submit only a best-case single-frame result.
- Test converter features with temporarily generated synthetic fixtures.
- Do not submit models, the official runtime, model-derived assets, or external product code.
- New third-party dependencies must document their license, upstream URL, pinned revision, and the reason for not using a system library.

Conventional Commits are recommended for commit messages, for example `feat: add binary asset bounds checks` or `fix: preserve mask topology during lod`.
