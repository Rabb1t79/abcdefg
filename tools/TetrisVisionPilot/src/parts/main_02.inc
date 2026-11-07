        out.pixels.resize(count);
        std::memcpy(out.pixels.data(), bits, count * sizeof(Pixel));
    }
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    if (!copied) { error = L"截取棋盘画面失败。请确认游戏不是独占全屏。"; return false; }
    return true;
}


int Median(std::vector<int>& v) {
    if (v.empty()) return 0;
    auto mid = v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2);
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

int ColorDistance(const Rgb& a, const Rgb& b) {
    int dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
    return static_cast<int>(std::sqrt(static_cast<double>(dr*dr + dg*dg + db*db)));
}

// Tetris Online palette sampled from the user's screenshot. The palette also lets us
// recover a tetromino when its spawn cell is clipped above the visible playfield.
char PieceFromPalette(const Rgb& c, int maximumDistance = 125) {
    struct PaletteEntry { char piece; Rgb color; };
    static constexpr PaletteEntry palette[] = {
        {'S',{142,219,35}},  // Tetris Online green
        {'J',{79,114,243}},   // blue
        {'I',{117,223,255}},  // cyan
        {'T',{192,91,225}},   // purple
        {'O',{235,204,6}},    // yellow
        {'L',{255,174,0}},    // orange
        {'Z',{253,116,116}}   // red
    };
    int best = (std::numeric_limits<int>::max)();
    char result = 0;
    for (const auto& p : palette) {
        int d = ColorDistance(c, p.color);
        if (d < best) { best = d; result = p.piece; }
    }
    return best <= maximumDistance ? result : 0;
}

bool InferTopClippedPiece(const std::vector<Cell>& visible, char piece,
                          int& rotationOut, std::vector<Cell>& fullCellsOut) {
    auto it = g_rotations.find(piece);
    if (it == g_rotations.end() || visible.size() != 3) return false;
    std::vector<Cell> observed = visible;
    std::sort(observed.begin(), observed.end(), [](const Cell& a, const Cell& b) {
        return a.y == b.y ? a.x < b.x : a.y < b.y;
    });
    int matches = 0;
    for (int rotation = 0; rotation < static_cast<int>(it->second.size()); ++rotation) {
        const auto& shape = it->second[rotation];
        // Tetris Online hides the fourth spawn mino above row zero. Test all legal
        // horizontal origins while fixing the shape's top row at y=-1.
        for (int originX = -2; originX < COLS + 2; ++originX) {
            std::vector<Cell> visibleExpected;
            std::vector<Cell> full;
            int hidden = 0;
            bool valid = true;
            for (const auto& c : shape) {
                Cell p{originX + c.x, -1 + c.y};
                if (p.x < 0 || p.x >= COLS || p.y >= ROWS) { valid = false; break; }
                full.push_back(p);
                if (p.y < 0) ++hidden;
                else visibleExpected.push_back(p);
            }
            if (!valid || hidden != 1 || visibleExpected.size() != visible.size()) continue;
            std::sort(visibleExpected.begin(), visibleExpected.end(), [](const Cell& a, const Cell& b) {
                return a.y == b.y ? a.x < b.x : a.y < b.y;
            });
            const bool sameVisibleCells = visibleExpected.size() == observed.size() &&
                std::equal(visibleExpected.begin(), visibleExpected.end(), observed.begin(),
                    [](const Cell& a, const Cell& b) { return a.x == b.x && a.y == b.y; });
            if (sameVisibleCells) {
                ++matches;
                rotationOut = rotation;
                fullCellsOut = std::move(full);
            }
        }
    }
    return matches == 1;
}

