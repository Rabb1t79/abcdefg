#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// Establish the legacy global Windows::Foundation namespace before the
// Windows.Graphics interop headers extend the global Windows namespace.
#include <roapi.h>
#include <mmsystem.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <condition_variable>
#include <chrono>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <windows.foundation.h>
#include <unknwn.h>
// The Direct3D11 interop header references the projected
// Windows::Foundation namespace, so make the projection visible first.
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <new>
#include <stdio.h>
#include <stdexcept>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <limits>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cctype>
#include <tesseract/baseapi.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")

namespace {
constexpr int COLS = 10;
constexpr int ROWS = 20;
constexpr UINT WM_APP_LOG = WM_APP + 1;
constexpr UINT WM_APP_REGION = WM_APP + 2;
constexpr UINT WM_APP_FINISHED = WM_APP + 3;
constexpr UINT WM_APP_TRAIN_STATS = WM_APP + 4;
constexpr UINT WM_APP_NEXT_REGION = WM_APP + 5;
constexpr UINT WM_APP_SCORE_REGION = WM_APP + 7;
constexpr UINT WM_APP_AGAIN_REGION = WM_APP + 8;
constexpr int ID_WINDOW = 1001;
constexpr int ID_REFRESH = 1002;
constexpr int ID_SELECT = 1003;
constexpr int ID_TEST = 1004;
constexpr int ID_START = 1005;
constexpr int ID_STOP = 1006;
constexpr int ID_THRESHOLD = 1007;
constexpr int ID_PREVIEW = 1008;
constexpr int ID_LOG = 1009;
constexpr int ID_REGION = 1010;
constexpr int ID_STATUS = 1011;
constexpr int ID_HELP = 1012;
constexpr int ID_SCORE = 1013;
constexpr int ID_SUBMIT_SCORE = 1014;
constexpr int ID_LEARNING_RATE = 1015;
constexpr int ID_EPSILON = 1016;
constexpr int ID_TEMPERATURE = 1017;
constexpr int ID_RESET_MODEL = 1018;
constexpr int ID_TRAIN_STATS = 1019;
constexpr int ID_SELECT_NEXT = 1020;
constexpr int ID_NEXT_COUNT = 1021;
constexpr int ID_LOOKAHEAD = 1022;
constexpr int ID_KEY_HOLD = 1023;
constexpr int ID_KEY_GAP = 1024;
constexpr int ID_DROP_WAIT = 1025;
constexpr int ID_AUTO_RETRY = 1026;
constexpr int ID_SELECT_SCORE = 1027;
constexpr int ID_TEST_SCORE = 1028;
constexpr int ID_SELECT_AGAIN = 1029;
constexpr int ID_MODE = 1030;
constexpr int ID_AUTO_LOCATE = 1031;
constexpr int ID_NEW_SESSION = 1032;
constexpr int ID_AUTO_RETRY_BUTTON = 1033;
constexpr int ID_NEW_SESSION = 1032;
constexpr int HOTKEY_STOP = 1;
constexpr UINT WM_APP_SCORE_RECORDED = WM_APP + 6;

struct Cell { int x{}, y{}; };
struct Rgb { int r{}, g{}, b{}; };
struct Pixel { uint8_t b{}, g{}, r{}, a{}; };
struct Rect { int left{}, top{}, width{}, height{}; bool clientRelative{}; int referenceClientWidth{}, referenceClientHeight{}; };

struct CapturedImage;
bool IsWindowCaptureConfigured();
bool TryCaptureWindowRect(const Rect& rect, CapturedImage& out, std::wstring& error);
bool EnsureWindowCapture(HWND target, std::wstring& error);
void StopWindowCapture();

static HINSTANCE g_instance{};
static HWND g_mainWindow{};
static HWND g_captureTarget{};
static int g_virtualLeft{}, g_virtualTop{};
static POINT g_selectStart{};
static POINT g_selectCurrent{};
static bool g_selectDragging{};
static bool g_selectNextRegion{};
static bool g_selectScoreRegion{};
static bool g_selectAgainRegion{};

std::wstring Format(const wchar_t* fmt, ...) {
    wchar_t buffer[1024]{};
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buffer, _countof(buffer), _TRUNCATE, fmt, args);
    va_end(args);
    return buffer;
}

void PostLog(HWND hwnd, const std::wstring& message) {
    if (!IsWindow(hwnd)) return;
    auto* copy = new (std::nothrow) std::wstring(message);
    if (copy && !PostMessageW(hwnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(copy))) delete copy;
}

std::vector<Cell> Normalize(std::vector<Cell> cells) {
    if (cells.empty()) return cells;
    int minX = cells.front().x, minY = cells.front().y;
    for (const auto& c : cells) { minX = (std::min)(minX, c.x); minY = (std::min)(minY, c.y); }
    for (auto& c : cells) { c.x -= minX; c.y -= minY; }
    std::sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) {
        return a.y == b.y ? a.x < b.x : a.y < b.y;
    });
    return cells;
}
