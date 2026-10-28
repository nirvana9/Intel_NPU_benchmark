#include "test_engine.h"

#include <openvino/openvino.hpp>
#include <openvino/opsets/opset8.hpp>
#include <openvino/core/type/float16.hpp>

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <iterator>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {
struct Failure : std::runtime_error {
    std::string code;
    Failure(std::string c, const std::string& message) : std::runtime_error(message), code(std::move(c)) {}
};

double elapsed_ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string utf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "[Unicode conversion error]";
    std::string value(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), value.data(), n, nullptr, nullptr);
    return value;
}

std::string json_quote(const std::string& s) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec << std::setfill(' ');
                } else out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}

std::string now_string() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[40];
    sprintf_s(buf, "%04u%02u%02u_%02u%02u%02u_%03u",
              t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}

std::string iso_now() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[48];
    sprintf_s(buf, "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
              t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}

std::wstring widen_ascii(const std::string& s) { return std::wstring(s.begin(), s.end()); }

std::wstring module_directory() {
    std::wstring buffer(32768, '\0');
    DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count == buffer.size()) throw Failure("PATH_ERROR", "Cannot determine EXE path");
    buffer.resize(count);
    return fs::path(buffer).parent_path().wstring();
}

std::shared_ptr<ov::Model> builtin_model() {
    auto x = std::make_shared<ov::opset8::Parameter>(ov::element::f32, ov::Shape{1, 3, 128, 128});
    x->set_friendly_name("input");
    std::mt19937 gen(20261028);
    std::uniform_real_distribution<float> d(-0.06f, 0.06f);
    std::vector<float> w1(16 * 3 * 3 * 3);
    std::vector<float> w2(16 * 16 * 3 * 3);
    for (float& v : w1) v = d(gen);
    for (float& v : w2) v = d(gen);
    auto c1 = ov::opset8::Constant::create(ov::element::f32, ov::Shape{16, 3, 3, 3}, w1);
    auto c2 = ov::opset8::Constant::create(ov::element::f32, ov::Shape{16, 16, 3, 3}, w2);
    auto y1 = std::make_shared<ov::opset8::Convolution>(
        x, c1, ov::Strides{1, 1}, ov::CoordinateDiff{1, 1}, ov::CoordinateDiff{1, 1}, ov::Strides{1, 1});
    auto r1 = std::make_shared<ov::opset8::Relu>(y1);
    auto y2 = std::make_shared<ov::opset8::Convolution>(
        r1, c2, ov::Strides{1, 1}, ov::CoordinateDiff{1, 1}, ov::CoordinateDiff{1, 1}, ov::Strides{1, 1});
    auto r2 = std::make_shared<ov::opset8::Relu>(y2);
    auto result = std::make_shared<ov::opset8::Result>(r2);
    return std::make_shared<ov::Model>(ov::ResultVector{result}, ov::ParameterVector{x}, "NPU_Conv_Relu_Functional");
}

std::string shape_text(const ov::Shape& shape) {
    std::ostringstream o;
    for (size_t i = 0; i < shape.size(); ++i) { if (i) o << 'x'; o << shape[i]; }
    return o.str();
}

void fill_input(ov::Tensor& tensor) {
    if (tensor.get_byte_size() > 128u * 1024u * 1024u)
        throw Failure("INPUT_TOO_LARGE", "Input tensor exceeds 128 MiB safety limit");
    const size_t n = tensor.get_size();
    std::mt19937 gen(20261028);
    std::uniform_real_distribution<float> f(-0.8f, 0.8f);
    auto type = tensor.get_element_type();
    if (type == ov::element::f32) {
        auto* a = tensor.data<float>(); for (size_t i=0; i<n; ++i) a[i] = f(gen);
    } else if (type == ov::element::f16) {
        auto* a = tensor.data<ov::float16>(); for (size_t i=0; i<n; ++i) a[i] = ov::float16(f(gen));
    } else if (type == ov::element::u8) {
        auto* a = tensor.data<uint8_t>(); for (size_t i=0; i<n; ++i) a[i] = static_cast<uint8_t>(gen() % 256);
    } else if (type == ov::element::i8) {
        auto* a = tensor.data<int8_t>(); for (size_t i=0; i<n; ++i) a[i] = static_cast<int8_t>(static_cast<int>(gen()%255)-127);
    } else if (type == ov::element::i32) {
        auto* a = tensor.data<int32_t>(); for (size_t i=0; i<n; ++i) a[i] = static_cast<int32_t>(gen()%101)-50;
    } else if (type == ov::element::i64) {
        auto* a = tensor.data<int64_t>(); for (size_t i=0; i<n; ++i) a[i] = static_cast<int64_t>(gen()%101)-50;
    } else {
        throw Failure("UNSUPPORTED_INPUT_TYPE", "Only f32/f16/u8/i8/i32/i64 model inputs are supported");
    }
}

