#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <cwchar>
#include <shellapi.h>

#include "process_inventory.hpp"
#include "tetris/client_profile.hpp"
#include "tetris/private_memory_policy.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {
constexpr int kWindowList = 1001;
constexpr int kRefresh = 1002;
constexpr int kInspect = 1003;
constexpr int kStatus = 1004;
constexpr int kOpenReport = 1005;
constexpr int kCaptureSnapshot = 1006;
constexpr int kCompareSnapshots = 1007;

struct AppState {
    HWND window{};
    HWND windowList{};
    HWND status{};
    HWND openReportButton{};
    std::wstring reportPath;
};

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string output(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        output.data(), count, nullptr, nullptr);
    return output;
}

std::wstring ExecutableFolder() {
    std::vector<wchar_t> path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
        static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return L".";
    return std::filesystem::path(std::wstring(path.data(), length))
        .parent_path().wstring();
}

BOOL CALLBACK EnumerateWindow(HWND hwnd, LPARAM parameter) {
    auto* state = reinterpret_cast<AppState*>(parameter);
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId()) return TRUE;

    const int length = GetWindowTextLengthW(hwnd);
    if (length < 2 || length > 512) return TRUE;
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(hwnd, title.data(), length + 1);
    if (copied <= 0) return TRUE;
    title.resize(static_cast<std::size_t>(copied));

    const LRESULT index = SendMessageW(state->windowList, CB_ADDSTRING, 0,
        reinterpret_cast<LPARAM>(title.c_str()));
    if (index != CB_ERR && index != CB_ERRSPACE) {
        SendMessageW(state->windowList, CB_SETITEMDATA,
            static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(hwnd));
    }
    return TRUE;
}

void RefreshWindows(AppState* state) {
    SendMessageW(state->windowList, CB_RESETCONTENT, 0, 0);
    EnumWindows(EnumerateWindow, reinterpret_cast<LPARAM>(state));
    const LRESULT count = SendMessageW(state->windowList, CB_GETCOUNT, 0, 0);
    if (count > 0) SendMessageW(state->windowList, CB_SETCURSEL, 0, 0);
    const std::wstring message = L"已发现 " + std::to_wstring(count) + L" 个可见窗口。";
    SetWindowTextW(state->status, message.c_str());
}

std::wstring ProtectionLabel(std::uint32_t protection) {
    std::wostringstream stream;
    stream << L"0x" << std::hex << protection;
    return stream.str();
}

const wchar_t* FingerprintStatusText(tetris::ClientFingerprintStatus status) {
    switch (status) {
    case tetris::ClientFingerprintStatus::Match:
        return L"匹配：已识别为支持的目标客户端";
    case tetris::ClientFingerprintStatus::ExecutableSizeMismatch:
        return L"不匹配：EXE 文件大小不同";
    case tetris::ClientFingerprintStatus::ArchitectureMismatch:
        return L"不匹配：进程架构不同";
    case tetris::ClientFingerprintStatus::Sha256Mismatch:
        return L"不匹配：SHA-256 不同";
    }
    return L"不匹配：未知指纹状态";
}

bool WriteReport(const tetris::win32::ProcessInventory& inventory,
                 const std::wstring& path) {
    std::wostringstream report;
    report << L"Tetris Pilot Next - read-only process inventory\n";
    report << L"PID: " << inventory.processId << L"\n";
    report << L"Architecture: " << inventory.architecture << L"\n";
    report << L"Executable: " << inventory.executablePath << L"\n";
    report << L"Executable size: " << inventory.executableSize << L" bytes\n";
    report << L"Executable SHA-256: "
        << (inventory.executableSha256.empty() ? L"unavailable" : inventory.executableSha256) << L"\n";
    if (!inventory.fingerprintError.empty())
        report << L"Fingerprint warning: " << inventory.fingerprintError << L"\n";
    const auto fingerprintStatus = tetris::CheckTargetClientFingerprint(
        inventory.executableSize, inventory.executableSha256,
        inventory.architecture);
    report << L"\n[Target client profile]\n";
    report << L"Expected architecture: "
        << tetris::kTargetProcessArchitecture << L"\n";
    report << L"Expected executable size: "
        << tetris::kTargetExecutableSize << L" bytes\n";
    report << L"Expected SHA-256: "
        << tetris::kTargetExecutableSha256 << L"\n";
    report << L"Fingerprint result: "
        << FingerprintStatusText(fingerprintStatus) << L"\n";

    report << L"\n[Modules]\n";
    for (const auto& module : inventory.modules) {
        report << L"Name: " << module.name
            << L" | Base: 0x" << std::hex << module.base
            << L" | Size: 0x" << module.size << std::dec
            << L" | Path: " << module.path << L"\n";
    }
    report << L"Module count: " << inventory.modules.size() << L"\n";
    if (!inventory.moduleError.empty())
        report << L"Module enumeration warning: " << inventory.moduleError << L"\n";

    report << L"\n[Committed MEM_IMAGE regions]\n";
    for (const auto& region : inventory.imageRegions) {
        report << L"Base: 0x" << std::hex << region.base
            << L" | AllocationBase: 0x" << region.allocationBase
            << L" | Size: 0x" << region.size
            << L" | Protect: " << ProtectionLabel(region.protection)
            << std::dec << L"\n";
    }
    report << L"Image region count: " << inventory.imageRegions.size() << L"\n";
    report << L"\n[Readable committed MEM_PRIVATE regions >= 0x4000]\n";
    report << L"Committed private region count: "
        << inventory.privateRegionCount << L"\n";
    report << L"Committed private bytes: "
        << inventory.privateCommittedBytes << L"\n";
    report << L"Readable private region count: "
        << inventory.readablePrivateRegionCount << L"\n";
    report << L"Readable private bytes: "
        << inventory.readablePrivateBytes << L"\n";
    report << L"Listed readable private regions: "
        << inventory.readablePrivateRegions.size() << L"\n";
    report << L"Filtered smaller readable private regions: "
        << inventory.filteredReadablePrivateRegionCount << L"\n";
    report << L"Minimum listed size: "
        << tetris::kMinimumListedPrivateRegionSize << L" bytes\n";
    for (const auto& region : inventory.readablePrivateRegions) {
        report << L"Base: 0x" << std::hex << region.base
            << L" | AllocationBase: 0x" << region.allocationBase
            << L" | Size: 0x" << region.size
            << L" | Protect: " << ProtectionLabel(region.protection)
            << std::dec << L"\n";
    }
    if (!inventory.memoryMapError.empty())
        report << L"Memory map warning: " << inventory.memoryMapError << L"\n";
    report << L"\nThis report contains process metadata and memory-region layout only.\n";
    report << L"No raw game memory was read or modified.\n";

    std::ofstream output(std::filesystem::path(path), std::ios::binary);
    if (!output) return false;
    output << "\xEF\xBB\xBF" << ToUtf8(report.str());
    return static_cast<bool>(output);
}


