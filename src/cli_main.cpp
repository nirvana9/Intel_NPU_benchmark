#include "test_engine.h"
#include <windows.h>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void usage() {
    std::cout << "Intel NPU Test C++ (no Python / OpenVINO setup required on test PC)\n"
      "Usage: IntelNPUTestCLI.exe [options]\n"
      "  --probe                  Detect NPU only (DETECTED != PASS)\n"
      "  --model <file.xml>       Use custom OpenVINO IR (and matching .bin)\n"
      "  --iterations <N>         Measured inferences (default: 100)\n"
      "  --warmup <N>             Warmup inferences (default: 5)\n"
      "  --duration <seconds>     Duration mode instead of --iterations\n"
      "  --compare-cpu            Enable CPU reference comparison\n"
      "  --no-compare-cpu         Disable CPU reference comparison\n"
      "  --min-fps <value>        Fail if below throughput threshold\n"
      "  --max-p95-ms <value>     Fail if above P95 latency threshold\n"
      "  --atol <value>           CPU absolute tolerance (default .03)\n"
      "  --rtol <value>           CPU relative tolerance (default .05)\n"
      "  --report <file.json>     Output JSON report path\n"
      "Exit: 0=PASS or probe DETECTED, 2=FAIL, 3=bad command-line\n";
}
int invalid(const std::string& message) { std::cerr << "Argument error: " << message << '\n'; usage(); return 3; }
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    TestOptions o;
    bool compare_explicit = false;
    bool has_duration = false, has_iterations = false;
    try {
        for (int i=1; i<argc; ++i) {
            std::wstring a = argv[i];
            auto value = [&]() -> std::wstring {
                if (i + 1 >= argc) throw std::runtime_error("missing option argument");
                return argv[++i];
            };
            if (a == L"--help" || a == L"-h") { usage(); return 0; }
            else if (a == L"--probe") o.probe_only = true;
            else if (a == L"--model") o.model_path = value();
            else if (a == L"--report") o.report_path = value();
            else if (a == L"--iterations") { o.iterations = std::stoi(value()); has_iterations = true; }
            else if (a == L"--warmup") o.warmup = std::stoi(value());
            else if (a == L"--duration") { o.duration_seconds = std::stod(value()); has_duration = true; }
            else if (a == L"--min-fps") o.min_fps = std::stod(value());
            else if (a == L"--max-p95-ms") o.max_p95_ms = std::stod(value());
            else if (a == L"--atol") o.atol = std::stod(value());
            else if (a == L"--rtol") o.rtol = std::stod(value());
            else if (a == L"--compare-cpu") { o.compare_cpu = true; compare_explicit = true; }
            else if (a == L"--no-compare-cpu") { o.compare_cpu = false; compare_explicit = true; }
            else throw std::runtime_error("unknown option");
        }
        if (has_duration && has_iterations) return invalid("--duration and --iterations are mutually exclusive");
        if (!o.model_path.empty() && !compare_explicit) o.compare_cpu = false;
        if (o.probe_only && !o.model_path.empty()) return invalid("--probe cannot be used with --model");
        if (o.iterations < 1 || o.warmup < 0 || (has_duration && o.duration_seconds <= 0) ||
            o.atol < 0 || o.rtol < 0 || o.min_fps < 0 || o.max_p95_ms < 0)
            return invalid("numeric argument out of range");
    } catch (const std::exception& ex) { return invalid(ex.what()); }
    auto result = run_npu_test(o, [](const std::string& s){ std::cout << s << '\n' << std::flush; });
    if (result.status == "PASS") return 0;
    if (result.status == "DETECTED" && o.probe_only) return 0;
    return 2;
}
