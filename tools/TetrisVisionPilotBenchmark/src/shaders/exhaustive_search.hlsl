#define COLS 10
#define ROWS 20
#define MASK ((1u << COLS) - 1u)
struct Node {
    uint rows[20];
    uint firstRows[20];
    int placed;
    int lines;
    int firstX;
    int firstRotation;
    int rotationTaps;
    int horizontalTaps;
    int firstLines;
    int multiClearEvents;
    int multiClearLines;
    uint valid;
    uint terminal;
};
struct Shape {
    int count;
    int maxX;
    int maxY;
    int pad;
    int cells[8];
};
struct Eval {
    int placed;
    int multiClearEvents;
    int multiClearLines;
    int lines;
    int holes;
    int maxHeight;
    int aggregateHeight;
    int bumpiness;
    int wells;
    int rowTransitions;
    int colTransitions;
    int keyTaps;
    int rotationTaps;
    int horizontalTaps;
    int firstX;
    int firstRotation;
};
cbuffer Params : register(b0) {
    uint gInputCount;
    uint gPieceId;
    uint gDepth;
    int gCurrentRotation;
    int gCurrentX;
    uint gPad0;
    uint gPad1;
    uint gPad2;
};
StructuredBuffer<Node> gInput : register(t0);
StructuredBuffer<Shape> gShapes : register(t1);
RWStructuredBuffer<Node> gOutput : register(u0);
RWStructuredBuffer<Node> gBest : register(u1);
groupshared uint gAnyChild;

bool Fits(Node parent, Shape shape, int x, int y) {
    [unroll] for (int c = 0; c < 4; ++c) {
        if (c >= shape.count) break;
        int px = x + shape.cells[c * 2];
        int py = y + shape.cells[c * 2 + 1];
        if (px < 0 || px >= COLS || py >= ROWS) return false;
        if (py >= 0 && (parent.rows[py] & (1u << px)) != 0) return false;
    }
    return true;
}