bool SelectedProcessId(AppState* state, DWORD& processId) {
    processId = 0;
    const LRESULT selection = SendMessageW(state->windowList, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) return false;
    HWND target = reinterpret_cast<HWND>(SendMessageW(
        state->windowList, CB_GETITEMDATA, static_cast<WPARAM>(selection), 0));
    if (!target || !IsWindow(target)) return false;
    GetWindowThreadProcessId(target, &processId);
    return processId != 0;
}

bool ChooseFilePath(HWND owner, bool save, const wchar_t* filter,
                    const wchar_t* defaultExtension, const wchar_t* suggestedName,
                    std::wstring& path) {
    std::vector<wchar_t> filename(32768, L'\0');
    if (suggestedName) {
        const auto count = (std::min)(std::wcslen(suggestedName), filename.size() - 1);
        std::copy_n(suggestedName, count, filename.data());
    }
    const std::wstring initialDirectory = ExecutableFolder();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.lpstrDefExt = defaultExtension;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY |
        (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!accepted) return false;
    path.assign(filename.data());
    return true;
}

void CaptureSnapshot(AppState* state) {
    DWORD processId = 0;
    if (!SelectedProcessId(state, processId)) {
        SetWindowTextW(state->status, L"请先刷新并选择目标游戏窗口。");
        return;
    }

    static const wchar_t filter[] =
        L"TetrisPilotNext memory snapshot (*.tpsnap)\0*.tpsnap\0All files (*.*)\0*.*\0";
    std::wstring path;
    if (!ChooseFilePath(state->window, true, filter, L"tpsnap",
        L"TetrisPilotNext-snapshot.tpsnap", path)) return;

    SetWindowTextW(state->status,
        L"正在采集可读私有内存及游戏目录内模块的可读写数据页。游戏进程不会暂停，请尽量在状态稳定时采集……");
    UpdateWindow(state->status);

    std::wstring error;
    if (!tetris::win32::CaptureReadableGameMemorySnapshot(processId, path, error)) {
        const std::wstring message = L"快照采集失败：" + error;
        SetWindowTextW(state->status, message.c_str());
        return;
    }

    const std::wstring message =
        L"快照采集完成。请使用不同文件名保存不同游戏状态：" + path;
    SetWindowTextW(state->status, message.c_str());
}

void CompareSnapshots(AppState* state) {
    static const wchar_t filter[] =
        L"TetrisPilotNext memory snapshot (*.tpsnap)\0*.tpsnap\0All files (*.*)\0*.*\0";
    static const wchar_t reportFilter[] =
        L"Text report (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    std::wstring beforePath;
    std::wstring afterPath;
    std::wstring reportPath;

    if (!ChooseFilePath(state->window, false, filter, L"tpsnap", nullptr, beforePath))
        return;
    if (!ChooseFilePath(state->window, false, filter, L"tpsnap", nullptr, afterPath))
        return;
    if (!ChooseFilePath(state->window, true, reportFilter, L"txt",
        L"TetrisPilotNext-memory-diff.txt", reportPath)) return;

    SetWindowTextW(state->status, L"正在比较两份快照……");
    UpdateWindow(state->status);

    std::wstring error;
    if (!tetris::win32::CompareSnapshotFiles(
        beforePath, afterPath, reportPath, error)) {
        const std::wstring message = L"快照比较失败：" + error;
        SetWindowTextW(state->status, message.c_str());
        return;
    }

    const std::wstring message = L"差分报告已生成：" + reportPath;
    SetWindowTextW(state->status, message.c_str());
    ShellExecuteW(state->window, L"open", reportPath.c_str(),
        nullptr, nullptr, SW_SHOWNORMAL);
}

