
std::string ShapeKey(const std::vector<Cell>& cells) {
    std::string key;
    for (const auto& c : Normalize(cells)) {
        key += std::to_string(c.x); key += ','; key += std::to_string(c.y); key += ';';
    }
    return key;
}

std::unordered_map<char, std::vector<std::vector<Cell>>> g_rotations;
std::unordered_map<std::string, std::pair<char, int>> g_shapeLookup;

std::vector<Cell> Rotate(const std::vector<Cell>& shape) {
    // Screen coordinates have +Y downward. (-y, x) is clockwise and matches
    // the application's configured VK_UP rotation direction.
    std::vector<Cell> out;
    out.reserve(shape.size());
    for (const auto& c : shape) out.push_back({ -c.y, c.x });
    return Normalize(std::move(out));
}

void InitShapes() {
    const std::vector<std::pair<char, std::vector<Cell>>> bases = {
        {'I', {{0,1},{1,1},{2,1},{3,1}}},
        {'O', {{0,0},{1,0},{0,1},{1,1}}},
        {'T', {{1,0},{0,1},{1,1},{2,1}}},
        {'S', {{1,0},{2,0},{0,1},{1,1}}},
        {'Z', {{0,0},{1,0},{1,1},{2,1}}},
        {'J', {{0,0},{0,1},{1,1},{2,1}}},
        {'L', {{2,0},{0,1},{1,1},{2,1}}}
    };
    for (const auto& entry : bases) {
        char name = entry.first;
        std::vector<Cell> current = Normalize(entry.second);
        auto& rotations = g_rotations[name];
        for (int i = 0; i < 4; ++i) {
            std::string key = ShapeKey(current);
            bool duplicate = false;
            for (const auto& old : rotations) if (ShapeKey(old) == key) duplicate = true;
            if (!duplicate) {
                int index = static_cast<int>(rotations.size());
                rotations.push_back(current);
                g_shapeLookup[key] = {name, index};
            }
            current = Rotate(current);
        }
    }
}

struct Candidate {
    char name{};
    int rotation{};
    std::vector<Cell> cells;
    int minX{}, minY{};
    double centerX{}, centerY{};
};

struct VisionResult {
    bool occupied[ROWS][COLS]{};
    Rgb colors[ROWS][COLS]{};
    Candidate active{};
    std::vector<Candidate> components; // All isolated tetromino-shaped components.
    Candidate ghost{}; // Hollow landing projection.
    bool hasActive{};
    bool hasGhost{};
    int ghostConfidence{};
    Rgb background{};
    int occupiedCount{};
    int activeStableSamples{};
    int usedThreshold{};
};

struct CapturedImage {
    int width{}, height{};
    std::vector<Pixel> pixels;
};

bool ResolveCaptureRect(const Rect& source, Rect& resolved, std::wstring& error) {
    resolved = source;
    resolved.clientRelative = false;
    if (!source.clientRelative) return true;

    HWND target = g_captureTarget;
    if (!target || !IsWindow(target)) {
        error = L"区域使用窗口相对坐标，但目标游戏窗口尚未绑定。";
        return false;
    }
    RECT client{};
    POINT origin{0, 0};
    if (!GetClientRect(target, &client) || !ClientToScreen(target, &origin)) {
        error = L"无法读取目标游戏窗口客户区坐标。";
        return false;
    }
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;
    if (source.left < 0 || source.top < 0 ||
        source.left + source.width > clientWidth ||
        source.top + source.height > clientHeight) {
        error = Format(L"窗口相对区域超出客户区：区域(%d,%d %d×%d)，客户区(%d×%d)。",
            source.left, source.top, source.width, source.height, clientWidth, clientHeight);
        return false;
    }
    resolved.left = origin.x + source.left;
    resolved.top = origin.y + source.top;
    resolved.width = source.width;
    resolved.height = source.height;
    return true;
}

bool CaptureScreenRect(const Rect& rect, CapturedImage& out, std::wstring& error) {
    // Generic screen capture is shared by the board, NEXT and SCORE regions.
    // Window-relative regions are resolved on every capture so moving/resizing the
    // game window never leaves stale absolute screen coordinates behind.
    Rect captureRect{};
    if (!ResolveCaptureRect(rect, captureRect, error)) return false;
    if (captureRect.width < 8 || captureRect.height < 8 ||
        captureRect.width > 10000 || captureRect.height > 10000) {
        error = Format(L"截图区域尺寸无效：%d × %d px（允许范围 8–10000 px）。",
            captureRect.width, captureRect.height);
        return false;
    }
    // When an HWND WGC session is active, never silently mix in desktop pixels:
    // an occluding window would contaminate the board/OCR image.
    if (IsWindowCaptureConfigured()) {
        return TryCaptureWindowRect(captureRect, out, error);
    }
    HDC screen = GetDC(nullptr);
    if (!screen) { error = L"无法访问桌面画面。"; return false; }
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = captureRect.width;
    info.bmiHeader.biHeight = -captureRect.height; // top-down DIB
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memory || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        error = L"创建屏幕截图缓冲区失败。";
        return false;
    }
    HGDIOBJ old = SelectObject(memory, bitmap);
    BOOL copied = BitBlt(memory, 0, 0, captureRect.width, captureRect.height, screen,
                         captureRect.left, captureRect.top, SRCCOPY | CAPTUREBLT);
    if (copied) {
        out.width = captureRect.width;
        out.height = captureRect.height;
        size_t count = static_cast<size_t>(captureRect.width) * captureRect.height;
