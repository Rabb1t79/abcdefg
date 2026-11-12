#include "score_sprite_templates.inc"

void SleepCheck(const std::atomic<bool>& stop, int milliseconds) {
    int elapsed=0;
    while(elapsed<milliseconds && !stop.load()) { int part=(std::min)(15,milliseconds-elapsed); Sleep(part); elapsed+=part; }
}


class ScopedInputTimerResolution {
public:
    ScopedInputTimerResolution() : active_(timeBeginPeriod(1) == TIMERR_NOERROR) {}
    ~ScopedInputTimerResolution() {
        if (active_) timeEndPeriod(1);
    }
    bool Active() const { return active_; }
private:
    bool active_{};
};

bool TapKey(HWND target, WORD vk, int pressMs, std::wstring& error) {
    if (GetAncestor(GetForegroundWindow(), GA_ROOT) != target) {
        error=L"游戏窗口已失去前台焦点，自动操作已停止，避免按键发到其他窗口。";
        return false;
    }
    // Every call is one discrete key-down/key-up pulse. Keep the down interval
    // short enough to avoid the game's held-key auto-repeat path.
    const int boundedPressMs=(std::max)(1,(std::min)(4,pressMs));
    INPUT input{};
    input.type=INPUT_KEYBOARD;
    input.ki.wVk=vk;
    if (vk==VK_LEFT || vk==VK_RIGHT || vk==VK_UP || vk==VK_DOWN) input.ki.dwFlags=KEYEVENTF_EXTENDEDKEY;
    if (SendInput(1,&input,sizeof(INPUT))!=1) { error=L"发送按键失败；请检查游戏窗口焦点和权限等级。"; return false; }
    // Use a short high-resolution CPU spin rather than Sleep(), whose scheduler
    // granularity can inflate a 1 ms pulse into a much longer hold.
    LARGE_INTEGER frequency{}, started{}, current{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    const LONGLONG targetTicks = (std::max)(1ll,
        frequency.QuadPart * boundedPressMs / 1000);
    do {
        QueryPerformanceCounter(&current);
        _mm_pause();
    } while (current.QuadPart - started.QuadPart < targetTicks);
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
    if (SendInput(1,&input,sizeof(INPUT)) != 1) {
        // Make one best-effort release before returning a visible failure.
        SendInput(1,&input,sizeof(INPUT));
        error=L"释放按键失败；为避免输入持续按住，自动操作已停止。";
        return false;
    }
    return true;
}

struct AppState {
    HWND hwnd{}, combo{}, regionLabel{}, nextRegionLabel{}, nextStatusLabel{}, nextCountEdit{}, lookaheadEdit{};
    HWND scoreRegionLabel{}, scoreStatusLabel{}, againRegionButton{};
    HWND thresholdEdit{}, logEdit{}, preview{}, startButton{}, stopButton{}, statusLabel{}, modeCombo{};
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

HWND SelectedWindow(AppState* s);

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
        static_cast<unsigned long long>(written),aboveBaseline?L"高于基准，加入经验检索库":L"仅作结果记录，不加入经验检索库",
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


struct ScoreSpriteBlob {
    int left{}, top{}, width{}, height{};
    std::vector<int> pixels;
};

bool MatchScoreSpriteBlob(const std::vector<std::uint8_t>& candidate,
                          int width, int height, int& digitOut,
                          double& confidenceOut) {
    if (width < 3 || height < 6 ||
        candidate.size() != static_cast<std::size_t>(width) * height) return false;

    double bestScore = -1.0;
    double secondScore = -1.0;
    int bestDigit = -1;

    for (int digit = 0; digit < 10; ++digit) {
        int minX = 20, minY = 26, maxX = -1, maxY = -1, templateCount = 0;
        for (int y = 0; y < 26; ++y) {
            const std::uint32_t row = kScoreSpriteGlyphRows[digit][y];
            for (int x = 0; x < 20; ++x) {
                if ((row & (1u << x)) == 0) continue;
                minX = (std::min)(minX, x);
                minY = (std::min)(minY, y);
                maxX = (std::max)(maxX, x);
                maxY = (std::max)(maxY, y);
                ++templateCount;
            }
        }
        if (maxX < minX || maxY < minY || templateCount == 0) continue;

        const int templateWidth = maxX - minX + 1;
        const int templateHeight = maxY - minY + 1;
        const double scaleX = static_cast<double>(width) / templateWidth;
        const double scaleY = static_cast<double>(height) / templateHeight;
        const double largerScale = (std::max)(scaleX, scaleY);
        if (largerScale < 0.45 || largerScale > 3.0 ||
            std::abs(scaleX - scaleY) / largerScale > 0.34) continue;

        std::vector<std::uint8_t> scaled(static_cast<std::size_t>(width) * height, 0);
        int scaledCount = 0;
        for (int y = 0; y < height; ++y) {
            const int sourceY = minY + (std::min)(templateHeight - 1,
                (y * templateHeight) / height);
            for (int x = 0; x < width; ++x) {
                const int sourceX = minX + (std::min)(templateWidth - 1,
                    (x * templateWidth) / width);
                if ((kScoreSpriteGlyphRows[digit][sourceY] & (1u << sourceX)) != 0) {
                    scaled[static_cast<std::size_t>(y) * width + x] = 1;
                    ++scaledCount;
                }
            }
        }
        if (scaledCount == 0) continue;

        int matchedTemplate = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (!scaled[static_cast<std::size_t>(y) * width + x]) continue;
                bool nearCandidate = false;
                for (int dy = -1; dy <= 1 && !nearCandidate; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && nx < width && ny >= 0 && ny < height &&
                            candidate[static_cast<std::size_t>(ny) * width + nx]) {
                            nearCandidate = true;
                            break;
                        }
                    }
                }
                if (nearCandidate) ++matchedTemplate;
            }
        }

        int candidateCount = 0, matchedCandidate = 0;
        for (std::uint8_t value : candidate) if (value) ++candidateCount;
        if (candidateCount == 0) continue;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (!candidate[static_cast<std::size_t>(y) * width + x]) continue;
                bool nearTemplate = false;
                for (int dy = -1; dy <= 1 && !nearTemplate; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx >= 0 && nx < width && ny >= 0 && ny < height &&
                            scaled[static_cast<std::size_t>(ny) * width + nx]) {
                            nearTemplate = true;
                            break;
                        }
                    }
                }
                if (nearTemplate) ++matchedCandidate;
            }
        }

        const double recall = static_cast<double>(matchedTemplate) / scaledCount;
        const double precision = static_cast<double>(matchedCandidate) / candidateCount;
        const double score = (recall + precision) * 0.5;
        if (score > bestScore) {
            secondScore = bestScore;
            bestScore = score;
            bestDigit = digit;
        } else if (score > secondScore) {
            secondScore = score;
        }
    }

    const double margin = bestScore - (secondScore < 0.0 ? 0.0 : secondScore);
    if (bestDigit < 0 || bestScore < 0.76 || margin < 0.014) return false;
    digitOut = bestDigit;
    confidenceOut = bestScore;
    return true;
}

