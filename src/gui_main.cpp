#include "test_engine.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <atomic>
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <cwchar>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr int ID_START = 1001, ID_BROWSE = 1002, ID_OPEN_REPORTS = 1003;
constexpr int ID_ITER = 1004, ID_MODEL = 1005, ID_COMPARE = 1006, ID_LOG = 1007, ID_STATUS = 1008;
constexpr UINT WM_LOG_TEXT = WM_APP + 11, WM_TEST_FINISHED = WM_APP + 12;

HWND g_window = nullptr;
HFONT g_font = nullptr;
std::atomic<bool> g_running{false};
std::wstring g_report_folder;

std::wstring as_wide(const std::string& input) {
    if (input.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0);
    if (len <= 0) return L"[Invalid UTF-8]";
    std::wstring out(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), out.data(), len);
    return out;
}

void log_async(HWND hwnd, const std::string& message) {
    auto* owned = new std::wstring(as_wide(message));
    if (!PostMessageW(hwnd, WM_LOG_TEXT, 0, reinterpret_cast<LPARAM>(owned))) delete owned;
}

std::wstring control_text(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring text(static_cast<size_t>(n+1), L'\0');
    GetWindowTextW(h, text.data(), n+1);
    text.resize(wcslen(text.c_str()));
    return text;
}

HWND create_control(const wchar_t* kind, const wchar_t* text, DWORD extra,
                    int x, int y, int w, int h, int id) {
    HWND result = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | extra,
                                  x, y, w, h, g_window,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    return result;
}

void enable_controls(bool enable) {
    for (int id : {ID_START, ID_BROWSE, ID_ITER, ID_MODEL, ID_COMPARE})
        EnableWindow(GetDlgItem(g_window, id), enable ? TRUE : FALSE);
}

void append_text(HWND edit, const std::wstring& text) {
    int len = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, len, len);
    std::wstring line = text + L"\r\n";
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

void start_test(HWND hwnd) {
    if (g_running.exchange(true)) return;
    TestOptions o;
    try {
        o.model_path = control_text(GetDlgItem(hwnd, ID_MODEL));
        o.iterations = std::stoi(control_text(GetDlgItem(hwnd, ID_ITER)));
        o.compare_cpu = SendMessageW(GetDlgItem(hwnd, ID_COMPARE), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (o.iterations < 1 || o.iterations > 100000)
            throw std::runtime_error("Iteration count must be between 1 and 100000");
    } catch (...) {
        g_running = false;
        MessageBoxW(hwnd, L"推理次数请输入 1 ~ 100000 的整数。", L"输入错误", MB_ICONWARNING);
        return;
    }
    SetWindowTextW(GetDlgItem(hwnd, ID_LOG), L"");
    SetWindowTextW(GetDlgItem(hwnd, ID_STATUS), L"正在运行 NPU 推理测试...");
    enable_controls(false);
    try {
        std::thread([hwnd, o]() {
            auto result = run_npu_test(o, [hwnd](const std::string& line) { log_async(hwnd, line); });
            auto* owned = new TestResult(std::move(result));
            if (!PostMessageW(hwnd, WM_TEST_FINISHED, 0, reinterpret_cast<LPARAM>(owned))) delete owned;
        }).detach();
    } catch (...) {
        g_running = false;
        enable_controls(true);
        SetWindowTextW(GetDlgItem(hwnd, ID_STATUS), L"线程创建失败");
    }
}

void choose_model(HWND hwnd) {
    wchar_t file[MAX_PATH * 4]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = hwnd;
    dialog.lpstrFile = file;
    dialog.nMaxFile = static_cast<DWORD>(std::size(file));
    dialog.lpstrFilter = L"OpenVINO IR 模型 (*.xml)\0*.xml\0全部文件 (*.*)\0*.*\0\0";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&dialog)) {
        SetWindowTextW(GetDlgItem(hwnd, ID_MODEL), file);
        SendMessageW(GetDlgItem(hwnd, ID_COMPARE), BM_SETCHECK, BST_UNCHECKED, 0);
    }
}

