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

char PieceFromGhostColor(const Rgb& color) {
    const double r = color.r / 255.0;
    const double g = color.g / 255.0;
    const double b = color.b / 255.0;
    const double maximum = (std::max)({r, g, b});
    const double minimum = (std::min)({r, g, b});
    const double delta = maximum - minimum;
    if (maximum < 0.12 || delta < 0.055) return 0;
    if (delta / maximum < 0.20) return 0;
    double hue = 0.0;
    if (maximum == r) hue = 60.0 * std::fmod((g - b) / delta, 6.0);
    else if (maximum == g) hue = 60.0 * ((b - r) / delta + 2.0);
    else hue = 60.0 * ((r - g) / delta + 4.0);
    if (hue < 0.0) hue += 360.0;
    if (hue < 15.0 || hue >= 345.0) return 'Z';
    if (hue >= 20.0 && hue < 43.0) return 'L';
    if (hue >= 43.0 && hue < 75.0) return 'O';
    if (hue >= 75.0 && hue < 160.0) return 'S';
    if (hue >= 160.0 && hue < 205.0) return 'I';
    if (hue >= 205.0 && hue < 260.0) return 'J';
    if (hue >= 260.0 && hue < 330.0) return 'T';
    return 0;
}

int GhostPieceIndex(char piece) {
    static constexpr char names[] = {'S','J','I','T','O','L','Z'};
    for (int i = 0; i < 7; ++i) if (names[i] == piece) return i;
    return -1;
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

    // Estimate per-frame background noise from cells that share the dominant
    // background bucket. The user's threshold remains a sensitivity control,
    // but it can no longer fall below the measured noise floor.
    std::vector<int> backgroundNoise;
    backgroundNoise.reserve(baseR.size());
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) {
        if (bucketIds[y][x] == commonBucket) {
            backgroundNoise.push_back(ColorDistance(result.colors[y][x], result.background));
        }
    }
    int noiseP90 = 0;
    if (!backgroundNoise.empty()) {
        std::sort(backgroundNoise.begin(), backgroundNoise.end());
        const size_t p90 = (std::min)(backgroundNoise.size() - 1,
            (backgroundNoise.size() * 9) / 10);
        noiseP90 = backgroundNoise[p90];
    }
    const int noiseFloor = (std::max)(14, noiseP90 * 2 + 8);
    const int effectiveThreshold = (std::max)(noiseFloor,
        (std::max)(10, (std::min)(160, threshold)));
    result.usedThreshold = effectiveThreshold;

    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) {
        result.occupied[y][x] =
            ColorDistance(result.colors[y][x], result.background) > effectiveThreshold;
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
        double centerX = sumX / 4.0, centerY = sumY / 4.0;
        Candidate detected;
        detected.name = pieceName;
        detected.rotation = rotation;
        detected.cells = fullCells;
        detected.minX = minX;
        detected.minY = minY;
        detected.centerX = centerX;
        detected.centerY = centerY;
        result.components.push_back(detected);

        if (minY > spawnRows) continue;
        double rank = minY + std::abs(centerX - 4.5) * 0.30;
        if (rank < bestRank) {
            bestRank = rank;
            result.hasActive = true;
            result.active = std::move(detected);
        }
    }

    // Scan the coloured inner borders of empty cells. The centre-based board
    // mask remains untouched, so hollow landing previews never become obstacles.
    char ghostCellPiece[ROWS][COLS]{};
    int ghostCellStrength[ROWS][COLS]{};
    for (int y = 0; y < ROWS; ++y) for (int x = 0; x < COLS; ++x) {
        if (result.occupied[y][x]) continue;
        const double left = x * cw, top = y * ch;
        const int px0 = (std::max)(0, static_cast<int>(left + cw * 0.06));
        const int px1 = (std::min)(image.width, static_cast<int>(left + cw * 0.94));
        const int py0 = (std::max)(0, static_cast<int>(top + ch * 0.06));
        const int py1 = (std::min)(image.height, static_cast<int>(top + ch * 0.94));
        int counts[7]{};
        for (int py = py0; py < py1; ++py) for (int px = px0; px < px1; ++px) {
            const double rx = (px + 0.5 - left) / cw;
            const double ry = (py + 0.5 - top) / ch;
            if (!(rx < 0.24 || rx > 0.76 || ry < 0.24 || ry > 0.76)) continue;
            const Pixel& pixel = image.pixels[static_cast<size_t>(py) * image.width + px];
            const char name = PieceFromGhostColor({pixel.r, pixel.g, pixel.b});
            const char names[] = {'S','J','I','T','O','L','Z'};
            for (int i = 0; i < 7; ++i) if (names[i] == name) { ++counts[i]; break; }
        }
        int bestIndex = -1, best = 0, second = 0;
        for (int i = 0; i < 7; ++i) {
            if (counts[i] > best) { second = best; best = counts[i]; bestIndex = i; }
            else second = (std::max)(second, counts[i]);
        }
        if (bestIndex >= 0 && best >= 5 && best >= second + 3 &&
            (second == 0 || best >= second * 1.30)) {
            static constexpr char names[] = {'S','J','I','T','O','L','Z'};
            ghostCellPiece[y][x] = names[bestIndex];
            ghostCellStrength[y][x] = best;
        }
    }

    bool ghostVisited[ROWS][COLS]{};
    int bestGhostStrength = 0;
    for (int sy = 0; sy < ROWS; ++sy) for (int sx = 0; sx < COLS; ++sx) {
        const char piece = ghostCellPiece[sy][sx];
        if (!piece || ghostVisited[sy][sx]) continue;
        std::vector<Cell> component;
        std::vector<Cell> stack{{sx, sy}};
        ghostVisited[sy][sx] = true;
        int strength = 0;
        const int dx[4] = {1, -1, 0, 0};
        const int dy[4] = {0, 0, 1, -1};
        while (!stack.empty()) {
            const Cell cell = stack.back();
            stack.pop_back();
            component.push_back(cell);
            strength += ghostCellStrength[cell.y][cell.x];
            for (int k = 0; k < 4; ++k) {
                const int nx = cell.x + dx[k], ny = cell.y + dy[k];
                if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS ||
                    ghostVisited[ny][nx] || ghostCellPiece[ny][nx] != piece) continue;
                ghostVisited[ny][nx] = true;
                stack.push_back({nx, ny});
            }
        }
        if (component.size() != 4 || strength <= bestGhostStrength) continue;
        const auto shape = g_shapeLookup.find(ShapeKey(component));
        if (shape == g_shapeLookup.end() || shape->second.first != piece) continue;
        Candidate ghost;
        ghost.name = piece;
        ghost.rotation = shape->second.second;
        ghost.cells = component;
        ghost.minX = COLS;
        ghost.minY = ROWS;
        double sumX = 0.0, sumY = 0.0;
        for (const Cell& cell : component) {
            ghost.minX = (std::min)(ghost.minX, cell.x);
            ghost.minY = (std::min)(ghost.minY, cell.y);
            sumX += cell.x;
            sumY += cell.y;
        }
        ghost.centerX = sumX / 4.0;
        ghost.centerY = sumY / 4.0;
        result.ghost = std::move(ghost);
        result.hasGhost = true;
        result.ghostConfidence = strength;
        bestGhostStrength = strength;
    }
    return result;
}