[numthreads(40, 1, 1)]
void ExpandCS(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID) {
    const uint parentIndex = groupId.x;
    const uint actionIndex = groupThreadId.x;
    if (actionIndex == 0) gAnyChild = 0;
    GroupMemoryBarrierWithGroupSync();

    Node parent = gInput[parentIndex];
    Node child;
    [unroll] for (int r = 0; r < ROWS; ++r) {
        child.rows[r] = 0;
        child.firstRows[r] = 0;
    }
    child.placed = 0;
    child.lines = 0;
    child.firstX = 0;
    child.firstRotation = 0;
    child.rotationTaps = 0;
    child.horizontalTaps = 0;
    child.firstLines = 0;
    child.multiClearEvents = 0;
    child.multiClearLines = 0;
    child.valid = 0;
    child.terminal = 0;

    if (parent.valid != 0) {
        if (parent.terminal != 0) {
            if (actionIndex == 0) child = parent;
        } else {
            const int rotation = (int)(actionIndex / COLS);
            const int x = (int)(actionIndex % COLS);
            Shape shape = gShapes[gPieceId * 4 + (uint)rotation];
            if (shape.count > 0 && x + shape.maxX < COLS) {
                int y = -shape.maxY - 1;
                int guard = 0;
                while (Fits(parent, shape, x, y + 1) && guard < 24) {
                    ++y;
                    ++guard;
                }
                bool legal = y >= 0 && Fits(parent, shape, x, y);
                if (legal) {
                    uint placedRows[20];
                    [unroll] for (int row = 0; row < ROWS; ++row) placedRows[row] = parent.rows[row];
                    [unroll] for (int cell = 0; cell < 4; ++cell) {
                        if (cell >= shape.count) break;
                        const int px = x + shape.cells[cell * 2];
                        const int py = y + shape.cells[cell * 2 + 1];
                        if (py < 0 || py >= ROWS || px < 0 || px >= COLS) legal = false;
                        else placedRows[py] |= (1u << px);
                    }
                    if (legal) {
                        uint compact[20];
                        [unroll] for (int row = 0; row < ROWS; ++row) compact[row] = 0;
                        int writeRow = ROWS - 1;
                        int cleared = 0;
                        for (int readRow = ROWS - 1; readRow >= 0; --readRow) {
                            if (placedRows[readRow] == MASK) ++cleared;
                            else compact[writeRow--] = placedRows[readRow];
                        }
                        [unroll] for (int row = 0; row < ROWS; ++row) {
                            child.rows[row] = compact[row];
                            child.firstRows[row] = gDepth == 0 ? compact[row] : parent.firstRows[row];
                        }
                        child.placed = parent.placed + 1;
                        child.lines = parent.lines + cleared;
                        child.firstX = gDepth == 0 ? x : parent.firstX;
                        child.firstRotation = gDepth == 0 ? rotation : parent.firstRotation;
                        const int rotationCount = shape.pad;
                        const int rootRotTaps = rotationCount > 0
                            ? (rotation - gCurrentRotation + rotationCount) % rotationCount : 0;
                        child.rotationTaps = gDepth == 0 ? rootRotTaps : parent.rotationTaps;
                        child.horizontalTaps = gDepth == 0
                            ? abs(x - gCurrentX) : parent.horizontalTaps;
                        child.firstLines = gDepth == 0 ? cleared : parent.firstLines;
                        child.multiClearEvents = parent.multiClearEvents + (cleared >= 2 ? 1 : 0);
                        child.multiClearLines = parent.multiClearLines + (cleared >= 2 ? cleared : 0);
                        child.valid = 1;
                        child.terminal = 0;
                        InterlockedOr(gAnyChild, 1u);
                    }
                }
            }
        }
    }
    gOutput[parentIndex * 40 + actionIndex] = child;
    GroupMemoryBarrierWithGroupSync();

    // If this parent had no legal child after at least one piece was placed,
    // preserve it as a terminal leaf. A root with no move remains top-out.
    if (actionIndex == 0 && parent.valid != 0 && parent.terminal == 0 &&
        gAnyChild == 0 && parent.placed > 0) {
        Node terminalNode = parent;
        terminalNode.valid = 1;
        terminalNode.terminal = 1;
        gOutput[parentIndex * 40] = terminalNode;
    }
}

Eval Evaluate(Node node) {
    Eval e;
    e.placed = node.placed;
    e.multiClearEvents = node.multiClearEvents;
    e.multiClearLines = node.multiClearLines;
    e.lines = node.lines;
    int heights[COLS];
    int aggregate = 0;
    int holes = 0;
    int maximum = 0;
    [unroll] for (int x = 0; x < COLS; ++x) {
        int first = ROWS;
        for (int y = 0; y < ROWS; ++y) {
            if ((node.rows[y] & (1u << x)) != 0) { first = y; break; }
        }
        heights[x] = ROWS - first;
        aggregate += heights[x];
        maximum = max(maximum, heights[x]);
        if (first < ROWS) {
            for (int y = first; y < ROWS; ++y)
                if ((node.rows[y] & (1u << x)) == 0) ++holes;
        }
    }
    int bumpiness = 0;
    [unroll] for (int x = 0; x < COLS - 1; ++x) bumpiness += abs(heights[x] - heights[x + 1]);
    int wells = 0;
    [unroll] for (int x = 0; x < COLS; ++x) {
        int left = x == 0 ? ROWS : heights[x - 1];
        int right = x == COLS - 1 ? ROWS : heights[x + 1];
        int floorHeight = min(left, right);
        if (floorHeight > heights[x]) wells += floorHeight - heights[x];
    }
    int rowTransitions = 0;
    for (int y = 0; y < ROWS; ++y) {
        int previous = 1;
        for (int x = 0; x < COLS; ++x) {
            int current = (node.rows[y] & (1u << x)) != 0 ? 1 : 0;
            if (current != previous) ++rowTransitions;
            previous = current;
        }
        if (previous == 0) ++rowTransitions;
    }
    int colTransitions = 0;
    [unroll] for (int x = 0; x < COLS; ++x) {
        int previous = 1;
        for (int y = 0; y < ROWS; ++y) {
            int current = (node.rows[y] & (1u << x)) != 0 ? 1 : 0;
            if (current != previous) ++colTransitions;
            previous = current;
        }
        if (previous == 0) ++colTransitions;
    }
    e.holes = holes;
    e.maxHeight = maximum;
    e.aggregateHeight = aggregate;
    e.bumpiness = bumpiness;
    e.wells = wells;
    e.rowTransitions = rowTransitions;
    e.colTransitions = colTransitions;
    e.rotationTaps = node.rotationTaps;
    e.horizontalTaps = node.horizontalTaps;
    e.keyTaps = node.rotationTaps + node.horizontalTaps;
    e.firstX = node.firstX;
    e.firstRotation = node.firstRotation;
    return e;
}

