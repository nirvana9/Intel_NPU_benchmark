"""Static handoff checks (cannot substitute for MSVC compile or a real Intel NPU)."""
from pathlib import Path
p = Path(__file__).resolve().parents[1]
engine = (p/'src/test_engine.cpp').read_text(encoding='utf-8')
cli = (p/'src/cli_main.cpp').read_text(encoding='utf-8')
gui = (p/'src/gui_main.cpp').read_text(encoding='utf-8')
cmake = (p/'CMakeLists.txt').read_text(encoding='utf-8')
ps = (p/'scripts/build_release.ps1').read_text(encoding='utf-8')
iss = (p/'installer/IntelNPUTest.iss').read_text(encoding='utf-8')
checks = {
    'strict NPU compile': 'core.compile_model(model, "NPU")' in engine,
    'CPU reference': 'core.compile_model(model, "CPU")' in engine,
    'NPU no AUTO': 'core.compile_model(model, "AUTO")' not in engine,
    'GUI thread and status': 'std::thread' in gui and 'WM_TEST_FINISHED' in gui,
    'CLI exit codes': 'return 2;' in cli and 'return 3;' in cli,
    'reports written': 'write_report(r, options, stamp)' in engine,
    'OpenVINO CMake': 'openvino::runtime' in cmake,
    'runtime included': 'openvino_intel_npu_plugin.dll' in ps and 'runtime\\3rdparty' in ps,
    'offline VC redist': 'vc_redist.x64.exe' in iss and 'HasVCRedist' in iss,
    'Inno installer': 'OutputBaseFilename=IntelNPUTest_Setup_v2.0' in iss,
    'no Python on testing PC': 'pip install' not in ps,
}
for name, ok in checks.items():
    print(('PASS' if ok else 'FAIL') + ': ' + name)
assert all(checks.values()), 'Static packaging validation failed'
