        if(bytes>0) {
            std::string utf8(static_cast<size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),utf8.data(),bytes,nullptr,nullptr);
            HANDLE file=CreateFileW(s->logPath.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file!=INVALID_HANDLE_VALUE) { DWORD written=0; WriteFile(file,utf8.data(),static_cast<DWORD>(utf8.size()),&written,nullptr); CloseHandle(file); }
        }
    }
    int len=GetWindowTextLengthW(s->logEdit);
    SendMessageW(s->logEdit,EM_SETSEL,len,len);
    SendMessageW(s->logEdit,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(s->logEdit,EM_SCROLLCARET,0,0);
}

void UpdateTrainingStats(AppState* s) {
    if (!s->trainingStatsLabel) return;
    SetWindowTextW(s->trainingStatsLabel,
        L"对照说明：CPU 使用多核并行；GPU 使用真实 D3D11 硬件计算着色器。GPU 初始化耗时单独统计，稳态搜索耗时不含首次创建设备。\r\n"
        L"马拉松记忆只在确认 NEXT 方块出生后提交锁定棋盘；若无法验证或发现未知新增格，会停止而不猜测。");
}

void OpenBenchmarkFile(AppState* s, bool memoryFile) {
    std::wstring path = memoryFile ? s->configPath : s->logPath;
    const size_t slash = path.find_last_of(L"\\/");
    path = (slash == std::wstring::npos ? L".\\" : path.substr(0, slash + 1)) +
        (memoryFile ? L"TetrisVisionPilotBenchmark-marathon-memory.csv" : L"TetrisVisionPilotBenchmark.log");
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(s->hwnd, memoryFile
            ? L"马拉松记忆档案尚未生成。请先开始一局并完成至少一次锁定。"
            : L"性能日志尚未生成。请先运行一次 CPU 或 GPU 测试。",
            L"文件尚不存在", MB_ICONINFORMATION);
        return;
    }
    const HINSTANCE result = ShellExecuteW(s->hwnd, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        MessageBoxW(s->hwnd, L"无法打开记录文件；请检查文件关联或权限。", L"打开失败", MB_ICONWARNING);
    }
}

void AppendTrainingCsv(AppState* s, int score, double learningRate, double epsilon, double temperature) {
    if (s->trainingCsvPath.empty()) return;
    bool writeHeader = GetFileAttributesW(s->trainingCsvPath.c_str()) == INVALID_FILE_ATTRIBUTES;
    const TrainerSnapshot snap = s->trainer.Snapshot();
    std::string line;
    if (writeHeader) line = "game,score,average_score,best_score,advantage,learning_rate,exploration_rate,temperature,w_height,w_holes,w_bump,w_max_height,w_wells,w_row_transitions,w_col_transitions,w_lines\r\n";
    char buffer[2048]{};
    sprintf_s(buffer, sizeof(buffer), "%llu,%d,%.3f,%d,%.6f,%.5f,%.5f,%.5f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\r\n",
        static_cast<unsigned long long>(snap.games), score, snap.averageScore, snap.bestScore,
        snap.lastAdvantage, learningRate, epsilon, temperature,
        snap.weights[0], snap.weights[1], snap.weights[2], snap.weights[3],
        snap.weights[4], snap.weights[5], snap.weights[6], snap.weights[7]);
    line += buffer;
    HANDLE file = CreateFileW(s->trainingCsvPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        CloseHandle(file);
    }
}

void SubmitTrainingScore(AppState* s) {
    if (s->running.load()) {
        MessageBoxW(s->hwnd, L"请先停止自动控制，或等本局结束后再提交最终分数。", L"本局仍在运行", MB_ICONINFORMATION);
        return;
    }
    wchar_t buffer[64]{};
    GetWindowTextW(s->scoreEdit, buffer, _countof(buffer));
    wchar_t* end = nullptr;
    long scoreValue = std::wcstol(buffer, &end, 10);
    if (end == buffer || scoreValue < 0 || scoreValue > 100000000) {
        MessageBoxW(s->hwnd, L"请输入有效的本局最终分数（0–100000000）。", L"分数无效", MB_ICONWARNING);
        return;
    }
    const double learningRate = ReadDoubleEdit(s->learningRateEdit, 0.08, 0.001, 0.50);
    const double epsilon = ReadDoubleEdit(s->epsilonEdit, 5.0, 0.0, 60.0) / 100.0;
    const double temperature = ReadDoubleEdit(s->temperatureEdit, 0.40, 0.05, 5.0);
    double advantage = 0.0;
    size_t samples = 0;
    std::wstring message;
    SaveCurrentEpisodeMemory(s,static_cast<int>(scoreValue));
    if (!s->trainer.SubmitScore(static_cast<int>(scoreValue), learningRate, advantage, samples, message)) {
        MessageBoxW(s->hwnd, message.c_str(), L"暂时无法训练", MB_ICONINFORMATION);
        return;
    }
    s->trainer.Save(s->modelPath);
    AppendTrainingCsv(s, static_cast<int>(scoreValue), learningRate, epsilon, temperature);
    AppendLog(s, message);
    AppendLog(s, Format(L"学习率 %.3f；探索率 %.1f%%；温度 %.2f。模型和训练记录已保存。",
        learningRate, epsilon * 100.0, temperature));
    SetWindowTextW(s->scoreEdit, L"0");
    UpdateTrainingStats(s);
}

void ResetTrainingModel(AppState* s) {
    if (s->running.load()) {
        MessageBoxW(s->hwnd, L"请先停止自动控制，再重置模型。", L"模型正在使用", MB_ICONINFORMATION);
        return;
    }
    if (MessageBoxW(s->hwnd, L"这会清除已学习权重、成绩统计和未结算样本，并恢复初始策略。是否继续？",
        L"重置训练模型", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    s->trainer.Reset();
    s->trainer.Save(s->modelPath);
    UpdateTrainingStats(s);
    AppendLog(s, L"模型已恢复初始权重；历史 CSV 不会删除。可从新的一局重新训练。");
}

void PaintPreview(HWND hwnd, AppState* s) {
    PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps);
    RECT rc{}; GetClientRect(hwnd,&rc);
    HBRUSH bg=CreateSolidBrush(RGB(28,30,34)); FillRect(dc,&rc,bg); DeleteObject(bg);
    int pad=8; int cell=(std::min)((rc.right-2*pad)/COLS,(rc.bottom-2*pad)/ROWS);
    int boardW=cell*COLS, boardH=cell*ROWS;
    int ox=(rc.right-boardW)/2, oy=(rc.bottom-boardH)/2;
    HPEN gridPen=CreatePen(PS_SOLID,1,RGB(65,68,74)); HGDIOBJ oldPen=SelectObject(dc,gridPen);