bool ReadScoreSpriteValue(const Rect& rect, int& scoreOut, int& confidenceOut,
                          std::string& observedText, bool* zeroVerified = nullptr) {
    if (zeroVerified) *zeroVerified = false;
    if (rect.width < 24 || rect.height < 10 ||
        rect.width > 1600 || rect.height > 240) return false;

    CapturedImage image;
    std::wstring captureError;
    if (!CaptureScreenRect(rect, image, captureError) ||
        image.width <= 0 || image.height <= 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height) {
        return false;
    }

    double bestAverage = -1.0;
    std::string bestDigits;
    const int thresholds[] = {145, 165, 185, 205};

    for (const int threshold : thresholds) {
        const int width = image.width, height = image.height;
        const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
        std::vector<std::uint8_t> bright(pixelCount, 0);
        for (std::size_t i = 0; i < pixelCount; ++i) {
            const Pixel& p = image.pixels[i];
            const int maximum = (std::max)({static_cast<int>(p.r), static_cast<int>(p.g), static_cast<int>(p.b)});
            const int minimum = (std::min)({static_cast<int>(p.r), static_cast<int>(p.g), static_cast<int>(p.b)});
            const int luminance = (299 * p.r + 587 * p.g + 114 * p.b) / 1000;
            if (luminance >= threshold && maximum - minimum <= 85) bright[i] = 1;
        }

        std::vector<std::uint8_t> visited(pixelCount, 0);
        std::vector<ScoreSpriteBlob> blobs;
        int maximumBlobHeight = 0;
        for (std::size_t start = 0; start < pixelCount; ++start) {
            if (!bright[start] || visited[start]) continue;
            ScoreSpriteBlob blob;
            blob.left = width;
            blob.top = height;
            int right = -1, bottom = -1;
            std::vector<int> stack;
            stack.push_back(static_cast<int>(start));
            visited[start] = 1;
            while (!stack.empty()) {
                const int index = stack.back();
                stack.pop_back();
                const int x = index % width, y = index / width;
                blob.pixels.push_back(index);
                blob.left = (std::min)(blob.left, x);
                blob.top = (std::min)(blob.top, y);
                right = (std::max)(right, x);
                bottom = (std::max)(bottom, y);
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
                        const int next = ny * width + nx;
                        if (bright[static_cast<std::size_t>(next)] && !visited[static_cast<std::size_t>(next)]) {
                            visited[static_cast<std::size_t>(next)] = 1;
                            stack.push_back(next);
                        }
                    }
                }
            }
            blob.width = right - blob.left + 1;
            blob.height = bottom - blob.top + 1;
            maximumBlobHeight = (std::max)(maximumBlobHeight, blob.height);
            blobs.push_back(std::move(blob));
        }

        blobs.erase(std::remove_if(blobs.begin(), blobs.end(),
            [maximumBlobHeight](const ScoreSpriteBlob& blob) {
                return blob.height < (std::max)(6, maximumBlobHeight * 55 / 100) ||
                       blob.pixels.size() < static_cast<std::size_t>((std::max)(8, maximumBlobHeight * 2));
            }), blobs.end());
        if (blobs.empty() || blobs.size() > 9) continue;

        std::sort(blobs.begin(), blobs.end(),
            [](const ScoreSpriteBlob& a, const ScoreSpriteBlob& b) { return a.left < b.left; });

        std::string digits;
        double totalConfidence = 0.0;
        bool allMatched = true;
        for (const ScoreSpriteBlob& blob : blobs) {
            std::vector<std::uint8_t> crop(static_cast<std::size_t>(blob.width) * blob.height, 0);
            for (int index : blob.pixels) {
                const int x = index % width - blob.left;
                const int y = index / width - blob.top;
                crop[static_cast<std::size_t>(y) * blob.width + x] = 1;
            }
            int digit = -1;
            double matchConfidence = 0.0;
            if (!MatchScoreSpriteBlob(crop, blob.width, blob.height, digit, matchConfidence)) {
                allMatched = false;
                break;
            }
            digits.push_back(static_cast<char>('0' + digit));
            totalConfidence += matchConfidence;
        }
        if (!allMatched || digits.empty()) continue;
        const double average = totalConfidence / digits.size();
        if (average > bestAverage) {
            bestAverage = average;
            bestDigits = std::move(digits);
        }
    }

    if (bestDigits.empty() || bestAverage < 0.80) return false;
    int parsed = 0;
    if (!ParseScoreDigits(bestDigits, parsed)) return false;

    scoreOut = parsed;
    confidenceOut = static_cast<int>(std::lround(bestAverage * 100.0));
    observedText = "sprite-template:" + bestDigits;
    if (zeroVerified) *zeroVerified = bestDigits == "0";
    return true;
}