bool Better(Eval a, Eval b) {
    if (a.placed != b.placed) return a.placed > b.placed;
    if (a.multiClearEvents != b.multiClearEvents) return a.multiClearEvents > b.multiClearEvents;
    if (a.multiClearLines != b.multiClearLines) return a.multiClearLines > b.multiClearLines;
    if (a.lines != b.lines) return a.lines > b.lines;
    if (a.holes != b.holes) return a.holes < b.holes;
    if (a.maxHeight != b.maxHeight) return a.maxHeight < b.maxHeight;
    if (a.aggregateHeight != b.aggregateHeight) return a.aggregateHeight < b.aggregateHeight;
    if (a.bumpiness != b.bumpiness) return a.bumpiness < b.bumpiness;
    if (a.wells != b.wells) return a.wells < b.wells;
    if (a.rowTransitions != b.rowTransitions) return a.rowTransitions < b.rowTransitions;
    if (a.colTransitions != b.colTransitions) return a.colTransitions < b.colTransitions;
    if (a.keyTaps != b.keyTaps) return a.keyTaps < b.keyTaps;
    if (a.rotationTaps != b.rotationTaps) return a.rotationTaps < b.rotationTaps;
    if (a.horizontalTaps != b.horizontalTaps) return a.horizontalTaps < b.horizontalTaps;
    if (a.firstX != b.firstX) return a.firstX < b.firstX;
    return a.firstRotation < b.firstRotation;
}

groupshared Node gBestNodes[64];
groupshared Eval gBestEvals[64];
groupshared uint gHasBest[64];

[numthreads(64, 1, 1)]
void ReduceCS(uint3 groupThreadId : SV_GroupThreadID) {
    const uint lane = groupThreadId.x;
    bool has = false;
    Node bestNode;
    Eval bestEval;
    [unroll] for (int r = 0; r < ROWS; ++r) {
        bestNode.rows[r] = 0;
        bestNode.firstRows[r] = 0;
    }
    bestNode.placed = 0; bestNode.lines = 0; bestNode.firstX = 0;
    bestNode.firstRotation = 0; bestNode.rotationTaps = 0;
    bestNode.horizontalTaps = 0; bestNode.firstLines = 0;
    bestNode.multiClearEvents = 0; bestNode.multiClearLines = 0;
    bestNode.valid = 0; bestNode.terminal = 0;

    for (uint i = lane; i < gInputCount; i += 64) {
        Node node = gInput[i];
        if (node.valid == 0) continue;
        Eval e = Evaluate(node);
        if (!has || Better(e, bestEval)) {
            has = true;
            bestNode = node;
            bestEval = e;
        }
    }
    gBestNodes[lane] = bestNode;
    gBestEvals[lane] = bestEval;
    gHasBest[lane] = has ? 1u : 0u;
    GroupMemoryBarrierWithGroupSync();

    for (uint offset = 32; offset > 0; offset >>= 1) {
        if (lane < offset && gHasBest[lane + offset] != 0 &&
            (gHasBest[lane] == 0 || Better(gBestEvals[lane + offset], gBestEvals[lane]))) {
            gBestNodes[lane] = gBestNodes[lane + offset];
            gBestEvals[lane] = gBestEvals[lane + offset];
            gHasBest[lane] = 1;
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (lane == 0) gBest[0] = gBestNodes[0];
}