void InspectSelected(AppState* state) {
    const LRESULT selection = SendMessageW(state->windowList, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR) {
        SetWindowTextW(state->status, L"请先刷新并选择游戏窗口。");
        return;
    }
    HWND target = reinterpret_cast<HWND>(
        SendMessageW(state->windowList, CB_GETITEMDATA,
            static_cast<WPARAM>(selection), 0));
    if (!target || !IsWindow(target)) {
        SetWindowTextW(state->status, L"所选窗口已失效，请重新刷新。");
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    tetris::win32::ProcessInventory inventory;
    std::wstring error;
    if (!tetris::win32::InspectProcessMetadata(pid, inventory, error)) {
        const std::wstring message = L"读取失败：" + error;
        SetWindowTextW(state->status, message.c_str());
        return;
    }

    state->reportPath = ExecutableFolder() + L"\\TetrisPilotNext-client-report.txt";
    if (!WriteReport(inventory, state->reportPath)) {
        SetWindowTextW(state->status, L"无法写入报告文件。");
        return;
    }

    const auto fingerprintStatus = tetris::CheckTargetClientFingerprint(
        inventory.executableSize, inventory.executableSha256,
        inventory.architecture);
    const std::wstring message =
        L"完成：PID " + std::to_wstring(pid) +
        L"；SHA-256 " + (inventory.executableSha256.empty() ? L"计算失败" : L"已计算") +
        L"；模块 " + std::to_wstring(inventory.modules.size()) +
        L"；映像区域 " + std::to_wstring(inventory.imageRegions.size()) +
        L"；私有区域 " + std::to_wstring(inventory.privateRegionCount) +
        L"；候选区域 " + std::to_wstring(inventory.readablePrivateRegions.size()) +
        L"；" + FingerprintStatusText(fingerprintStatus) + L"。报告已导出。";
    SetWindowTextW(state->status, message.c_str());
    EnableWindow(state->openReportButton, TRUE);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        state = static_cast<AppState*>(create->lpCreateParams);
        state->window = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) return DefWindowProcW(hwnd, message, wParam, lParam);

    switch (message) {
    case WM_CREATE: {
        HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto make = [&](const wchar_t* cls, const wchar_t* label, DWORD style,
                        int x, int y, int width, int height, int id) {
            HWND control = CreateWindowExW(0, cls, label,
                WS_CHILD | WS_VISIBLE | style, x, y, width, height,
                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            if (control) SendMessageW(control, WM_SETFONT,
                reinterpret_cast<WPARAM>(font), TRUE);
            return control;
        };
        make(L"STATIC", L"目标游戏窗口", SS_LEFT, 18, 20, 120, 24, 0);
        state->windowList = make(L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 140, 16, 525, 240, kWindowList);
        make(L"BUTTON", L"刷新", BS_PUSHBUTTON, 680, 16, 95, 28, kRefresh);
        make(L"BUTTON", L"导出进程报告",
            BS_PUSHBUTTON, 18, 60, 165, 34, kInspect);
        make(L"BUTTON", L"采集内存快照",
            BS_PUSHBUTTON, 195, 60, 165, 34, kCaptureSnapshot);
        make(L"BUTTON", L"比较两份快照",
            BS_PUSHBUTTON, 372, 60, 165, 34, kCompareSnapshots);
        state->openReportButton = make(L"BUTTON", L"打开进程报告",
            BS_PUSHBUTTON, 549, 60, 145, 34, kOpenReport);
        EnableWindow(state->openReportButton, FALSE);
        state->status = make(L"STATIC",
            L"报告用于检查进程布局；快照用于采集和比较游戏状态变化。",
            SS_LEFT, 18, 112, 930, 70, kStatus);
        RefreshWindows(state);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kRefresh: RefreshWindows(state); return 0;
        case kInspect: InspectSelected(state); return 0;
        case kCaptureSnapshot: CaptureSnapshot(state); return 0;
        case kCompareSnapshots: CompareSnapshots(state); return 0;
        case kOpenReport:
            if (!state->reportPath.empty())
                ShellExecuteW(hwnd, L"open", state->reportPath.c_str(),
                    nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = POINT{1000, 250};
        return 0;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"TetrisPilotNextInspector";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&wc)) return 1;

    AppState state;
    HWND window = CreateWindowExW(0, wc.lpszClassName,
        L"Tetris Pilot Next - Read-only Snapshot Inspector",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 250,
        nullptr, nullptr, instance, &state);
    if (!window) return 1;

    ShowWindow(window, show);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