bool IsSingleZeroScoreText(const std::string& text) {
    std::string digits;
    for (unsigned char c : text) {
        if (c >= '0' && c <= '9') digits.push_back(static_cast<char>(c));
        else if (c == ',' || c == '.' || c == ':' || c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        else return false;
    }
    return digits == "0";
}

// A lone zero often receives very low word-level confidence from Tesseract.
// Normal scores keep the 40/100 confidence gate. A low-confidence zero is
// accepted only when at least two independent segmentation modes agree exactly.
bool ReadScoreValue(tesseract::TessBaseAPI& ocr, const Rect& rect,
                    int& scoreOut, int& confidenceOut, std::string& observedText,
                    std::wstring& error, bool* zeroVerified = nullptr,
                    bool allowOcrFallback = true) {
    if (zeroVerified) *zeroVerified = false;
    if (ReadScoreSpriteValue(rect, scoreOut, confidenceOut, observedText, zeroVerified)) return true;
    if (!allowOcrFallback) {
        error = L"原生数字模板未能可靠匹配，且 Tesseract OCR 不可用。";
        return false;
    }
    std::string lineText;
    int lineConfidence = -1;
    if (!ReadOcrText(ocr, rect, "0123456789,:. ", tesseract::PSM_SINGLE_LINE,
                     lineText, error, &lineConfidence)) return false;
    observedText = lineText;
    confidenceOut = lineConfidence;

    int parsed = 0;
    if (lineConfidence >= 40 && ParseScoreDigits(lineText, parsed)) {
        scoreOut = parsed;
        return true;
    }

    int zeroVotes = IsSingleZeroScoreText(lineText) ? 1 : 0;
    int bestConfidence = lineConfidence;
    std::string wordText, charText;
    std::wstring fallbackError;
    int wordConfidence = -1, charConfidence = -1;
    if (ReadOcrText(ocr, rect, "0123456789", tesseract::PSM_SINGLE_WORD,
                    wordText, fallbackError, &wordConfidence) &&
        IsSingleZeroScoreText(wordText)) ++zeroVotes;
    bestConfidence = (std::max)(bestConfidence, wordConfidence);
    if (ReadOcrText(ocr, rect, "0123456789", tesseract::PSM_SINGLE_CHAR,
                    charText, fallbackError, &charConfidence) &&
        IsSingleZeroScoreText(charText)) ++zeroVotes;
    bestConfidence = (std::max)(bestConfidence, charConfidence);

    if (zeroVotes >= 2) {
        scoreOut = 0;
        confidenceOut = bestConfidence;
        observedText = lineText + " | word=" + wordText + " | char=" + charText;
        if (zeroVerified) *zeroVerified = true;
        return true;
    }
    if (error.empty()) error = L"OCR 未能以足够置信度解析分数；低置信度零分的多模式复核也未达成一致。";
    return false;
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

#include "input_calibration.inc"
#include "kick_wall_learning.inc"

void AppendTrainingCsv(AppState* s, int score, double learningRate, double epsilon, double temperature);

void BotThread(AppState* s, HWND target, Rect rect, Rect nextRect, int threshold,
               int previewCount, int lookaheadDepth, double epsilon, double temperature,
               int tapMs, int keyGapMs, int dropWaitMs, Rect scoreRect, Rect againRect,
               double learningRate, bool autoRetry, bool mathMode) {
    auto log=[&](const std::wstring& m){Log(s,m);};
    ScopedInputTimerResolution inputTimer;
    if (!inputTimer.Active())
        log(L"提示：无法将系统定时器调整到 1 ms；短点按仍会发送 key-down/key-up，但实际间隔可能受 Windows 调度精度影响。");
    log(Format(L"输入模式：离散点按；单次按下 %d ms、释放间隔 %d ms，单次点按周期不超过 20 ms；横移按目标列差值发送对应次数。",
        tapMs,keyGapMs));
    try {
        if(!IsWindow(target)) throw std::runtime_error("所选游戏窗口已关闭。");
        ShowWindow(target,SW_RESTORE);