void build_ui() {
    g_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    create_control(L"STATIC", L"Intel Core Ultra NPU 测试  |  OpenVINO C++", 0, 20, 14, 670, 28, 0);
    create_control(L"STATIC", L"内置模型直接测试；外部模型请同时准备 XML/BIN", 0, 20, 47, 660, 21, 0);
    create_control(L"STATIC", L"推理次数：", 0, 20, 84, 85, 25, 0);
    create_control(L"EDIT", L"100", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, 105, 81, 82, 26, ID_ITER);
    create_control(L"BUTTON", L"CPU 参考对比（内置模型推荐）", BS_AUTOCHECKBOX | WS_TABSTOP,
                   215, 83, 270, 24, ID_COMPARE);
    SendMessageW(GetDlgItem(g_window, ID_COMPARE), BM_SETCHECK, BST_CHECKED, 0);
    create_control(L"STATIC", L"外部 IR 模型（留空使用内置模型）：", 0, 20, 122, 425, 24, 0);
    create_control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                   20, 148, 545, 27, ID_MODEL);
    create_control(L"BUTTON", L"浏览模型", BS_PUSHBUTTON | WS_TABSTOP,
                   575, 148, 110, 27, ID_BROWSE);
    create_control(L"BUTTON", L"开始 NPU 测试", BS_DEFPUSHBUTTON | WS_TABSTOP,
                   20, 194, 180, 40, ID_START);
    create_control(L"BUTTON", L"打开报告目录", BS_PUSHBUTTON | WS_TABSTOP,
                   213, 194, 155, 40, ID_OPEN_REPORTS);
    create_control(L"STATIC", L"未测试", WS_BORDER | SS_CENTER | SS_CENTERIMAGE,
                   384, 194, 301, 40, ID_STATUS);
    create_control(L"EDIT", L"", WS_BORDER | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                   20, 251, 665, 290, ID_LOG);
    create_control(L"STATIC", L"PASS = NPU 编译、真实推理、输出校验均通过；只检测到设备不算通过。", 0,
                   20, 553, 660, 28, 0);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_CREATE: {
            g_window = hwnd;
            g_report_folder = default_report_folder();
            build_ui();
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(w)) {
                case ID_START: start_test(hwnd); break;
                case ID_BROWSE: choose_model(hwnd); break;
                case ID_OPEN_REPORTS: {
                    try { std::filesystem::create_directories(std::filesystem::path(g_report_folder)); }
                    catch (...) { MessageBoxW(hwnd, L"无法创建报告目录。", L"目录错误", MB_ICONERROR); break; }
                    ShellExecuteW(hwnd, L"open", g_report_folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                }
                default: break;
            }
            return 0;
        }
        case WM_LOG_TEXT: {
            std::unique_ptr<std::wstring> line(reinterpret_cast<std::wstring*>(l));
            append_text(GetDlgItem(hwnd, ID_LOG), *line);
            return 0;
        }
        case WM_TEST_FINISHED: {
            std::unique_ptr<TestResult> result(reinterpret_cast<TestResult*>(l));
            g_running = false;
            enable_controls(true);
            std::wstring outcome = as_wide(result->status);
            if (result->status == "FAIL") outcome += L"  |  " + as_wide(result->error_code);
            SetWindowTextW(GetDlgItem(hwnd, ID_STATUS), outcome.c_str());
            if (result->status == "PASS")
                MessageBoxW(hwnd, L"NPU 真实推理测试通过，报告已保存。", L"PASS", MB_OK | MB_ICONINFORMATION);
            else
                MessageBoxW(hwnd, (L"NPU 测试未通过。\n错误码：" + as_wide(result->error_code) +
                                   L"\n详情请查看窗口日志及 JSON 报告。").c_str(),
                            L"FAIL", MB_OK | MB_ICONERROR);
            return 0;
        }
        case WM_CLOSE:
            if (g_running) {
                MessageBoxW(hwnd, L"NPU 测试进行中，请等待本轮测试结束再关闭。", L"运行中", MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0); return 0;
        default: return DefWindowProcW(hwnd, msg, w, l);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.hInstance = inst;
    cls.lpfnWndProc = window_proc;
    cls.lpszClassName = L"IntelNPUTestWin32";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    if (!RegisterClassExW(&cls)) return 3;
    HWND hwnd = CreateWindowExW(0, cls.lpszClassName, L"Intel NPU Test Tool v2.0",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
        730, 630, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 3;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return static_cast<int>(msg.wParam);
}
