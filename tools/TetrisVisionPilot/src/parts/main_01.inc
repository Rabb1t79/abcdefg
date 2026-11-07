
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
    std::vector<Cell> out;
    out.reserve(shape.size());
    for (const auto& c : shape) out.push_back({ c.y, -c.x });
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
    bool hasActive{};
    Rgb background{};
    int occupiedCount{};
    int activeStableSamples{};
};

struct CapturedImage {
    int width{}, height{};
    std::vector<Pixel> pixels;
};

bool CaptureScreenRect(const Rect& rect, CapturedImage& out, std::wstring& error) {
    // Generic screen capture is shared by the board and the much smaller NEXT panel.
    // Validate only safe bitmap bounds here; the board-specific 10x20 minimum is checked
    // by TestRecognition before it calls Analyze().
    if (rect.width < 8 || rect.height < 8 || rect.width > 10000 || rect.height > 10000) {
        error = Format(L"截图区域尺寸无效：%d × %d px（允许范围 8–10000 px）。",
            rect.width, rect.height);
        return false;
    }
    HDC screen = GetDC(nullptr);
    if (!screen) { error = L"无法访问桌面画面。"; return false; }
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.width;
    info.bmiHeader.biHeight = -rect.height; // top-down DIB
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
    BOOL copied = BitBlt(memory, 0, 0, rect.width, rect.height, screen,
                         rect.left, rect.top, SRCCOPY | CAPTUREBLT);
    if (copied) {
        out.width = rect.width;
        out.height = rect.height;
        size_t count = static_cast<size_t>(rect.width) * rect.height;