VisionResult AnalyzeStable(const Rect& rect, int threshold, std::wstring& error) {
    constexpr int kSamples = 3;
    constexpr int kSampleIntervalMs = 28;
    std::array<VisionResult, kSamples> samples{};
    for (int i = 0; i < kSamples; ++i) {
        CapturedImage image;
        if (!CaptureScreenRect(rect, image, error)) return {};
        samples[static_cast<size_t>(i)] = Analyze(image, threshold);
        if (i + 1 < kSamples) Sleep(kSampleIntervalMs);
    }

    // Confirm the ghost outline in at least two of three captured frames.
    const auto& latest = samples.back();
    Candidate stableGhost{};
    int stableGhostCount = 0;
    int stableGhostIndex = -1;
    for (int i = 0; i < kSamples; ++i) {
        if (!samples[static_cast<size_t>(i)].hasGhost) continue;
        const Candidate& candidate = samples[static_cast<size_t>(i)].ghost;
        int count = 0;
        int latestMatch = i;
        for (int j = 0; j < kSamples; ++j) {
            const auto& other = samples[static_cast<size_t>(j)];
            if (other.hasGhost &&
                other.ghost.name == candidate.name &&
                other.ghost.rotation == candidate.rotation &&
                other.ghost.minX == candidate.minX &&
                other.ghost.minY == candidate.minY) {
                ++count;
                latestMatch = (std::max)(latestMatch, j);
            }
        }
        if (count > stableGhostCount) {
            stableGhostCount = count;
            stableGhostIndex = latestMatch;
        }
    }
    const bool hasStableGhost = stableGhostCount >= 2 && stableGhostIndex >= 0;
    VisionResult stable = latest;
    stable.hasGhost = hasStableGhost;
    stable.ghostConfidence = hasStableGhost
        ? samples[static_cast<size_t>(stableGhostIndex)].ghostConfidence : 0;
    if (hasStableGhost) stable.ghost = samples[static_cast<size_t>(stableGhostIndex)].ghost;

    // The active piece normally moves downward between samples. A settled block
    // can have exactly the same four-cell shape, so identity alone is insufficient.
    int bestMotionScore = -1;
    Candidate movingCandidate{};
    int movingCount = 0;

    for (const Candidate& current : latest.components) {
        int matched = 0;
        int earliestY = current.minY;
        int horizontalMotion = 0;
        bool observedMotion = false;
        for (int sampleIndex = 0; sampleIndex < kSamples - 1; ++sampleIndex) {
            const auto& candidates = samples[static_cast<size_t>(sampleIndex)].components;
            const Candidate* closest = nullptr;
            int closestDistance = (std::numeric_limits<int>::max)();
            int closestDx = 0;
            int closestDy = 0;
            for (const Candidate& previous : candidates) {
                if (previous.name != current.name ||
                    previous.rotation != current.rotation) continue;

                // A moving piece changes X during a horizontal tap sequence.
                // Requiring exact minX made the tracker reject the very movement
                // it was supposed to verify. Match a small local trajectory instead.
                const int dx = std::abs(current.minX - previous.minX);
                const int dy = current.minY - previous.minY;
                if (dx > 4 || dy < 0 || dy > 8) continue;
                const int distance = dx * 2 + dy;
                if (distance < closestDistance) {
                    closestDistance = distance;
                    closest = &previous;
                    closestDx = dx;
                    closestDy = dy;
                }
            }
            if (closest) {
                ++matched;
                earliestY = (std::min)(earliestY, closest->minY);
                horizontalMotion = (std::max)(horizontalMotion, closestDx);
                if (closestDx > 0 || closestDy > 0) observedMotion = true;
            }
        }

        if (matched < 1 || !observedMotion) continue;
        const int verticalMotion = current.minY - earliestY;
        const int motionScore = verticalMotion * 10 + horizontalMotion * 24;
        const int score = matched * 100 + motionScore - std::abs(current.minX - 4);
        if (score > bestMotionScore) {
            bestMotionScore = score;
            movingCandidate = current;
            movingCount = matched + 1;
        }
    }

    if (bestMotionScore >= 0) {
        VisionResult result = stable;
        if (hasStableGhost &&
            (movingCandidate.name != stable.ghost.name ||
             movingCandidate.rotation != stable.ghost.rotation ||
             movingCandidate.minX != stable.ghost.minX)) {
            // On a high stack, a settled tetromino can look like the falling one.
            // A stable landing outline is an independent identity/column check.
            const Candidate* matching = nullptr;
            int matches = 0;
            for (const Candidate& candidate : latest.components) {
                if (candidate.name == stable.ghost.name &&
                    candidate.rotation == stable.ghost.rotation &&
                    candidate.minX == stable.ghost.minX &&
                    candidate.minY < ROWS) {
                    matching = &candidate;
                    ++matches;
                }
            }
            if (matches != 1 || !matching) {
                result.hasActive = false;
                result.activeStableSamples = 0;
                return result;
            }
            movingCandidate = *matching;
        }
        result.active = std::move(movingCandidate);
        result.hasActive = true;
        result.activeStableSamples = movingCount;
        return result;
    }

    // Stationary/spawn fallback is intentionally conservative: use it only when
    // exactly one tetromino-shaped component exists across the latest frame.
    // On a crowded upper board, static settled pieces must never be mistaken for
    // the falling piece merely because they lie inside the old spawn-row window.
    VisionResult result = stable;
    result.hasActive = false;
    result.activeStableSamples = 0;
    if (hasStableGhost) {
        const Candidate* match = nullptr;
        int matches = 0;
        for (const Candidate& candidate : latest.components) {
            if (candidate.name == stable.ghost.name &&
                candidate.rotation == stable.ghost.rotation &&
                candidate.minX == stable.ghost.minX &&
                candidate.minY < ROWS) {
                match = &candidate;
                ++matches;
            }
        }
        if (matches == 1 && match) {
            result.active = *match;
            result.hasActive = true;
            result.activeStableSamples = stableGhostCount;
        }
    }
    if (!result.hasActive && latest.components.size() == 1 && latest.hasActive &&
        latest.active.minY <= 2) {
        bool sameInEarlierSamples = true;
        for (int sampleIndex = 0; sampleIndex < kSamples - 1; ++sampleIndex) {
            bool found = false;
            for (const Candidate& candidate : samples[static_cast<size_t>(sampleIndex)].components) {
                if (candidate.name == latest.active.name &&
                    candidate.rotation == latest.active.rotation &&
                    candidate.minX == latest.active.minX &&
                    candidate.minY == latest.active.minY) {
                    found = true;
                    break;
                }
            }
            if (!found) sameInEarlierSamples = false;
        }
        if (sameInEarlierSamples) {
            result.active = latest.active;
            result.hasActive = true;
            result.activeStableSamples = kSamples;
        }
    }
    return result;
}