VisionResult Analyze(const CapturedImage& image, int threshold, int spawnRows = 7) {
    VisionResult result{};
    const double cw = static_cast<double>(image.width) / COLS;
    const double ch = static_cast<double>(image.height) / ROWS;
    int buckets[4096]{};
    int bucketIds[ROWS][COLS]{};
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) {
        int x0 = (std::max)(0, static_cast<int>((x + 0.27) * cw));
        int x1 = (std::min)(image.width, static_cast<int>((x + 0.73) * cw));
        int y0 = (std::max)(0, static_cast<int>((y + 0.27) * ch));
        int y1 = (std::min)(image.height, static_cast<int>((y + 0.73) * ch));
        std::vector<int> rs, gs, bs;
        for (int py = y0; py < y1; py += 2) for (int px = x0; px < x1; px += 2) {
            const Pixel& p = image.pixels[static_cast<size_t>(py) * image.width + px];
            rs.push_back(p.r); gs.push_back(p.g); bs.push_back(p.b);
        }
        Rgb color{Median(rs), Median(gs), Median(bs)};
        result.colors[y][x] = color;
        int bucket = ((color.r >> 4) << 8) | ((color.g >> 4) << 4) | (color.b >> 4);
        bucketIds[y][x] = bucket;
        ++buckets[bucket];
    }
    int commonBucket = 0;
    for (int i = 1; i < 4096; ++i) if (buckets[i] > buckets[commonBucket]) commonBucket = i;
    std::vector<int> baseR, baseG, baseB;
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) {
        if (bucketIds[y][x] == commonBucket) {
            baseR.push_back(result.colors[y][x].r);
            baseG.push_back(result.colors[y][x].g);
            baseB.push_back(result.colors[y][x].b);
        }
    }
    result.background = {Median(baseR), Median(baseG), Median(baseB)};
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) {
        result.occupied[y][x] = ColorDistance(result.colors[y][x], result.background) > threshold;
        if (result.occupied[y][x]) ++result.occupiedCount;
    }

    bool visited[ROWS][COLS]{};
    double bestRank = 1e9;
    const int dx[4] = {1,-1,0,0};
    const int dy[4] = {0,0,1,-1};
    for (int sy = 0; sy < ROWS; ++sy) for (int sx = 0; sx < COLS; ++sx) {
        if (!result.occupied[sy][sx] || visited[sy][sx]) continue;
        const Rgb seed = result.colors[sy][sx];
        std::vector<Cell> component;
        std::vector<Cell> stack{{sx,sy}};
        visited[sy][sx] = true;
        while (!stack.empty()) {
            Cell c = stack.back(); stack.pop_back();
            component.push_back(c);
            for (int k = 0; k < 4; ++k) {
                int nx = c.x + dx[k], ny = c.y + dy[k];
                if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS || visited[ny][nx] || !result.occupied[ny][nx]) continue;
                if (ColorDistance(result.colors[ny][nx], seed) <= 92) {
                    visited[ny][nx] = true;
                    stack.push_back({nx,ny});
                }
            }
        }

        char pieceName = 0;
        int rotation = 0;
        std::vector<Cell> fullCells;
        if (component.size() == 4) {
            auto shape = g_shapeLookup.find(ShapeKey(component));
            if (shape != g_shapeLookup.end()) {
                pieceName = shape->second.first;
                rotation = shape->second.second;
                fullCells = component;
            }
        } else if (component.size() == 3) {
            int minVisibleY = ROWS;
            for (const auto& c : component) minVisibleY = (std::min)(minVisibleY, c.y);
            if (minVisibleY <= 2) {
                pieceName = PieceFromPalette(seed);
                if (!pieceName || !InferTopClippedPiece(component, pieceName, rotation, fullCells)) pieceName = 0;
            }
        }
        if (!pieceName || fullCells.size() != 4) continue;

        int minX = COLS, minY = ROWS;
        double sumX = 0, sumY = 0;
        for (const auto& c : fullCells) {
            minX = (std::min)(minX,c.x);
            minY = (std::min)(minY,c.y);
            sumX += c.x; sumY += c.y;
        }
        if (minY > spawnRows) continue;
        double centerX = sumX / 4.0, centerY = sumY / 4.0;
        double rank = minY + std::abs(centerX - 4.5) * 0.30;
        if (rank < bestRank) {
            bestRank = rank;
            result.hasActive = true;
            result.active.name = pieceName;
            result.active.rotation = rotation;
            result.active.cells = std::move(fullCells);
            result.active.minX = minX;
            result.active.minY = minY;
            result.active.centerX = centerX;
            result.active.centerY = centerY;
        }
    }
    return result;
}


struct NextQueueResult { std::vector<char> pieces; std::vector<int> confidence; };
bool ReadNextQueue(const Rect& rect, int slotCount, NextQueueResult& out, std::wstring& error) {
    out = {};
    if (slotCount < 1 || slotCount > 10 || rect.width < 30 || rect.height < slotCount * 26) {
        error = L"NEXT 区域尺寸或预览块数量无效。请只框选 NEXT 竖列中的预览卡片。";
        return false;
    }
    CapturedImage image;
    if (!CaptureScreenRect(rect, image, error)) return false;
    constexpr int paletteCount = 7;
    const double pitch = static_cast<double>(image.height) / slotCount;
    static constexpr char names[] = {'S','J','I','T','O','L','Z'};
    for (int slot = 0; slot < slotCount; ++slot) {
        const int y0 = (std::max)(0, static_cast<int>(slot * pitch + pitch * 0.12));
        const int y1 = (std::min)(image.height, static_cast<int>((slot + 1) * pitch - pitch * 0.12));
        const int x0 = (std::max)(0, static_cast<int>(image.width * 0.06));
        const int x1 = (std::min)(image.width, static_cast<int>(image.width * 0.94));
        std::array<int, paletteCount> counts{};
        for (int py = y0; py < y1; py += 2) for (int px = x0; px < x1; px += 2) {
            const Pixel& pixel = image.pixels[static_cast<size_t>(py) * image.width + px];
            const char piece = PieceFromPalette({pixel.r, pixel.g, pixel.b}, 60);
            for (int i = 0; i < paletteCount; ++i) if (names[i] == piece) { ++counts[i]; break; }
        }
        const int bestIndex = static_cast<int>(std::distance(counts.begin(), std::max_element(counts.begin(), counts.end())));
        const int best = counts[bestIndex];
        int second = 0;
        for (int i = 0; i < paletteCount; ++i) if (i != bestIndex) second = (std::max)(second, counts[i]);
        if (best < 10 || (second > 0 && best < static_cast<int>(second * 1.05))) {
            error = Format(L"NEXT 第 %d 格无法可靠识别（颜色样本 %d）。请调整框选区域或预览块数量。", slot + 1, best);
            return false;
        }
        out.pieces.push_back(names[bestIndex]);
        out.confidence.push_back(best);
    }
    return true;
}
std::wstring FormatNextQueue(const std::vector<char>& pieces) {
    std::wstring result;
    for (char piece : pieces) {
        if (!result.empty()) result += L" → ";
        result += static_cast<wchar_t>(piece);
    }
    return result;
}
