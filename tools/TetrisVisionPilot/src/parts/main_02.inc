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
char PieceFromPalette(const Rgb& c) {
    struct PaletteEntry { char piece; Rgb color; };
    static constexpr PaletteEntry palette[] = {
        {'S',{190,248,150}}, // green
        {'J',{79,114,243}},  // blue
        {'I',{71,169,227}},  // cyan
        {'T',{183,64,222}},  // purple
        {'O',{232,201,6}},   // yellow
        {'L',{236,167,36}},  // orange
        {'Z',{253,111,111}}  // red
    };
    int best = (std::numeric_limits<int>::max)();
    char result = 0;
    for (const auto& p : palette) {
        int d = ColorDistance(c, p.color);
        if (d < best) { best = d; result = p.piece; }
    }
    return best <= 125 ? result : 0;
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
            if (visibleExpected == observed) {
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