bool ReadPieceColumn(const VisionResult& vision, char piece, int rotation,
                     int& columnOut, bool& usedGhostOut) {
    usedGhostOut = false;
    if (vision.hasGhost && vision.ghost.name == piece &&
        vision.ghost.rotation == rotation) {
        columnOut = vision.ghost.minX;
        usedGhostOut = true;
        return true;
    }
    if (vision.hasActive && vision.active.name == piece &&
        vision.active.rotation == rotation) {
        columnOut = vision.active.minX;
        return true;
    }
    return false;
}

char PieceFromPreviewColor(const Rgb& color) {
    const double r = color.r / 255.0;
    const double g = color.g / 255.0;
    const double b = color.b / 255.0;
    const double maximum = (std::max)({r, g, b});
    const double minimum = (std::min)({r, g, b});
    const double delta = maximum - minimum;
    if (maximum < 0.22 || delta < 0.10) return 0;
    const double saturation = maximum <= 0.0 ? 0.0 : delta / maximum;
    if (saturation < 0.28) return 0;

    double hue = 0.0;
    if (delta > 0.0) {
        if (maximum == r) hue = 60.0 * std::fmod((g - b) / delta, 6.0);
        else if (maximum == g) hue = 60.0 * ((b - r) / delta + 2.0);
        else hue = 60.0 * ((r - g) / delta + 4.0);
    }
    if (hue < 0.0) hue += 360.0;

    // Tetris Online palette families; saturation/value filtering removes the
    // grey card frames, arrows, shadows and most specular white highlights.
    if (hue < 15.0 || hue >= 345.0) return 'Z';
    if (hue >= 20.0 && hue < 43.0) return 'L';
    if (hue >= 43.0 && hue < 75.0) return 'O';
    if (hue >= 75.0 && hue < 160.0) return 'S';
    if (hue >= 160.0 && hue < 205.0) return 'I';
    if (hue >= 205.0 && hue < 260.0) return 'J';
    if (hue >= 260.0 && hue < 330.0) return 'T';
    return 0;
}

