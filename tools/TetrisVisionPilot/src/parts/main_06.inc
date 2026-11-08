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
    HWND hwnd{}, combo{}, regionLabel{}, nextRegionLabel{}, nextStatusLabel{}, nextCountEdit{}, lookaheadEdit{};
    HWND scoreRegionLabel{}, scoreStatusLabel{}, againRegionButton{};
    HWND thresholdEdit{}, logEdit{}, preview{}, startButton{}, stopButton{}, statusLabel{};
    HWND scoreEdit{}, learningRateEdit{}, epsilonEdit{}, temperatureEdit{}, trainingStatsLabel{};
    HWND keyHoldEdit{}, keyGapEdit{}, dropWaitEdit{}, autoRetryCheck{};
    std::wstring tessdataPath{};
    Rect boardRect{}, nextRect{}, scoreRect{}, againRect{};
    std::vector<char> previewNextPieces;
    bool hasNextPreview{};
    PolicyTrainer trainer;
    ExperienceMemory experienceMemory;
    std::wstring experiencePath;
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

bool SaveCurrentEpisodeMemory(AppState* s, int score) {
    const TrainerSnapshot snapshot=s->trainer.Snapshot();
    const auto experiences=s->trainer.PendingExperiences();
    if(experiences.empty()) return true;
    const bool aboveBaseline=snapshot.games>0 && static_cast<double>(score)>snapshot.averageScore;
    size_t written=0; std::wstring error;
    if(!s->experienceMemory.AppendEpisode(s->experiencePath,score,snapshot.averageScore,
                                          aboveBaseline,experiences,written,error)) {
        PostLog(s->hwnd,L"经验档案保存失败："+error);
        return false;
    }
    PostLog(s->hwnd,Format(L"经验档案追加 %llu 条：%s；可检索成功经验共 %llu 条。",
        static_cast<unsigned long long>(written),aboveBaseline?L"高于基准，加入成功经验记忆":L"仅作结果记录，不加入成功偏好库",
        static_cast<unsigned long long>(s->experienceMemory.Size())));
    return true;
}

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

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) return {};
    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required, nullptr, nullptr);
    result.resize(static_cast<size_t>(required - 1));
    return result;
}

bool ReadOcrText(tesseract::TessBaseAPI& ocr, const Rect& rect,
                 const char* whitelist, tesseract::PageSegMode pageMode,
                 std::string& output, std::wstring& error, int* confidence = nullptr) {
    CapturedImage image;
    if (!CaptureScreenRect(rect, image, error)) return false;
    std::vector<unsigned char> rgb(static_cast<size_t>(image.width) * image.height * 3);
    for (size_t i = 0; i < image.pixels.size(); ++i) {
        rgb[i * 3 + 0] = image.pixels[i].r;
        rgb[i * 3 + 1] = image.pixels[i].g;
        rgb[i * 3 + 2] = image.pixels[i].b;
    }
    ocr.SetVariable("tessedit_char_whitelist", whitelist);
    ocr.SetPageSegMode(pageMode);
    ocr.SetSourceResolution(300);
    ocr.SetImage(rgb.data(), image.width, image.height, 3, image.width * 3);
    char* raw = ocr.GetUTF8Text();
    if (!raw) { error = L"OCR 没有返回文本。"; return false; }
    output.assign(raw);
    delete[] raw;
    if (confidence) *confidence = ocr.MeanTextConf();
    return true;
}

bool ParseScoreDigits(const std::string& text, int& scoreOut) {
    std::string digits;
    for (unsigned char c : text) {
        if (c >= '0' && c <= '9') digits.push_back(static_cast<char>(c));
        else if (c == ',' || c == '.' || c == ':' || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        else return false;
    }
    if (digits.empty() || digits.size() > 9) return false;
    try {
        const long value = std::stol(digits);
        if (value < 0 || value > 100000000) return false;
        scoreOut = static_cast<int>(value);
        return true;
    } catch (...) { return false; }
}

bool IsGameOverText(const std::string& text) {
    std::string normalized;
    for (unsigned char c : text) {
        if (std::isalpha(c)) normalized.push_back(static_cast<char>(std::toupper(c)));
        else if (c == '0') normalized.push_back('O');
    }
    return normalized.find("GAME") != std::string::npos &&
           normalized.find("OVER") != std::string::npos;
}

Rect ClientRelativeRect(HWND target, double x, double y, double width, double height) {
    RECT client{};
    if (!GetClientRect(target, &client)) return {};
    POINT origin{0, 0};
    if (!ClientToScreen(target, &origin)) return {};
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;
    return {origin.x + static_cast<int>(x * w), origin.y + static_cast<int>(y * h),
            static_cast<int>(width * w), static_cast<int>(height * h)};
}

bool IsScreenRectInsideClient(HWND target, const Rect& screenRect) {
    if (screenRect.width <= 0 || screenRect.height <= 0) return false;
    RECT client{};
    if (!GetClientRect(target, &client)) return false;
    POINT origin{0,0};
    if (!ClientToScreen(target, &origin)) return false;
    const RECT bounds{origin.x, origin.y,
                      origin.x + (client.right - client.left),
                      origin.y + (client.bottom - client.top)};
    const RECT selected{screenRect.left, screenRect.top,
                        screenRect.left + screenRect.width,
                        screenRect.top + screenRect.height};
    return selected.left >= bounds.left && selected.top >= bounds.top &&
           selected.right <= bounds.right && selected.bottom <= bounds.bottom;
}

bool ClickAgainButton(HWND target, const Rect& buttonRect, std::wstring& error) {
    if (GetAncestor(GetForegroundWindow(), GA_ROOT) != target) {
        error = L"游戏窗口已失去前台焦点，未点击 Again。";
        return false;
    }
    if (!IsScreenRectInsideClient(target, buttonRect)) {
        error = L"Again 按钮框选区域不在游戏窗口客户区内，未执行点击。请重新框选。";
        return false;
    }
    const int x = buttonRect.left + buttonRect.width / 2;
    const int y = buttonRect.top + buttonRect.height / 2;
    if (!SetCursorPos(x, y)) { error = L"无法移动鼠标到 Again 按钮。"; return false; }
    INPUT down{};
    down.type = INPUT_MOUSE;
    down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    if (SendInput(1, &down, sizeof(INPUT)) != 1) { error = L"Again 鼠标按下失败。"; return false; }
    Sleep(35);
    INPUT up{};
    up.type = INPUT_MOUSE;
    up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    if (SendInput(1, &up, sizeof(INPUT)) != 1) {
        SendInput(1, &up, sizeof(INPUT));
        error = L"Again 鼠标释放失败。";
        return false;
    }
    return true;
}

void AppendTrainingCsv(AppState* s, int score, double learningRate, double epsilon, double temperature);

void BotThread(AppState* s, HWND target, Rect rect, Rect nextRect, int threshold,
               int previewCount, int lookaheadDepth, double epsilon, double temperature,
               int keyHoldMs, int keyGapMs, int dropWaitMs, Rect scoreRect, Rect againRect,
               double learningRate, bool autoRetry) {
    auto log=[&](const std::wstring& m){Log(s,m);};
    try {
        if(!IsWindow(target)) throw std::runtime_error("所选游戏窗口已关闭。");
        ShowWindow(target,SW_RESTORE);
