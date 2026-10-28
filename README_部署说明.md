# Intel Core Ultra NPU 产线测试工具 · C++ 版本 2.0

## 1. 目标

根据上传的《intel core ultra系列通调动NPU测试方案》改为原生 Windows C++ 软件：

- **测试人员仅安装 `IntelNPUTest_Setup_v2.0.exe`，双击“Intel NPU 测试”即可运行**；
- 测试人员无需安装 Python / OpenVINO / Visual Studio / CMake，也无需编辑 PATH 或运行 setupvars.bat；
- 安装包内含官方 OpenVINO DLL、NPU 及 CPU 插件、IR 前端、依赖 DLL、Visual C++ 运行库安装程序；
- **唯一不可随普通 EXE 替代的是 Intel NPU 内核/用户态驱动**：必须已随设备厂商系统镜像或 Windows Update 正常安装；
- 默认内置静态 2 层 Conv+ReLU 推理模型，不需要单独寻找 MobileNet-V2；
- 可选使用附件提到的 MobileNet-V2 `.xml` 和 `.bin` 做外部模型验证；
- NPU 一定由 `core.compile_model(model,"NPU")` 执行，不启用 AUTO、HETERO 或 CPU 回退；
- 自动判断 PASS/FAIL，写入 JSON + TXT 日志，方便产线集成。

**重要：本仓库包含源码和自动打包方案，但不包含已编译的 Windows 安装 EXE。** 当前交付环境没有 Windows/MSVC/OpenVINO x64 DLL，也没有 Intel NPU。需在开发用 Windows 电脑上执行一次构建脚本生成安装包，并在实际 Core Ultra 设备上做功能验收。

## 2. 项目结构

```text
Intel_NPU_CPP_v2/
  CMakeLists.txt
  src/
    test_engine.h           测试参数 / 报告结构 / API
    test_engine.cpp         OpenVINO NPU 推理核心、CPU 比对、性能、JSON日志
    gui_main.cpp            Win32 中文图形界面，不依赖 Qt
    cli_main.cpp            产线脚本用 CLI EXE
  scripts/
    build_release.ps1       编译 + 打包全部运行库 + 可选生成安装包
  installer/
    IntelNPUTest.iss        Inno Setup 离线单文件安装 EXE
  tests/
    validate_package.py    跨平台静态交付检查（非 Windows 硬件测试）
  README_部署说明.md
```

## 3. 第一次打包（仅开发人员执行）

需要一台 Windows 11 x64 开发机，安装以下**构建工具**（与产线电脑无关）：

1. Visual Studio 2022：勾选“使用 C++ 的桌面开发”和 Windows SDK。
2. CMake 3.26 或更高版本（加入构建机 PATH）。
3. 下载 Intel 官方 **OpenVINO Windows x64 C++ 归档版**，解压到 `D:\SDK\openvino_2026`（示例；应包含 `runtime\cmake\OpenVINOConfig.cmake` 和 `runtime\bin\intel64\Release`）。**用官方完整版，不用 Python pip 包。**
4. 下载微软官方 `vc_redist.x64.exe`，如 `D:\SDK\vc_redist.x64.exe`，供安装包静默安装 Microsoft VC++ runtime。
5. 安装 Inno Setup 6（仅打包电脑需要）。

在项目目录打开 PowerShell：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build_release.ps1 `
    -OpenVINO 'D:\SDK\openvino_2026' `
    -VCRedist 'D:\SDK\vc_redist.x64.exe' `
    -MakeInstaller
```

如 Inno Setup 安装在非标准目录，另加参数：

```powershell
    -InnoSetupCompiler 'D:\Tools\Inno Setup 6\ISCC.exe'
```

生成物：

```text
dist/
  IntelNPUTest_Setup_v2.0.exe     <-- 复制给测试人员（离线安装包）
  IntelNPUTest/                   <-- 免安装便携目录（需系统 VC++ 运行库）
    IntelNPUTest.exe             <-- Win32 GUI
    IntelNPUTestCLI.exe          <-- 产线自动化接口
    openvino.dll
    openvino_intel_npu_plugin.dll
    openvino_intel_cpu_plugin.dll
    openvino_ir_frontend.dll
    [OpenVINO/oneTBB/编译器附属 DLL ...]
    licenses/
```

脚本将 Release 文件夹和 3rdparty 下的 DLL 都打包，优先保证离线兼容性，包体会较大。遵守 OpenVINO 及其第三方许可证要求：分发前检查 `dist/IntelNPUTest/licenses` 是否包括所有适用的第三方声明，缺少的需从官方归档补齐。

**无需全局 OpenVINO 环境变量**；安装包将 DLL 放在程序 EXE 同目录，通过 Windows DLL 搜索机制加载。部分 OpenVINO 版本的可选插件/依赖布局可能变化，需用目标版本的完整归档验证一次。尽可能固定已验收的 OpenVINO 和 NPU 驱动版本。

## 4. 测试人员的操作

