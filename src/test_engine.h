#pragma once

#include <functional>
#include <string>
#include <vector>

struct TestOptions {
    std::wstring model_path;            // empty -> built-in deterministic convolution
    std::wstring report_path;           // empty -> %LOCALAPPDATA%/IntelNPUTest/reports
    int iterations = 100;
    int warmup = 5;
    double duration_seconds = 0.0;     // >0 overrides iterations
    bool probe_only = false;
    bool compare_cpu = true;            // built-in default; explicitly enabled for external model
    double atol = 0.03;
    double rtol = 0.05;
    double min_fps = 0.0;               // 0 disables threshold
    double max_p95_ms = 0.0;            // 0 disables threshold
};

struct TestResult {
    std::string status = "FAIL";
    std::string error_code = "NOT_STARTED";
    std::string error_message;
    std::string stage = "init";
    std::string npu_name;
    std::string npu_driver;
    std::vector<std::string> devices;
    std::string model_name;
    std::string input_shape;
    std::string input_type;
    std::string output_type;
    bool npu_detected = false;
    bool model_compiled_on_npu = false;
    bool npu_inference_successful = false;
    bool output_valid = false;
    bool cpu_compared = false;
    bool cpu_match = false;
    int successful_inferences = 0;
    double compile_ms = 0.0;
    double avg_ms = 0.0;
    double p95_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double throughput_fps = 0.0;
    double max_abs_diff = 0.0;
    std::wstring report_path;
    std::wstring log_path;
};

using LogCallback = std::function<void(const std::string&)>;

TestResult run_npu_test(const TestOptions& options, LogCallback on_log = {});
std::wstring default_report_folder();
