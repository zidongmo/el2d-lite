# 许可与资产边界

本文记录项目发布边界，不构成法律意见。发布具体模型或商业产品前，应按你的主体、地区和用途核对相应条款。

## 本仓库可以公开的内容

- EL2D Lite 自有 C/C++/Python/HTML 代码：Apache License 2.0。
- Vendored meshoptimizer 子集：MIT License，版权与固定 revision 保留在 `third_party/meshoptimizer/`。
- 合成测试代码与测试运行时生成的数据：随本项目许可发布。
- PurismCore 适配边界、布局检查和编译探针：本仓库自有代码。

## 本仓库不分发的内容

- Live2D Cubism SDK、Cubism Core、Cubism Components 或官方 Web runtime。
- 官方样例模型、Haru 或其他角色的 `.moc3`、纹理、动作、表情和物理文件。
- 从上述模型提取的 drawable snapshot、量化纹理或生成 C 数组。
- 外部产品固件、设备驱动、密钥、网络配置和业务状态映射。

离线提取器只接受用户通过 `--runtime-dir` 指定的本地 runtime，不复制或嵌入该 runtime。该技术边界不替用户判断其使用是否合法。

## PurismCore

[SakuraMotion/PurismCore](https://github.com/SakuraMotion/PurismCore) 声明为 MIT 许可的 Cubism Core 开源重实现。本仓库不 vendoring PurismCore；`EL2D_PURISMCORE_DIR` 只读取用户本地 checkout。若未来 vendoring，必须保留其 MIT 版权声明并固定上游 revision。

PurismCore 的开源许可不自动改变输入模型、编辑器输出或 Live2D 商标的许可状态。

## 官方资料

- [Live2D SDK licensing](https://www.live2d.com/en/sdk/license/)
- [Live2D Open Software License](https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html)
- [Live2D sample data terms](https://www.live2d.com/en/learn/sample/model-terms/)
- [Live2D Free Material License](https://www.live2d.com/eula/live2d-free-material-license-agreement_en.html)

不要仅因为仓库不再分发官方文件，就推断最终应用一定不需要额外许可。嵌入式发布、商业主体、可扩展应用和模型再分发可能有不同要求。

## 商标

Live2D 和 Cubism 是 Live2D Inc. 的商标或注册商标。项目名称使用中性的 EL2D Lite；文档中的 Live2D/Cubism 只用于描述兼容工作流和输入格式。本项目不暗示官方隶属、认证或支持。

## 贡献者检查表

- 新 fixture 是代码生成的合成数据，或有清晰且兼容的公开许可。
- 没有提交 `.moc3`、官方 runtime、角色纹理或模型派生 C 数组。
- 第三方源码包含许可证、版权和固定版本信息。
- 文档示例使用 `Avatar`、`Synthetic` 等中性名称和占位路径。
- `python tools/audit_public_tree.py .` 通过。
