// Adapter from Windows TOP visual-player types to the portable BFS planner.
// The tetris header is included at the top of main.cpp, outside the app namespace.
struct ReachableRoot {
    ActionCandidate candidate{};
    std::vector<tetris::MovementAction> path;
    int rotationTaps{};
    int horizontalTaps{};
};

namespace {
tetris::PieceCatalog g_topReachabilityCatalog;
tetris::ReachabilityOptions g_topReachabilityOptions;
bool g_topReachabilityInitialized = false;
bool g_topReachabilityReady = false;

tetris::PieceKind ToCorePieceKind(char piece) {
    switch (piece) {
    case 'I': return tetris::PieceKind::I;
    case 'O': return tetris::PieceKind::O;
    case 'T': return tetris::PieceKind::T;
    case 'S': return tetris::PieceKind::S;
    case 'Z': return tetris::PieceKind::Z;
    case 'J': return tetris::PieceKind::J;
    case 'L': return tetris::PieceKind::L;
    default: return tetris::PieceKind::Count;
    }
}
tetris::Board ToCoreBoard(const Board& source) {
    tetris::Board result{};
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < COLS; ++x)
            if (source[y][x]) result.Set(x, y, true);
    return result;
}
Board FromCoreBoard(const tetris::Board& source) {
    Board result{};
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < COLS; ++x)
            result[y][x] = source.Occupied(x, y) ? 1 : 0;
    return result;
}
bool EnsureTopReachabilityProfile(std::wstring& error) {
    if (!g_topReachabilityInitialized) {
        g_topReachabilityInitialized = true;
        g_topReachabilityReady = tetris::ConfigureTopPieceProfile(
            g_topReachabilityCatalog, g_topReachabilityOptions);
        if (g_topReachabilityReady) {
            // Use only the clockwise Up-key mapping already used by this client.
            g_topReachabilityOptions.rotationTransitions.erase(
                std::remove_if(
                    g_topReachabilityOptions.rotationTransitions.begin(),
                    g_topReachabilityOptions.rotationTransitions.end(),
                    [](const tetris::RotationTransitionRule& rule) {
                        return !rule.clockwise;
                    }),
                g_topReachabilityOptions.rotationTransitions.end());
            // Soft drop is not calibrated. Re-plan after observing natural gravity.
            g_topReachabilityOptions.allowSoftDrop = false;
            g_topReachabilityOptions.allowGravitySteps = true;
        }
    }
    if (!g_topReachabilityReady) {
        error = L"无法初始化 TOP 方块形状/旋转目录。";
        return false;
    }
    return true;
}
void CountPathActions(const std::vector<tetris::MovementAction>& path,
                      int& rotations, int& horizontal) {
    rotations = 0;
    horizontal = 0;
    for (const tetris::MovementAction action : path) {
        switch (action) {
        case tetris::MovementAction::RotateClockwise: ++rotations; break;
        case tetris::MovementAction::MoveLeft:
        case tetris::MovementAction::MoveRight: ++horizontal; break;
        default: break;
        }
    }
}
} // namespace

bool BuildReachableRoots(const Board& board, char piece, int rotation,
                         int x, int y, std::vector<ReachableRoot>& roots,
                         std::wstring& error) {
    roots.clear();
    if (!EnsureTopReachabilityProfile(error)) return false;
    const tetris::PieceKind kind = ToCorePieceKind(piece);
    if (kind == tetris::PieceKind::Count) {
        error = L"当前方块类型不在 TOP 几何目录中。";
        return false;
    }
    const tetris::ReachabilityResult reachable = tetris::ReachabilityPlanner{}.Enumerate(
        ToCoreBoard(board), tetris::ActivePiece{kind, rotation, x, y},
        g_topReachabilityCatalog, g_topReachabilityOptions);
    if (!reachable.validStart) {
        error = L"活动方块与当前固定棋盘不一致，BFS 拒绝规划。";
        return false;
    }
    if (reachable.truncated) {
        error = L"BFS 达到状态上限，候选落点不完整；本次不执行。";
        return false;
    }
    if (reachable.landings.empty()) {
        error = L"当前方块没有通过已配置旋转/横移规则可达的硬降落点。";
        return false;
    }
    roots.reserve(reachable.landings.size());
    for (const tetris::ReachablePlacement& landing : reachable.landings) {
        if (landing.path.empty() ||
            landing.path.back() != tetris::MovementAction::HardDrop) {
            error = L"BFS 返回了不完整的硬降路径；本次不执行。";
            roots.clear();
            return false;
        }
        ReachableRoot root;
        root.candidate.move.rotation = landing.placement.rotation;
        root.candidate.move.x = landing.placement.x;
        root.candidate.linesCleared = landing.placement.linesCleared;
        root.candidate.nextBoard = FromCoreBoard(landing.placement.resultingBoard);
        root.candidate.features = ExtractFeatures(
            root.candidate.nextBoard, root.candidate.linesCleared);
        root.path = landing.path;
        CountPathActions(root.path, root.rotationTaps, root.horizontalTaps);
        roots.push_back(std::move(root));
    }
    return !roots.empty();
}

bool FindReachablePathForTarget(const Board& board, char piece,
                                int rotation, int x, int y,
                                int targetRotation, int targetX,
                                const Board& expectedBoard,
                                std::vector<tetris::MovementAction>& path,
                                std::wstring& error) {
    path.clear();
    if (!EnsureTopReachabilityProfile(error)) return false;
    const tetris::PieceKind kind = ToCorePieceKind(piece);
    if (kind == tetris::PieceKind::Count) {
        error = L"当前方块类型不在 TOP 几何目录中。";
        return false;
    }
    const tetris::ReachabilityResult reachable = tetris::ReachabilityPlanner{}.Enumerate(
        ToCoreBoard(board), tetris::ActivePiece{kind, rotation, x, y},
        g_topReachabilityCatalog, g_topReachabilityOptions);
    if (!reachable.validStart || reachable.truncated) {
        error = reachable.truncated
            ? L"重新规划时 BFS 达到状态上限。"
            : L"重新规划时活动方块状态无效。";
        return false;
    }
    for (const tetris::ReachablePlacement& landing : reachable.landings) {
        if (landing.placement.rotation != targetRotation ||
            landing.placement.x != targetX) continue;
        if (FromCoreBoard(landing.placement.resultingBoard) != expectedBoard) continue;
        path = landing.path;
        return !path.empty() && path.back() == tetris::MovementAction::HardDrop;
    }
    error = L"当前实时位置已无法按 BFS 路径到达原定落点；必须重新求解。";
    return false;
}
