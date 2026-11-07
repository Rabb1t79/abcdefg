    for (size_t i = 0; i < candidates.size(); ++i) {
        double value = 0.0;
        for (size_t j = 0; j < FEATURE_COUNT; ++j) value += weights[j] * candidates[i].features.value[j];
        logits[i] = value / temperature;
        maxLogit = (std::max)(maxLogit, logits[i]);
    }
    std::vector<double> softmax(candidates.size());
    double sum = 0.0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        softmax[i] = std::exp((std::max)(-60.0, logits[i] - maxLogit));
        sum += softmax[i];
    }
    if (sum <= 0.0) return false;
    for (double& p : softmax) p /= sum;

    std::array<double, FEATURE_COUNT> expected{};
    for (size_t i = 0; i < candidates.size(); ++i)
        for (size_t j = 0; j < FEATURE_COUNT; ++j)
            expected[j] += softmax[i] * candidates[i].features.value[j];

    std::uniform_real_distribution<double> unit(0.0, 1.0);
    size_t chosen = 0;
    if (unit(rng) < epsilon) {
        std::uniform_int_distribution<size_t> pick(0, candidates.size() - 1);
        chosen = pick(rng);
    } else {
        std::discrete_distribution<size_t> pick(softmax.begin(), softmax.end());
        chosen = pick(rng);
    }

    const double actionProbability = (1.0 - epsilon) * softmax[chosen] + epsilon / static_cast<double>(candidates.size());
    const double gradientScale = ((1.0 - epsilon) * softmax[chosen] / (std::max)(1e-12, actionProbability)) / temperature;
    for (size_t j = 0; j < FEATURE_COUNT; ++j) {
        decision.experience.gradient[j] = gradientScale * (candidates[chosen].features.value[j] - expected[j]);
    }
    decision.move = candidates[chosen].move;
    decision.move.score = logits[chosen];
    decision.predictedLines = candidates[chosen].linesCleared;
    return true;
}

void SleepCheck(const std::atomic<bool>& stop, int milliseconds) {
    int elapsed=0;
    while(elapsed<milliseconds && !stop.load()) { int part=(std::min)(15,milliseconds-elapsed); Sleep(part); elapsed+=part; }
}


bool TapKey(HWND target, WORD vk, int holdMs, std::wstring& error) {
    if (GetAncestor(GetForegroundWindow(), GA_ROOT) != target) {
        error=L"游戏窗口已失去前台焦点，自动操作已停止，避免按键发到其他窗口。";
        return false;
    }
    INPUT input{};
    input.type=INPUT_KEYBOARD;
    input.ki.wVk=vk;
    if (vk==VK_LEFT || vk==VK_RIGHT || vk==VK_UP || vk==VK_DOWN) input.ki.dwFlags=KEYEVENTF_EXTENDEDKEY;
    if (SendInput(1,&input,sizeof(INPUT))!=1) { error=L"发送按键失败；请检查游戏窗口焦点和权限等级。"; return false; }
    Sleep(holdMs);
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1,&input,sizeof(INPUT));
    return true;
}

struct AppState {
    HWND hwnd{}, combo{}, regionLabel{}, thresholdEdit{}, logEdit{}, preview{}, startButton{}, stopButton{}, statusLabel{};
    HWND scoreEdit{}, learningRateEdit{}, epsilonEdit{}, temperatureEdit{}, trainingStatsLabel{};
    Rect boardRect{};
    PolicyTrainer trainer;
    std::atomic<bool> stop{false};
    std::atomic<bool> running{false};
    std::thread worker;
    VisionResult previewData{};
    bool hasPreview{};
    std::wstring configPath;
    std::wstring logPath;
    std::wstring modelPath;
    std::wstring trainingCsvPath;
};

int ReadThreshold(AppState* s) {
    wchar_t buf[32]{}; GetWindowTextW(s->thresholdEdit,buf,_countof(buf));
    int value=_wtoi(buf); if(value<10 || value>160)value=48; return value;
}

double ReadDoubleEdit(HWND edit, double fallback, double minimum, double maximum) {
    wchar_t buffer[64]{};
    GetWindowTextW(edit, buffer, _countof(buffer));
    wchar_t* end = nullptr;
    double value = std::wcstod(buffer, &end);
    if (end == buffer || !std::isfinite(value)) return fallback;
    return (std::max)(minimum, (std::min)(maximum, value));
}

void Log(AppState* s, const std::wstring& msg) { PostLog(s->hwnd,msg); }

void BotThread(AppState* s, HWND target, Rect rect, int threshold, double epsilon, double temperature) {
    auto log=[&](const std::wstring& m){Log(s,m);};
    try {
        if(!IsWindow(target)) throw std::runtime_error("所选游戏窗口已关闭。");
        ShowWindow(target,SW_RESTORE);
