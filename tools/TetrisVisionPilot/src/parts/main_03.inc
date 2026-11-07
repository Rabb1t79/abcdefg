            result.active.centerX = centerX;
            result.active.centerY = centerY;
        }
    }
    return result;
}

using Board = std::array<std::array<int, COLS>, ROWS>;
Board BoardWithoutActive(const VisionResult& v) {
    Board b{};
    for (int y=0;y<ROWS;++y) for (int x=0;x<COLS;++x) b[y][x] = v.occupied[y][x] ? 1 : 0;
    if (v.hasActive) for (const auto& c : v.active.cells) b[c.y][c.x] = 0;
    return b;
}

bool Valid(const Board& board, const std::vector<Cell>& shape, int x, int y) {
    for (const auto& c : shape) {
        int px=x+c.x, py=y+c.y;
        if (px<0 || px>=COLS || py>=ROWS) return false;
        if (py>=0 && board[py][px]) return false;
    }
    return true;
}

bool PlaceAndClear(const Board& board, const std::vector<Cell>& shape, int x, Board& out, int& cleared) {
    int maxY=0; for (const auto& c : shape) maxY=(std::max)(maxY,c.y);
    int y=-maxY-1;
    while (Valid(board,shape,x,y+1)) ++y;
    if (y<0 || !Valid(board,shape,x,y)) return false;
    out=board;
    for (const auto& c : shape) {
        int py=y+c.y;
        if (py<0) return false;
        out[py][x+c.x]=1;
    }
    Board compact{};
    int write=ROWS-1;
    for (int read=ROWS-1;read>=0;--read) {
        bool full=true;
        for (int col=0;col<COLS;++col) if (!out[read][col]) { full=false; break; }
        if (!full) compact[write--]=out[read];
    }
    cleared=write+1;
    out=compact;
    return true;
}

constexpr size_t FEATURE_COUNT = 8;

struct Features {
    std::array<double, FEATURE_COUNT> value{};
};

struct Move { int rotation{}, x{}; double score{-1e30}; };
struct ActionCandidate {
    Move move{};
    Features features{};
    int linesCleared{};
};
struct TrainingExperience {
    std::array<double, FEATURE_COUNT> gradient{};
};
struct PolicyDecision {
    Move move{};
    TrainingExperience experience{};
    int predictedLines{};
};

Features ExtractFeatures(const Board& board, int linesCleared) {
    int heights[COLS]{};
    int holes = 0;
    int aggregate = 0;
    int bumpiness = 0;
    int maxHeight = 0;
    int wells = 0;
    int rowTransitions = 0;
    int colTransitions = 0;

    for (int x = 0; x < COLS; ++x) {
        int first = ROWS;
        for (int y = 0; y < ROWS; ++y) {
            if (board[y][x]) { first = y; break; }
        }
        heights[x] = ROWS - first;
        aggregate += heights[x];
        maxHeight = (std::max)(maxHeight, heights[x]);
        if (first < ROWS) {
            for (int y = first; y < ROWS; ++y) if (!board[y][x]) ++holes;
        }
    }
    for (int x = 0; x < COLS - 1; ++x) bumpiness += std::abs(heights[x] - heights[x + 1]);

    for (int x = 0; x < COLS; ++x) {
        int left = (x == 0) ? ROWS : heights[x - 1];
        int right = (x == COLS - 1) ? ROWS : heights[x + 1];
        int floor = (std::min)(left, right);
        if (floor > heights[x]) wells += floor - heights[x];
    }
    for (int y = 0; y < ROWS; ++y) {
        int previous = 1; // wall
