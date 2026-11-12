// Deterministic three-piece exhaustive search shared by CPU and GPU backends.
// "Optimal" is relative to the visible sequence (current + up to two NEXT)
// and the declared objective; it is not a proof of whole-game global optimality.
struct ExhaustiveEvaluation {
    int placed{};
    int multiClearEvents{};
    int multiClearLines{};
    int lines{};
    int holes{};
    int maxHeight{};
    int aggregateHeight{};
    int bumpiness{};
    int wells{};
    int rowTransitions{};
    int colTransitions{};
    int keyTaps{};
    int rotationTaps{};
    int horizontalTaps{};
    int firstX{};
    int firstRotation{};
};

struct ExhaustiveDecision {
    Move move{};
    Board expectedBoard{};
    int predictedLines{};
    int searchedNextPieces{};
    std::uint64_t nodes{};
    double elapsedMs{};
    int keyTaps{};
    int rotationTaps{};
    int horizontalTaps{};
    int multiClearEvents{};
    int multiClearLines{};
};

bool BetterExhaustive(const ExhaustiveEvaluation& a, const ExhaustiveEvaluation& b) {
    if (a.placed != b.placed) return a.placed > b.placed;
    // Give explicit preference to one placement clearing two or more rows.
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

ExhaustiveEvaluation EvaluateExhaustiveBoard(const Board& board, int placed, int lines,
                                               int multiClearEvents, int multiClearLines) {
    const Features f = ExtractFeatures(board, 0);
    ExhaustiveEvaluation e;
    e.placed = placed;
    e.multiClearEvents = multiClearEvents;
    e.multiClearLines = multiClearLines;
    e.lines = lines;
    e.aggregateHeight = static_cast<int>(std::lround(f.value[0] * 200.0));
    e.holes = static_cast<int>(std::lround(f.value[1] * 40.0));
    e.bumpiness = static_cast<int>(std::lround(f.value[2] * 40.0));
    e.maxHeight = static_cast<int>(std::lround(f.value[3] * 20.0));
    e.wells = static_cast<int>(std::lround(f.value[4] * 80.0));
    e.rowTransitions = static_cast<int>(std::lround(f.value[5] * 200.0));
    e.colTransitions = static_cast<int>(std::lround(f.value[6] * 200.0));
    return e;
}

struct ExhaustiveSearchState {
    bool valid{};
    ExhaustiveEvaluation evaluation{};
    Move firstMove{};
    Board firstBoard{};
    int firstLines{};
};

void ExhaustiveSearch(const Board& board, const std::vector<char>& sequence,
                      size_t depth, int placed, int totalLines,
                      int multiClearEvents, int multiClearLines,
                      const Move& firstMove, const Board& firstBoard, int firstLines,
                      int rotationTaps, int horizontalTaps,
                      int initialRotation, int initialX,
                      ExhaustiveSearchState& best, std::uint64_t& nodes) {
    if (depth >= sequence.size()) {
        ExhaustiveEvaluation evaluation = EvaluateExhaustiveBoard(
            board, placed, totalLines, multiClearEvents, multiClearLines);
        evaluation.rotationTaps = rotationTaps;
        evaluation.horizontalTaps = horizontalTaps;
        evaluation.keyTaps = rotationTaps + horizontalTaps;
        evaluation.firstX = firstMove.x;
        evaluation.firstRotation = firstMove.rotation;
        if (!best.valid || BetterExhaustive(evaluation, best.evaluation)) {
            best.valid = true;
            best.evaluation = evaluation;
            best.firstMove = firstMove;
            best.firstBoard = firstBoard;
            best.firstLines = firstLines;
        }
        return;
    }

    const std::vector<ActionCandidate> actions = EnumerateActions(board, sequence[depth]);
    nodes += static_cast<std::uint64_t>(actions.size());
    if (actions.empty()) {
        if (depth == 0) return; // No legal move for the current piece is a top-out.
        ExhaustiveEvaluation evaluation = EvaluateExhaustiveBoard(
            board, placed, totalLines, multiClearEvents, multiClearLines);
        evaluation.rotationTaps = rotationTaps;
        evaluation.horizontalTaps = horizontalTaps;
        evaluation.keyTaps = rotationTaps + horizontalTaps;
        evaluation.firstX = firstMove.x;
        evaluation.firstRotation = firstMove.rotation;
        if (!best.valid || BetterExhaustive(evaluation, best.evaluation)) {
            best.valid = true;
            best.evaluation = evaluation;
            best.firstMove = firstMove;
            best.firstBoard = firstBoard;
            best.firstLines = firstLines;
        }
        return;
    }

    for (const ActionCandidate& action : actions) {
        const int nextMultiEvents = multiClearEvents + (action.linesCleared >= 2 ? 1 : 0);
        const int nextMultiLines = multiClearLines + (action.linesCleared >= 2 ? action.linesCleared : 0);
        if (depth == 0) {
            const auto rotations = g_rotations.find(sequence[depth]);
            const int rotationCount = rotations == g_rotations.end()
                ? 1 : static_cast<int>(rotations->second.size());
            const int rootRotationTaps = rotationCount > 0
                ? (action.move.rotation - initialRotation + rotationCount) % rotationCount : 0;
            const int rootHorizontalTaps = std::abs(action.move.x - initialX);
            ExhaustiveSearch(action.nextBoard, sequence, depth + 1, placed + 1,
                totalLines + action.linesCleared, nextMultiEvents, nextMultiLines,
                action.move, action.nextBoard, action.linesCleared,
                rootRotationTaps, rootHorizontalTaps,
                initialRotation, initialX, best, nodes);
        } else {
            ExhaustiveSearch(action.nextBoard, sequence, depth + 1, placed + 1,
                totalLines + action.linesCleared, nextMultiEvents, nextMultiLines,
                firstMove, firstBoard, firstLines, rotationTaps, horizontalTaps,
                initialRotation, initialX, best, nodes);
        }
    }
}

bool ChooseExhaustiveAction(const Board& board, char piece, int currentRotation,
                            int currentX, const std::vector<char>& nextQueue,
                            ExhaustiveDecision& decision) {
    const auto rotationIt = g_rotations.find(piece);
    if (rotationIt == g_rotations.end() || rotationIt->second.empty()) return false;
    std::vector<char> sequence{piece};
    const int futureCount = (std::min)(2, static_cast<int>(nextQueue.size()));
    for (int i = 0; i < futureCount; ++i) sequence.push_back(nextQueue[static_cast<size_t>(i)]);

    ExhaustiveSearchState best{};
    std::uint64_t nodes = 0;
    const auto started = std::chrono::steady_clock::now();
    ExhaustiveSearch(board, sequence, 0, 0, 0, 0, 0, Move{}, Board{}, 0,
                     0, 0, currentRotation, currentX, best, nodes);
    const auto finished = std::chrono::steady_clock::now();
    if (!best.valid) return false;

    const int rotationCount = static_cast<int>(rotationIt->second.size());
    const int rotationTaps = rotationCount > 0
        ? (best.firstMove.rotation - currentRotation + rotationCount) % rotationCount : 0;
    const int horizontalTaps = std::abs(best.firstMove.x - currentX);
    decision.move = best.firstMove;
    decision.expectedBoard = best.firstBoard;
    decision.predictedLines = best.firstLines;
    decision.searchedNextPieces = futureCount;
    decision.nodes = nodes;
    decision.elapsedMs = std::chrono::duration<double, std::milli>(finished - started).count();
    decision.rotationTaps = rotationTaps;
    decision.horizontalTaps = horizontalTaps;
    decision.keyTaps = rotationTaps + horizontalTaps;
    decision.multiClearEvents = best.evaluation.multiClearEvents;
    decision.multiClearLines = best.evaluation.multiClearLines;
    return true;
}