struct NextQueueResult {
    std::vector<char> pieces;
    std::vector<int> confidence;
};

struct PreviewBand {
    int top{};
    int bottom{};
    int total{};
    int best{};
    int second{};
    char piece{};
};

bool ReadNextQueue(const Rect& rect, int slotCount, NextQueueResult& out, std::wstring& error) {
    out = {};
    if (slotCount < 1 || slotCount > 10 || rect.width < 30 || rect.height < slotCount * 18) {
        error = L"NEXT 区域尺寸或预览块数量无效。请只框选 NEXT 预览卡片列。";
        return false;
    }

    CapturedImage image;
    if (!CaptureScreenRect(rect, image, error)) return false;

    const int x0 = (std::max)(0, static_cast<int>(image.width * 0.06));
    const int x1 = (std::min)(image.width, static_cast<int>(image.width * 0.94));
    if (x1 - x0 < 20 || image.height < slotCount * 18) {
        error = L"NEXT 选择区域太窄或太矮。请把六个预览卡片完整框入。";
        return false;
    }

    // Detect actual colored-piece rows first. This adapts to card gaps and
    // different tetromino heights (the I piece is only one block tall).
    std::vector<int> rowCounts(static_cast<size_t>(image.height), 0);
    for (int y = 0; y < image.height; y += 2) {
        int count = 0;
        for (int x = x0; x < x1; x += 2) {
            const Pixel& px = image.pixels[static_cast<size_t>(y) * image.width + x];
            if (PieceFromPreviewColor({px.r, px.g, px.b})) ++count;
        }
        rowCounts[static_cast<size_t>(y)] = count;
    }

    std::vector<PreviewBand> bands;
    int activeStart = -1;
    int lastActive = -1;
    for (int y = 0; y < image.height; y += 2) {
        const bool active = rowCounts[static_cast<size_t>(y)] >= 2;
        if (active) {
            if (activeStart < 0) activeStart = y;
            lastActive = y;
        } else if (activeStart >= 0 && y - lastActive > 10) {
            PreviewBand band;
            band.top = activeStart;
            band.bottom = lastActive;
            bands.push_back(band);
            activeStart = -1;
            lastActive = -1;
        }
    }
    if (activeStart >= 0) {
        PreviewBand band;
        band.top = activeStart;
        band.bottom = lastActive;
        bands.push_back(band);
    }

    // Count hue classes over each band and discard one-row decorations or
    // border artefacts before matching the expected queue length.
    std::vector<PreviewBand> validBands;
    for (auto band : bands) {
        if (band.bottom - band.top + 1 < 6) continue;
        std::array<int, 7> counts{};
        static constexpr char names[] = {'S','J','I','T','O','L','Z'};
        for (int y = band.top; y <= band.bottom; y += 2) {
            for (int x = x0; x < x1; x += 2) {
                const Pixel& px = image.pixels[static_cast<size_t>(y) * image.width + x];
                const char piece = PieceFromPreviewColor({px.r, px.g, px.b});
                for (int i = 0; i < 7; ++i) if (names[i] == piece) { ++counts[i]; break; }
            }
        }
        const int bestIndex = static_cast<int>(std::distance(counts.begin(),
            std::max_element(counts.begin(), counts.end())));
        band.best = counts[bestIndex];
        band.second = 0;
        band.total = 0;
        for (int i = 0; i < 7; ++i) {
            band.total += counts[i];
            if (i != bestIndex) band.second = (std::max)(band.second, counts[i]);
        }
        band.piece = names[bestIndex];
        if (band.total >= 20 && band.best >= 12) validBands.push_back(band);
    }

    if (static_cast<int>(validBands.size()) < slotCount) {
        error = Format(L"只检测到 %d/%d 个 NEXT 方块色带。请把预览卡片完整框入，勿包含标题或裁掉最上/最下方卡片。",
            static_cast<int>(validBands.size()), slotCount);
        return false;
    }

    // If the rectangle also contains a coloured decoration, retain the
    // slotCount strongest bands, then restore their original top-to-bottom order.
    if (static_cast<int>(validBands.size()) > slotCount) {
        std::sort(validBands.begin(), validBands.end(), [](const PreviewBand& a, const PreviewBand& b) {
            return a.total > b.total;
        });
        validBands.resize(static_cast<size_t>(slotCount));
    }
    std::sort(validBands.begin(), validBands.end(), [](const PreviewBand& a, const PreviewBand& b) {
        return a.top < b.top;
    });

    for (size_t i = 0; i < validBands.size(); ++i) {
        const auto& band = validBands[i];
        if (band.best < static_cast<int>(band.second * 1.12)) {
            error = Format(L"NEXT 第 %d 格颜色仍有歧义（主色 %d，次色 %d）。请缩紧框选范围。",
                static_cast<int>(i + 1), band.best, band.second);
            out = {};
            return false;
        }
        out.pieces.push_back(band.piece);
        out.confidence.push_back(band.total > 0 ? (100 * band.best / band.total) : 0);
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

std::wstring FormatNextQueueConfidence(const NextQueueResult& queue) {
    std::wstring result;
    for (size_t i = 0; i < queue.pieces.size(); ++i) {
        if (!result.empty()) result += L" → ";
        const int confidence = i < queue.confidence.size() ? queue.confidence[i] : 0;
        result += Format(L"%c(%d%%)", queue.pieces[i], confidence);
    }
    return result;
}
