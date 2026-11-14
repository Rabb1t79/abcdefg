// Opening input calibration is deliberately isolated from PolicyTrainer.
// It tests discrete key taps only: one key-down, a short release interval, and
// one key-up. It never calibrates by holding a direction key for multiple cells.
enum class CalibrationOutcome {
    Success,
    Skipped,
    Unresponsive,
    Unsafe
};

struct InputCalibrationResult {
    int leftTapMs{};
    int rightTapMs{};
    bool leftCalibrated{};
    bool rightCalibrated{};
};

bool ReadCalibrationPiece(HWND target, const Rect& rect, int threshold,
                          char expectedPiece, int expectedRotation,
                          int referenceX, int referenceY,
                          int& xOut, int& yOut, std::wstring& error) {
    // A 4 ms sampling interval often captures the same rendered frame twice.
    // That makes a stationary active piece indistinguishable from settled cells.
    VisionResult vision = AnalyzeStable(rect, threshold, error, 3, 16);
    if (!error.empty()) return false;
    if (vision.hasActive &&
        vision.active.name == expectedPiece &&
        vision.active.rotation == expectedRotation) {
        xOut = vision.active.minX;
        yOut = vision.active.minY;
        return true;
    }

    // During calibration, the piece identity and its previous location are
    // already known. Use that narrow corridor as a safe fallback when the
    // frame-to-frame motion tracker has not yet seen a full game-frame change.
    const Candidate* match = nullptr;
    int matches = 0;
    for (const Candidate& candidate : vision.components) {
        if (candidate.name != expectedPiece ||
            candidate.rotation != expectedRotation ||
            std::abs(candidate.minX - referenceX) > 1 ||
            candidate.minY < referenceY - 1 ||
            candidate.minY > referenceY + 5) continue;
        match = &candidate;
        ++matches;
    }
    if (matches == 1 && match) {
        xOut = match->minX;
        yOut = match->minY;
        return true;
    }

    error = L"校准期间无法唯一确认原活动方块；为避免对错误方块输入，停止校准。";
    return false;
}

bool RestoreCalibrationColumn(const std::atomic<bool>& stop, HWND target,
                              const Rect& rect, int threshold, char piece,
                              int rotation, int targetX, int& currentX,
                              int& currentY, int tapMs, int keyGapMs,
                              std::wstring& error) {
    for (int step = 0; step < 10 && currentX != targetX; ++step) {
        if (stop.load()) { error = L"用户请求停止。"; return false; }
        const int direction = targetX > currentX ? 1 : -1;
        const WORD key = direction > 0 ? VK_RIGHT : VK_LEFT;
        std::wstring inputError;
        if (!TapKey(target,key,tapMs,stop,inputError)) { error=inputError; return false; }
        SleepCheck(stop,keyGapMs);
        // Allow at least one rendered game frame to consume the key-down before
        // judging whether the pulse moved the tetromino.
        SleepCheck(stop,32);
        if (stop.load()) { error=L"用户请求停止。"; return false; }

        int observedX=0, observedY=0;
        if (!ReadCalibrationPiece(target,rect,threshold,piece,rotation,
                                  currentX,currentY,observedX,observedY,error)) return false;
        const int delta = observedX - currentX;
        // A restoration pulse must move exactly one cell toward the saved
        // column. Any overshoot is treated as unsafe rather than guessed.
        if (delta != direction) {
            error = L"校准复位时单次点按未移动恰好一格；已中止自动操作。";
            return false;
        }
        currentX = observedX;
        currentY = observedY;
    }
    if (currentX != targetX) {
        error = L"校准复位超过最大点按次数；已中止自动操作。";
        return false;
    }
    return true;
}

CalibrationOutcome CalibrateOneDirection(const std::atomic<bool>& stop,
                                         HWND target, const Rect& rect,
                                         int threshold, const Board& board,
                                         const Candidate& startPiece, WORD directionKey,
                                         int baseTapMs, int keyGapMs,
                                         int& selectedTapMs, std::wstring& note) {
    const auto rotations = g_rotations.find(startPiece.name);
    if (rotations == g_rotations.end() ||
        startPiece.rotation < 0 ||
        startPiece.rotation >= static_cast<int>(rotations->second.size())) {
        note = L"当前方块旋转形状无效，跳过该方向校准。";
        return CalibrationOutcome::Skipped;
    }
    const int dx = directionKey == VK_LEFT ? -1 : 1;
    const auto& shape = rotations->second[static_cast<size_t>(startPiece.rotation)];
    if (!Valid(board,shape,startPiece.minX+dx,startPiece.minY)) {
        note = directionKey == VK_LEFT
            ? L"左侧相邻格被墙壁或堆叠占用，跳过左移校准。"
            : L"右侧相邻格被墙壁或堆叠占用，跳过右移校准。";
        return CalibrationOutcome::Skipped;
    }

    // Try only short, fully released pulses. Each candidate is a separate tap;
    // no candidate holds the direction key long enough to request auto-repeat.
    const int tapCandidates[] = {2, 4, 8, 12, 16, 20, 24};
    bool triedTapMs[5]{};
    const int originalX=startPiece.minX;
    int currentX=originalX;
    int currentY=startPiece.minY;
    int chosenTapMs=0;
    bool moved=false;

    for (int tapMs : tapCandidates) {
        if (tapMs < 1 || tapMs > 4 || triedTapMs[tapMs]) continue;
        triedTapMs[tapMs]=true;
        if (stop.load()) { note=L"用户请求停止。"; return CalibrationOutcome::Unsafe; }
        std::wstring inputError;
        if (!TapKey(target,directionKey,tapMs,stop,inputError)) {
            note=inputError;
            return CalibrationOutcome::Unsafe;
        }
        SleepCheck(stop,keyGapMs);
        // Do not sample 4 ms after key-up: the game may not have rendered the
        // input yet. Wait across at least one frame, then track multiple frames.
        SleepCheck(stop,32);
        if (stop.load()) { note=L"用户请求停止。"; return CalibrationOutcome::Unsafe; }

        int observedX=0, observedY=0;
        std::wstring visionError;
        if (!ReadCalibrationPiece(target,rect,threshold,startPiece.name,
                                  startPiece.rotation,currentX,currentY,
                                  observedX,observedY,visionError)) {
            note=visionError;
            return CalibrationOutcome::Unsafe;
        }
        const int delta=observedX-originalX;
        currentX=observedX;
        currentY=observedY;
        if (delta==0) continue;
        if ((delta>0?1:-1)!=dx) {
            note=L"校准点按的实际移动方向与预期不一致；停止以避免误操作。";
            return CalibrationOutcome::Unsafe;
        }
        moved=true;
        chosenTapMs=tapMs;
        break;
    }

    if (!moved) {
        note=directionKey==VK_LEFT
            ? L"左方向键的短点按未触发移动；本方向未通过校准。"
            : L"右方向键的短点按未触发移动；本方向未通过校准。";
        return CalibrationOutcome::Unresponsive;
    }

    // Exactly one cell per pulse is the only accepted calibration result.
    const bool exactOneCell=std::abs(currentX-originalX)==1;
    std::wstring restoreError;
    if (!RestoreCalibrationColumn(stop,target,rect,threshold,startPiece.name,
                                  startPiece.rotation,originalX,currentX,currentY,
                                  chosenTapMs,keyGapMs,restoreError)) {
        note=restoreError;
        return CalibrationOutcome::Unsafe;
    }
    if (!exactOneCell) {
        note=L"单次短点按移动超过一格；已通过离散点按复位，但本方向不用于校准。";
        return CalibrationOutcome::Unresponsive;
    }

    selectedTapMs=chosenTapMs;
    note=Format(L"%s离散点按校准成功：按下 %d ms，单次移动 1 格；已复位。",
        directionKey==VK_LEFT?L"左":L"右",chosenTapMs);
    return CalibrationOutcome::Success;
}

CalibrationOutcome CalibrateOpeningInput(const std::atomic<bool>& stop,
                                         HWND target, const Rect& rect, int threshold,
                                         const VisionResult& initial,
                                         const Board& board, int baseTapMs,
                                         int keyGapMs,
                                         InputCalibrationResult& result,
                                         std::wstring& summary) {
    result.leftTapMs=baseTapMs;
    result.rightTapMs=baseTapMs;
    if (!initial.hasActive || initial.active.minY>2 ||
        ExtractFeatures(board,0).value[3] > 0.55) {
        summary=L"当前方块已离开安全出生区或棋盘堆叠较高，跳过开局点按校准，沿用默认短脉冲。";
        return CalibrationOutcome::Skipped;
    }

    Candidate current=initial.active;
    std::wstring leftNote,rightNote;
    const CalibrationOutcome left=CalibrateOneDirection(stop,target,rect,threshold,
        board,current,VK_LEFT,baseTapMs,keyGapMs,result.leftTapMs,leftNote);
    if (left==CalibrationOutcome::Unsafe) { summary=leftNote; return left; }

    // Read the current vertical position again because gravity continues while
    // the calibration pulse and visual verification run.
    std::wstring captureError;
    int x=0,y=0;
    if (!ReadCalibrationPiece(target,rect,threshold,current.name,current.rotation,
                              current.minX,current.minY,x,y,captureError)) {
        summary=captureError;
        return CalibrationOutcome::Unsafe;
    }
    current.minX=x;
    current.minY=y;
    const Board currentBoard=board;
    const CalibrationOutcome right=CalibrateOneDirection(stop,target,rect,threshold,
        currentBoard,current,VK_RIGHT,baseTapMs,keyGapMs,result.rightTapMs,rightNote);
    if (right==CalibrationOutcome::Unsafe) { summary=rightNote; return right; }

    result.leftCalibrated=left==CalibrationOutcome::Success;
    result.rightCalibrated=right==CalibrationOutcome::Success;
    summary=leftNote+L" "+rightNote;
    return result.leftCalibrated||result.rightCalibrated
        ? CalibrationOutcome::Success
        : CalibrationOutcome::Unresponsive;
}