std::vector<double> read_tensor(const ov::Tensor& t) {
    auto type = t.get_element_type();
    size_t count = t.get_size();
    if (count == 0 || count > 4'000'000 || t.get_byte_size() > 128u * 1024u * 1024u)
        throw Failure("OUTPUT_INVALID", "Empty or too large output tensor");
    std::vector<double> values(count);
    if (type == ov::element::f32) {
        const auto* p = t.data<const float>();
        for (size_t i=0; i<count; ++i) values[i] = p[i];
    } else if (type == ov::element::f16) {
        const auto* p = t.data<const ov::float16>();
        for (size_t i=0; i<count; ++i) values[i] = static_cast<float>(p[i]);
    } else if (type == ov::element::u8) {
        const auto* p = t.data<const uint8_t>();
        for (size_t i=0; i<count; ++i) values[i] = p[i];
    } else if (type == ov::element::i8) {
        const auto* p = t.data<const int8_t>();
        for (size_t i=0; i<count; ++i) values[i] = p[i];
    } else if (type == ov::element::i32) {
        const auto* p = t.data<const int32_t>();
        for (size_t i=0; i<count; ++i) values[i] = p[i];
    } else if (type == ov::element::i64) {
        const auto* p = t.data<const int64_t>();
        for (size_t i=0; i<count; ++i) values[i] = static_cast<double>(p[i]);
    } else {
        throw Failure("UNSUPPORTED_OUTPUT_TYPE", "Only f32/f16/u8/i8/i32/i64 model outputs are supported");
    }
    for (auto v : values) if (!std::isfinite(v)) throw Failure("OUTPUT_INVALID", "NaN or Inf in NPU output");
    return values;
}

std::vector<std::vector<double>> read_outputs(ov::InferRequest& req, size_t count) {
    std::vector<std::vector<double>> out;
    for (size_t i=0; i<count; ++i) out.push_back(read_tensor(req.get_output_tensor(i)));
    return out;
}

void compare_outputs(const std::vector<std::vector<double>>& npu,
                     const std::vector<std::vector<double>>& cpu,
                     const TestOptions& options, TestResult& r) {
    if (npu.size() != cpu.size()) throw Failure("CPU_COMPARE_FAILED", "Output count mismatch");
    double maxdiff = 0.0;
    size_t errors = 0;
    for (size_t j=0; j<npu.size(); ++j) {
        if (npu[j].size() != cpu[j].size()) throw Failure("CPU_COMPARE_FAILED", "Output shape mismatch");
        for (size_t i=0; i<npu[j].size(); ++i) {
            double diff = std::abs(npu[j][i] - cpu[j][i]);
            maxdiff = std::max(maxdiff, diff);
            if (diff > options.atol + options.rtol * std::abs(cpu[j][i])) ++errors;
        }
    }
    r.max_abs_diff = maxdiff;
    r.cpu_compared = true;
    r.cpu_match = errors == 0;
    if (errors) {
        std::ostringstream msg;
        msg << "NPU and CPU mismatch: " << errors << " values; max abs diff " << maxdiff;
        throw Failure("CPU_COMPARE_FAILED", msg.str());
    }
}