1. 在目标机器确认 Windows 11 x64，搭载 **Intel Core Ultra NPU**，设备管理器“Intel(R) NPU Accelerator”驱动正常。
2. 双击 `IntelNPUTest_Setup_v2.0.exe` 一次安装（无需联网；可能要求管理员权限，安装器同时安装 VC++ 运行库）。
3. 从桌面打开“Intel NPU 测试”，默认模型留空，推理次数保留 100，点击“开始 NPU 测试”。
4. 看到 `PASS` 才表示本次 NPU 真实推理通过；`FAIL` 查看错误码。`CPU 参考对比` 默认选中，可根据项目验收规则取消（取消后仍验证 NPU 输出有限且非空）。
5. 点击“打开报告目录”。报告实际保存于：

```text
%LOCALAPPDATA%\IntelNPUTest\reports\
  npu_20261028_XXXXXX_XXX.json
  npu_20261028_XXXXXX_XXX.log
```

**没有 NPU 驱动时**程序会 FAIL，安装包无法自动补齐硬件专属驱动。可提前将经 OEM 验证的驱动安装到工厂系统镜像。

## 5. CMD/产线自动化调用

同一安装目录有 `IntelNPUTestCLI.exe`：

```cmd
:: 默认内置模型，5 次预热 + 100 次 NPU 推理 + CPU 对照
"C:\Program Files\IntelNPUTest\IntelNPUTestCLI.exe"

:: 只检查 NPU 是否枚举（不是功能 PASS）
"C:\Program Files\IntelNPUTest\IntelNPUTestCLI.exe" --probe

:: 快速产测
"C:\Program Files\IntelNPUTest\IntelNPUTestCLI.exe" --iterations 20 --warmup 3

:: 持续推理 10 分钟
"C:\Program Files\IntelNPUTest\IntelNPUTestCLI.exe" --duration 600 --warmup 10

:: 使用附件的 MobileNet-V2 模型文件（默认不强制与 CPU 对比）
"C:\Program Files\IntelNPUTest\IntelNPUTestCLI.exe" --model "D:\model\mobilenet-v2.xml" --iterations 100

:: 指定性能合格门限（以下数字仅是参数格式示例，不代表合格标准）
"C:\Program Files\IntelNPUTest\IntelNPUTestCLI.exe" --min-fps 10 --max-p95-ms 200
```

退出码：`0` = `PASS`；`2` = `FAIL`；`3` = 命令行参数错误。**`--probe` 模式检测到设备返回 0，但其报告状态是 `DETECTED`，不能视为功能 PASS**。产线调用尽量使用默认真实推理模式。

## 6. PASS / FAIL 判定

| 项目 | 判定条件 | 错误码示例 |
|---|---|---|
| NPU 检测 | OpenVINO 枚举到 NPU | `NPU_NOT_FOUND` |
| 模型编译 | 明确指定 NPU 编译模型成功 | `NPU_COMPILE_FAILED` |
| 真实推理 | 完成指定次数/时长的实际 NPU 调用 | `NPU_INFERENCE_FAILED` |
| 输出有效 | 每次推理输出非空、无 NaN/Inf；内置模型不能全零 | `OUTPUT_INVALID` |
| CPU 对照（默认内置模型启用） | 元素误差满足 `abs_diff <= atol + rtol*abs(cpu)` | `CPU_COMPARE_FAILED` |
| 可选性能门限 | `--min-fps` 与 `--max-p95-ms` 指定后检查 | `PERFORMANCE_THRESHOLD_FAILED` |

平均延迟和 P95 是单次同步 `infer()` 调用延迟；吞吐率按整个连续运行阶段耗时计算（包含输出检查），不能直接与官方 `benchmark_app` 的吞吐结果横向等同。默认不设置 FPS 硬性门限：不同 Core Ultra 代际、驱动、模型与功耗模式没有统一数值。

注意：这属于**NPU 功能与基础稳定性测试**，不等于 NPU 电气测试、全算子覆盖或 AI 模型精度认证。外部 MobileNet-V2 没有真实图片标注，使用合成输入只验证推理链路可用，不是分类精度验证。外部模型仅支持单输入、静态维度以及指定的数值数据类型。

## 7. 示例报告（示意数值，非实测）

```json
{
  "time_local": "2026-10-28T09:30:00.000",
  "status": "PASS",
  "error_code": "NONE",
  "target_device": "NPU",
  "checks": {
    "npu_detected": true,
    "model_compiled_on_npu": true,
    "npu_inference_successful": true,
    "output_valid": true,
    "cpu_compared": true,
    "cpu_match": true
  },
  "performance": {
    "inferences": 100,
    "avg_ms": 5.0,
    "p95_ms": 6.1,
    "throughput_fps": 190.0
  }
}
```

## 8. 与原附件对应

- 附件 P1–P3：CMake/VS/OpenVINO 安装和 `setupvars.bat` → **开发人员一次性构建，测试人员无需做这些操作**。
- 附件 P4：`benchmark_app.exe` + MobileNet-V2 XML/BIN → **内置模型开箱可测试，也支持外部模型**。
- 附件 P5：`-d NPU` → **C++ 明确调用 `compile_model(model, "NPU")`，并增加校验与报告**。

## 9. 官方参考

- OpenVINO NPU 设备与驱动： https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html
- OpenVINO 本地发布库： https://docs.openvino.ai/2025/openvino-workflow/deployment-locally/local-distribution-libraries.html
- OpenVINO Windows archive： https://docs.openvino.ai/2026/get-started/install-openvino/install-openvino-archive-windows.html
- 微软 VC++ Redist： https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist
