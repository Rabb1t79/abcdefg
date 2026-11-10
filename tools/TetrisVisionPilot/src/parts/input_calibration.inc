// Opening input calibration is deliberately isolated from PolicyTrainer:
// calibration movements are restored before planning and never become samples.
enum class CalibrationOutcome {
    Success,
    Skipped,
    Unresponsive,
    Unsafe
};

struct InputCalibrationResult {
    int leftHoldMs{};
    int rightHoldMs{};
    bool leftCalibrated{};
    bool rightCalibrated{};
};

bool ReadCalibrationPiece(HWND target, const Rect& rect, int threshold,
                          char expectedPiece, int expectedRotation,
                          int& xOut, int& yOut, std::wstring& error) {
    VisionResult vision = AnalyzeStable(rect, threshold, error);
    if (!error.empty()) return false;
    if (!vision.hasActive ||
        vision.active.name != expectedPiece ||
        vision.active.rotation != expectedRotation) {
        error = L"校准期间未能稳定确认同一个活动方块；为避免位置误判，停止校准。";
        return false;
    }
    xOut = vision.active.minX;
    yOut = vision.active.minY;
    return true;
}

bool RestoreCalibrationColumn(const std::atomic<bool>& stop, HWND target,
                              const Rect& rect, int threshold, char piece,
                              int rotation, int targetX, int& currentX,
                              int& currentY, int preferredHoldMs,
                              std::wstring& error) {
    const int correctionHolds[] = {
        (std::max)(8, preferredHoldMs / 2),
        (std::max)(8, preferredHoldMs - 5),
        (std::max)(8, preferredHoldMs)
    };
    for (int step = 0; step < 8 && currentX != targetX; ++step) {
        if (stop.load()) { error = L"用户请求停止。"; return false; }
        const int direction = targetX > currentX ? 1 : -1;
        const WORD key = direction > 0 ? VK_RIGHT : VK_LEFT;
        bool advanced = false;
        for (int holdMs : correctionHolds) {
            std::wstring inputError;
            if (!TapKey(target,key,holdMs,inputError)) { error=inputError; return false; }
            SleepCheck(stop,55);
            if (stop.load()) { error=L"用户请求停止。"; return false; }
            int observedX=0, observedY=0;
            if (!ReadCalibrationPiece(target,rect,threshold,piece,rotation,
                                      observedX,observedY,error)) return false;
            const int delta = observedX - currentX;
            if (delta == 0) continue;
            // A correction pulse must move exactly one cell toward the saved
            // column. Any overshoot is treated as unsafe rather than guessed.
            if (delta != direction) {
                error = L"校准复位时按键移动距离不符合单格预期；已中止自动操作。";
                return false;
            }
            currentX = observedX;
            currentY = observedY;
            advanced = true;
            break;
        }
        if (!advanced) {
            error = L"无法把方块可靠地复位到校准前的位置；已中止自动操作。";
            return false;
        }
    }
    if (currentX != targetX) {
        error = L"校准复位超过最大修正次数；已中止自动操作。";
        return false;
    }
    return true;
}

CalibrationOutcome CalibrateOneDirection(const std::atomic<bool>& stop,
                                         HWND target, const Rect& rect,
                                         int threshold, const Board& board,
                                         const Candidate& startPiece, WORD directionKey,
                                         int baseHoldMs, int& selectedHoldMs,
                                         std::wstring& note) {
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

    const int holds[] = {
        (std::max)(8,(std::min)(80,baseHoldMs)),
        (std::max)(8,(std::min)(80,baseHoldMs+8)),
        (std::max)(8,(std::min)(80,baseHoldMs+16))
    };
    int originalX=startPiece.minX;
    int currentX=originalX;
    int currentY=startPiece.minY;
    bool moved=false;
    int chosen=baseHoldMs;
    for (int holdMs : holds) {
        if (stop.load()) { note=L"用户请求停止。"; return CalibrationOutcome::Unsafe; }
        std::wstring inputError;
        if (!TapKey(target,directionKey,holdMs,inputError)) {
            note=inputError;
            return CalibrationOutcome::Unsafe;
        }
        SleepCheck(stop,55);
        if (stop.load()) { note=L"用户请求停止。"; return CalibrationOutcome::Unsafe; }
        int observedX=0, observedY=0;
        std::wstring visionError;
        if (!ReadCalibrationPiece(target,rect,threshold,startPiece.name,
                                  startPiece.rotation,observedX,observedY,visionError)) {
            note=visionError;
            return CalibrationOutcome::Unsafe;
        }
        const int delta=observedX-originalX;
        if (delta==0) {
            currentX=observedX;
            currentY=observedY;
            continue;
        }
        if ((delta>0?1:-1)!=dx) {
            note=L"校准按键的实际移动方向与预期不一致；停止以避免误操作。";
            return CalibrationOutcome::Unsafe;
        }
        moved=true;
        currentX=observedX;
        currentY=observedY;
        chosen=holdMs;
        break;
    }

    if (!moved) {
        note=directionKey==VK_LEFT
            ? L"左移短按未检测到有效位移，暂时保留原按键时长。"
            : L"右移短按未检测到有效位移，暂时保留原按键时长。";
        return CalibrationOutcome::Unresponsive;
    }

    // A one-cell result is the only measurement accepted as a calibrated tap.
    // Longer movement is still restored, but not used to tune the controller.
    const bool exactOneCell = std::abs(currentX-originalX)==1;
    std::wstring restoreError;
    if (!RestoreCalibrationColumn(stop,target,rect,threshold,startPiece.name,
                                   startPiece.rotation,originalX,currentX,currentY,
                                   chosen,restoreError)) {
        note=restoreError;
        return CalibrationOutcome::Unsafe;
    }
    if (!exactOneCell) {
        note=L"单次按键触发了多格移动；位置已复原，但本次数据不用于校准。";
        return CalibrationOutcome::Unresponsive;
    }
    selectedHoldMs=chosen;
    note=Format(L"%s短按校准成功：%d ms，实际移动 1 格；已复位。",
        directionKey==VK_LEFT?L"左":L"右",chosen);
    return CalibrationOutcome::Success;
}

CalibrationOutcome CalibrateOpeningInput(const std::atomic<bool>& stop,
                                         HWND target, const Rect& rect, int threshold,
                                         const VisionResult& initial,
                                         const Board& board, int baseHoldMs,
                                         InputCalibrationResult& result,
                                         std::wstring& summary) {
    result.leftHoldMs=baseHoldMs;
    result.rightHoldMs=baseHoldMs;
    if (!initial.hasActive || initial.active.minY>2 ||
        ExtractFeatures(board,0).value[3] > 0.55) {
        summary=L"当前方块已离开安全出生区或棋盘堆叠较高，跳过开局移动校准，沿用原按键时长。";
        return CalibrationOutcome::Skipped;
    }

    Candidate current=initial.active;
    std::wstring leftNote,rightNote;
    const CalibrationOutcome left=CalibrateOneDirection(stop,target,rect,threshold,
        board,current,VK_LEFT,baseHoldMs,result.leftHoldMs,leftNote);
    if (left==CalibrationOutcome::Unsafe) { summary=leftNote; return left; }

    // Read the current vertical position again because the game continues to
    // apply gravity while the calibration pulse and image verification run.
    std::wstring captureError;
    int x=0,y=0;
    if (!ReadCalibrationPiece(target,rect,threshold,current.name,current.rotation,
                              x,y,captureError)) {
        summary=captureError;
        return CalibrationOutcome::Unsafe;
    }
    current.minX=x;
    current.minY=y;
    const Board currentBoard=board;
    const CalibrationOutcome right=CalibrateOneDirection(stop,target,rect,threshold,
        currentBoard,current,VK_RIGHT,baseHoldMs,result.rightHoldMs,rightNote);
    if (right==CalibrationOutcome::Unsafe) { summary=rightNote; return right; }

    result.leftCalibrated=left==CalibrationOutcome::Success;
    result.rightCalibrated=right==CalibrationOutcome::Success;
    summary=leftNote+L" "+rightNote;
    return result.leftCalibrated||result.rightCalibrated
        ? CalibrationOutcome::Success
        : CalibrationOutcome::Unresponsive;
}