void write_report(const TestResult& r, const TestOptions& options, const std::string& stamp) {
    std::ofstream f(fs::path(r.report_path), std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("Could not open JSON report file");
    f << std::fixed << std::setprecision(4);
    f << "{\n"
      << "  \"time_local\": " << json_quote(stamp) << ",\n"
      << "  \"status\": " << json_quote(r.status) << ",\n"
      << "  \"error_code\": " << json_quote(r.error_code) << ",\n"
      << "  \"error_message\": " << json_quote(r.error_message) << ",\n"
      << "  \"stage\": " << json_quote(r.stage) << ",\n"
      << "  \"target_device\": \"NPU\",\n"
      << "  \"npu_name\": " << json_quote(r.npu_name) << ",\n"
      << "  \"npu_driver\": " << json_quote(r.npu_driver) << ",\n"
      << "  \"devices\": [";
    for (size_t i=0; i<r.devices.size(); ++i) { if (i) f << ", "; f << json_quote(r.devices[i]); }
    f << "],\n"
      << "  \"model\": " << json_quote(r.model_name) << ",\n"
      << "  \"input_shape\": " << json_quote(r.input_shape) << ",\n"
      << "  \"input_type\": " << json_quote(r.input_type) << ",\n"
      << "  \"output_type\": " << json_quote(r.output_type) << ",\n"
      << "  \"checks\": {\n"
      << "    \"npu_detected\": " << (r.npu_detected ? "true" : "false") << ",\n"
      << "    \"model_compiled_on_npu\": " << (r.model_compiled_on_npu ? "true" : "false") << ",\n"
      << "    \"npu_inference_successful\": " << (r.npu_inference_successful ? "true" : "false") << ",\n"
      << "    \"output_valid\": " << (r.output_valid ? "true" : "false") << ",\n"
      << "    \"cpu_compared\": " << (r.cpu_compared ? "true" : "false") << ",\n"
      << "    \"cpu_match\": " << (r.cpu_match ? "true" : "false") << "\n  },\n"
      << "  \"options\": {\"iterations\": " << options.iterations
      << ", \"warmup\": " << options.warmup
      << ", \"duration_seconds\": " << options.duration_seconds
      << ", \"compare_cpu\": " << (options.compare_cpu ? "true" : "false") << "},\n"
      << "  \"performance\": {\"inferences\": " << r.successful_inferences
      << ", \"npu_compile_ms\": " << r.compile_ms
      << ", \"avg_ms\": " << r.avg_ms
      << ", \"p95_ms\": " << r.p95_ms
      << ", \"min_ms\": " << r.min_ms
      << ", \"max_ms\": " << r.max_ms
      << ", \"throughput_fps\": " << r.throughput_fps
      << ", \"max_abs_diff\": " << r.max_abs_diff << "}\n}\n";
    f.flush();
    if (!f) throw std::runtime_error("Writing JSON report failed");
}

std::string property_string(ov::Core& core, const std::string& device, const std::string& prop) {
    try { return core.get_property(device, prop).as<std::string>(); }
    catch (...) { return {}; }
}

} // namespace

std::wstring default_report_folder() {
    wchar_t buffer[32768]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    fs::path base = (n > 0 && n < std::size(buffer)) ? fs::path(buffer) : fs::temp_directory_path();
    return (base / L"IntelNPUTest" / L"reports").wstring();
}

TestResult run_npu_test(const TestOptions& options, LogCallback on_log) {
    TestResult r;
    const std::string stamp = iso_now();
    std::ofstream log_file;
    auto log = [&](const std::string& msg) {
        if (log_file) { log_file << msg << '\n'; log_file.flush(); }
        if (on_log) on_log(msg);
    };
    try {
        if (options.iterations < 1 || options.warmup < 0 || options.duration_seconds < 0 || options.atol < 0 || options.rtol < 0 ||
            options.min_fps < 0 || options.max_p95_ms < 0)
            throw Failure("INVALID_ARGUMENT", "Invalid iteration, duration or threshold argument");
        const fs::path report_file = options.report_path.empty()
            ? (fs::path(default_report_folder()) / (L"npu_" + widen_ascii(now_string()) + L".json"))
            : fs::absolute(fs::path(options.report_path));
        fs::create_directories(report_file.parent_path());
        r.report_path = report_file.wstring();
        r.log_path = report_file.parent_path().append(report_file.stem().wstring() + L".log").wstring();
        log_file.open(fs::path(r.log_path), std::ios::binary | std::ios::trunc);
        log("Intel NPU Test C++ | " + stamp);
        log("[1/5] Enumerating OpenVINO hardware ...");
        r.stage = "detect";
        ov::Core core;
        r.devices = core.get_available_devices();
        for (const auto& d : r.devices) {
            log("Device: " + d);
            if (d == "NPU" || d.rfind("NPU.", 0) == 0) r.npu_detected = true;
        }
        if (!r.npu_detected) {
            // The bundled plugin normally registers automatically. Manual registration
            // is a fallback for portable deployments with different plugin layouts.
            auto npu_dll = fs::path(module_directory()) / L"openvino_intel_npu_plugin.dll";
            if (fs::is_regular_file(npu_dll)) {
                try {
                    core.register_plugin(npu_dll, "NPU");
                    r.devices = core.get_available_devices();
                    for (const auto& d : r.devices) if (d == "NPU" || d.rfind("NPU.", 0) == 0) r.npu_detected = true;
                } catch (const std::exception& ex) { log(std::string("NPU plugin registration: ") + ex.what()); }
            }
        }
        if (!r.npu_detected)
            throw Failure("NPU_NOT_FOUND", "NPU unavailable: verify Core Ultra NPU, BIOS, Intel NPU driver, and bundled OpenVINO NPU DLL");
        r.npu_name = property_string(core, "NPU", "FULL_DEVICE_NAME");
        r.npu_driver = property_string(core, "NPU", "NPU_DRIVER_VERSION");
        log("[OK] NPU detected: " + (r.npu_name.empty() ? std::string("NPU") : r.npu_name));
        if (options.probe_only) {
            r.status = "DETECTED";
            r.error_code = "NONE";
            r.stage = "probe_only";
            log("[DETECTED] Probe only. No inference executed; NOT a functional PASS.");
        } else {
            r.stage = "model";
            const bool builtin = options.model_path.empty();
            std::shared_ptr<ov::Model> model;
            if (builtin) {
                log("[2/5] Creating built-in Conv+ReLU test model ...");
                model = builtin_model();
                r.model_name = "built-in Conv+ReLU (128x128)";
            } else {
                fs::path model_path = fs::absolute(fs::path(options.model_path));
                if (!fs::is_regular_file(model_path) || model_path.extension() != L".xml")
                    throw Failure("MODEL_NOT_FOUND", "Model must be an existing OpenVINO .xml file");
                fs::path bin_path = model_path;
                bin_path.replace_extension(L".bin");
                if (!fs::is_regular_file(bin_path))
                    throw Failure("MODEL_BIN_MISSING", "Paired OpenVINO .bin model file is missing");
                log("[2/5] Loading external IR model: " + utf8(model_path.wstring()));
                try { model = core.read_model(model_path); }
                catch (const std::exception& ex) { throw Failure("MODEL_READ_FAILED", ex.what()); }
                r.model_name = utf8(model_path.wstring());
            }
            if (model->inputs().size() != 1) throw Failure("UNSUPPORTED_MODEL", "Only single-input models are supported");
            if (model->outputs().empty()) throw Failure("UNSUPPORTED_MODEL", "Model has no outputs");
            if (model->input().get_partial_shape().is_dynamic()) throw Failure("DYNAMIC_SHAPE", "Model input must have fixed shape");
            ov::Shape shape = model->input().get_shape();
            r.input_shape = shape_text(shape);
            r.input_type = model->input().get_element_type().get_type_name();
            size_t bytes_per_element = model->input().get_element_type().size();
            if (bytes_per_element == 0 || bytes_per_element > 128u * 1024u * 1024u)
                throw Failure("INPUT_TOO_LARGE", "Unsupported element size");
            size_t max_elements = (128u * 1024u * 1024u) / bytes_per_element;
            for (size_t dim : shape) {
                if (dim == 0 || dim > max_elements)
                    throw Failure("INPUT_TOO_LARGE", "Input shape too large or empty");
                max_elements /= dim;
            }
            if (model->output().get_partial_shape().is_dynamic())
                throw Failure("DYNAMIC_OUTPUT", "Model output must have fixed shape");
            r.output_type = model->output().get_element_type().get_type_name();
            ov::Tensor input(model->input().get_element_type(), shape);
            fill_input(input);
            r.stage = "npu_compile";
            log("[3/5] Compiling explicitly for NPU (no AUTO / CPU fallback) ...");
            auto compile_start = Clock::now();
            ov::CompiledModel compiled;
            try { compiled = core.compile_model(model, "NPU"); }
            catch (const std::exception& ex) { throw Failure("NPU_COMPILE_FAILED", ex.what()); }
            r.compile_ms = elapsed_ms(compile_start);
            r.model_compiled_on_npu = true;
            // Since only "NPU" was specified, no CPU fallback is permitted.
            log("[OK] NPU compile completed in " + std::to_string(r.compile_ms) + " ms");
            r.stage = "inference";
            ov::InferRequest req = compiled.create_infer_request();
            req.set_input_tensor(input);
            log("[4/5] Running warmup + measured real NPU inference ...");
            for (int i=0; i<options.warmup; ++i) req.infer();
            std::vector<double> samples;
            std::vector<std::vector<double>> first_outputs;
            auto total_start = Clock::now();
            while (true) {
                auto start = Clock::now();
                try { req.infer(); }
                catch (const std::exception& ex) { throw Failure("NPU_INFERENCE_FAILED", ex.what()); }
                samples.push_back(elapsed_ms(start));
                auto values = read_outputs(req, model->outputs().size());
                if (first_outputs.empty()) {
                    first_outputs = std::move(values);
                    if (builtin) {
                        bool some_nonzero = false;
                        for (const auto& o : first_outputs) for (auto v : o) if (std::abs(v) > 1e-8) { some_nonzero = true; break; }
                        if (!some_nonzero) throw Failure("OUTPUT_INVALID", "Built-in model output is all zero");
                    }
                }
                r.successful_inferences++;
                if (r.successful_inferences == 1 || r.successful_inferences % 50 == 0)
                    log("NPU inference OK: " + std::to_string(r.successful_inferences));
                if (options.duration_seconds > 0) {
                    if (std::chrono::duration<double>(Clock::now() - total_start).count() >= options.duration_seconds) break;
                } else if (r.successful_inferences >= options.iterations) break;
            }
            double total_secs = std::chrono::duration<double>(Clock::now() - total_start).count();
            r.npu_inference_successful = true;
            r.output_valid = true;
            std::sort(samples.begin(), samples.end());
            r.min_ms = samples.front(); r.max_ms = samples.back();
            r.avg_ms = std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
            const double p = 0.95 * static_cast<double>(samples.size() - 1);
            size_t a = static_cast<size_t>(std::floor(p)), b = static_cast<size_t>(std::ceil(p));
            r.p95_ms = samples[a] + (samples[b] - samples[a]) * (p - a);
            r.throughput_fps = r.successful_inferences / std::max(total_secs, 1e-9);
            if (options.compare_cpu) {
                r.stage = "cpu_compare";
                log("[5/5] Comparing one CPU reference inference ...");
                ov::CompiledModel cpu_model;
                try { cpu_model = core.compile_model(model, "CPU"); }
                catch (const std::exception& ex) { throw Failure("CPU_COMPARE_FAILED", std::string("CPU compile: ") + ex.what()); }
                auto cpu_req = cpu_model.create_infer_request();
                cpu_req.set_input_tensor(input);
                cpu_req.infer();
                compare_outputs(first_outputs, read_outputs(cpu_req, model->outputs().size()), options, r);
                log("[OK] CPU reference comparison passed");
            } else log("[5/5] CPU reference comparison disabled");
            r.stage = "threshold";
            if (options.min_fps > 0 && r.throughput_fps < options.min_fps)
                throw Failure("PERFORMANCE_THRESHOLD_FAILED", "Throughput below minimum threshold");
            if (options.max_p95_ms > 0 && r.p95_ms > options.max_p95_ms)
                throw Failure("PERFORMANCE_THRESHOLD_FAILED", "P95 latency above maximum threshold");
            r.status = "PASS";
            r.error_code = "NONE";
            r.stage = "complete";
            std::ostringstream stats;
            stats << std::fixed << std::setprecision(3)
                  << "[PASS] NPU true inference=" << r.successful_inferences
                  << ", avg=" << r.avg_ms << "ms, P95=" << r.p95_ms
                  << "ms, throughput=" << r.throughput_fps << " infer/s";
            log(stats.str());
        }
    } catch (const Failure& ex) {
        r.status = "FAIL"; r.error_code = ex.code; r.error_message = ex.what();
        log("[FAIL] " + r.error_code + ": " + r.error_message);
    } catch (const std::exception& ex) {
        r.status = "FAIL"; r.error_code = "RUNTIME_ERROR"; r.error_message = ex.what();
        log("[FAIL] " + r.error_message);
    } catch (...) {
        r.status = "FAIL"; r.error_code = "UNKNOWN_ERROR"; r.error_message = "Unknown native exception";
        log("[FAIL] Unknown native exception");
    }
    if (!r.report_path.empty()) {
        try { write_report(r, options, stamp); log("Report: " + utf8(r.report_path)); }
        catch (const std::exception& ex) {
            r.status = "FAIL";
            r.error_code = "REPORT_WRITE_FAILED";
            r.error_message = ex.what();
            log(std::string("[FAIL] Failed to save JSON report: ") + ex.what());
        }
    }
    return r;
}
