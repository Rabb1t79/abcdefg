    if (s->nextRect.width > 0) {
        const int count = static_cast<int>(ReadDoubleEdit(s->nextCountEdit,6,1,10));
        NextQueueResult queue; std::wstring queueError;
        if (ReadNextQueue(s->nextRect,count,queue,queueError)) {
            s->previewNextPieces=queue.pieces; s->hasNextPreview=true;
            SetWindowTextW(s->nextStatusLabel,(L"NEXT识别： "+FormatNextQueueConfidence(queue)).c_str());
            AppendLog(s,L"NEXT 队列识别成功："+FormatNextQueueConfidence(queue));
        } else {
            AppendLog(s,L"NEXT 当前区域识别失败："+queueError+L"；尝试自动重新定位整个布局。");
            if (AutoLocateRegions(s) && s->hasNextPreview) {
                AppendLog(s,L"NEXT 已通过自动重新定位恢复识别。");
            } else {
                s->hasNextPreview=false; s->previewNextPieces.clear();
                SetWindowTextW(s->nextStatusLabel,L"NEXT识别：失败");
                AppendLog(s,L"自动重新定位后 NEXT 仍未通过验证；本次不会使用猜测的队列。");
            }
        }
    } else {
        s->hasNextPreview=false; s->previewNextPieces.clear();
        SetWindowTextW(s->nextStatusLabel,L"NEXT识别：尚未框选");
        AppendLog(s,L"NEXT 尚未选择：请点击“框选 NEXT 区域”，只框住右侧预览卡片列。");
    }
}

void StartBot(AppState* s) {
    if(s->running.load())return;
    auto rejectStart=[&](const std::wstring& message) {
        AppendLog(s,L"未启动自动控制："+message);
        SetWindowTextW(s->statusLabel,L"状态：未启动（查看运行日志）");
    };
    const bool autoRetry=s->autoRetryEnabled;
    const bool newMemorySession=s->resetMemoryOnNextStart;
    const bool gpuMode=SendMessageW(s->modeCombo,CB_GETCURSEL,0,0)==1;
    const bool mathMode=true; // Benchmark build never updates training weights.
    if(s->worker.joinable())s->worker.join();
    HWND target=SelectedWindow(s);
    if(!target||!IsWindow(target)){rejectStart(L"请先从窗口列表中选择正在运行的游戏。");return;}
    std::wstring captureInitError;
    if (EnsureWindowCapture(target, captureInitError)) {
        AppendLog(s,L"已绑定游戏窗口；棋盘、NEXT 和 SCORE 区域将跟随窗口移动与缩放。");
    } else if (!captureInitError.empty()) {
        AppendLog(s,L"WGC 初始化失败，尝试桌面截图："+captureInitError);
    }
    const bool boardRebased=NormalizeCaptureRegion(s->boardRect,target);
    const bool nextRebased=NormalizeCaptureRegion(s->nextRect,target);
    const bool scoreRebased=NormalizeCaptureRegion(s->scoreRect,target);
    if (boardRebased || nextRebased || scoreRebased) SaveConfig(s);
    const bool boardRegionValid=IsCaptureRegionInsideClient(target,s->boardRect);
    const bool nextRegionValid=IsCaptureRegionInsideClient(target,s->nextRect);
    const bool scoreRegionValid=IsCaptureRegionInsideClient(target,s->scoreRect);
    if (!boardRegionValid || !nextRegionValid || !scoreRegionValid || IsLegacyAutoBoardRect(s->boardRect)) {
        AppendLog(s,L"发现识别区域缺失、失效或超出当前窗口，自动重新定位棋盘、NEXT 和 SCORE。");
        AutoLocateRegions(s);
        NormalizeCaptureRegion(s->boardRect,target);
        NormalizeCaptureRegion(s->nextRect,target);
        NormalizeCaptureRegion(s->scoreRect,target);
    }
    if(s->boardRect.width<=0){rejectStart(L"自动定位棋盘失败，请使用“框选棋盘区域”手动设置。");return;}
    if (autoRetry) {
        if (s->scoreRect.width < 24 || s->scoreRect.height < 10) {
            rejectStart(L"自动定位 SCORE 失败；请框选 SCORE 数字区域并测试识别。"); return;
        }
        if (s->againRect.width < 70 || s->againRect.height < 22) {
            rejectStart(L"自动重开需要先框选 Again 按钮内部区域。"); return;
        }
        if (!IsCaptureRegionInsideClient(target,s->scoreRect)) {
            rejectStart(L"SCORE 区域超出游戏窗口；请重新自动定位或手动框选。"); return;
        }
        if (!IsScreenRectInsideClient(target,s->againRect)) {
            rejectStart(L"Again 按钮区域不在游戏窗口内部；请重新框选。"); return;
        }
        if (!TestScoreRecognition(s)) {
            rejectStart(L"SCORE 识别测试未通过；请重新定位或手动框选数字区域。"); return;
        }
    }
    TestRecognition(s);
    if(!s->hasPreview || !s->previewData.hasActive){rejectStart(L"尚未识别到当前方块；请调整棋盘框选范围或颜色阈值后测试。");return;}
    if(!s->hasNextPreview){rejectStart(L"NEXT 队列识别未通过；请重新框选 NEXT 预览列并测试。");return;}
    AppendLog(s,L"识别预检通过；开始自动控制。运行中按 F8 或点击“停止”可紧急停止。");
    s->stop.store(false);s->running.store(true);
    EnableWindow(s->startButton,FALSE);EnableWindow(s->stopButton,TRUE);SetWindowTextW(s->statusLabel,L"状态：运行中（F8 紧急停止）");
    Rect rect=s->boardRect, nextRect=s->nextRect;int threshold=ReadThreshold(s);
    const int previewCount=static_cast<int>(ReadDoubleEdit(s->nextCountEdit,6,1,10));
    const int lookaheadDepth=2;
    constexpr double epsilon=0.05;
    constexpr double temperature=0.40;
    constexpr double learningRate=0.08;
    constexpr int tapMs=16;
    constexpr int keyGapMs=0;
    constexpr int dropWaitMs=0;
    SaveConfig(s);
    try {
        s->worker=std::thread(BotThread,s,target,rect,nextRect,threshold,previewCount,
            lookaheadDepth,epsilon,temperature,tapMs,keyGapMs,dropWaitMs,
            s->scoreRect,s->againRect,learningRate,autoRetry,mathMode,gpuMode,newMemorySession);
        if (newMemorySession) {
            s->resetMemoryOnNextStart=false;
            SetWindowTextW(s->newSessionButton,L"新局重置记忆");
        }
    } catch(...) {
        s->running.store(false);
        EnableWindow(s->startButton,TRUE);
        EnableWindow(s->stopButton,FALSE);
        SetWindowTextW(s->statusLabel,L"状态：启动失败（查看运行日志）");
        AppendLog(s,L"无法创建自动控制线程。");
    }
}

void StopBot(AppState* s) {
    if(s->running.load()){s->stop.store(true);AppendLog(s,L"已请求停止……");SetWindowTextW(s->statusLabel,L"状态：正在停止");}
}


std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), result.data(), needed, nullptr, nullptr);
    return result;
}

std::wstring MachineName(USHORT machine) {
    switch (machine) {
    case IMAGE_FILE_MACHINE_I386: return L"x86";
    case IMAGE_FILE_MACHINE_AMD64: return L"x64";
    case IMAGE_FILE_MACHINE_ARM64: return L"ARM64";
    case IMAGE_FILE_MACHINE_UNKNOWN: return L"native/default";
    default: return Format(L"machine 0x%04X", machine);
    }
}

void ExportClientReport(AppState* s) {
    HWND target = SelectedWindow(s);
    if (!target || !IsWindow(target)) {
        AppendLog(s, L"客户端信息导出失败：请先选择正在运行的游戏窗口。");
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    if (!pid) {
        AppendLog(s, L"客户端信息导出失败：无法取得目标窗口 PID。");
        return;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
        PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!process) {
        AppendLog(s, Format(L"客户端信息导出失败：OpenProcess 错误 %lu。", GetLastError()));
        return;
    }

    wchar_t exePath[32768]{};
    DWORD exePathLength = static_cast<DWORD>(_countof(exePath));
    if (!QueryFullProcessImageNameW(process, 0, exePath, &exePathLength)) {
        CloseHandle(process);
        AppendLog(s, Format(L"客户端信息导出失败：读取 EXE 路径错误 %lu。", GetLastError()));
        return;
    }

    std::wstring title(512, L'\0');
    const int titleLength = GetWindowTextW(target, title.data(),
        static_cast<int>(title.size()));
    if (titleLength >= 0) title.resize(static_cast<size_t>(titleLength));
    else title.clear();

    std::wostringstream report;
    report << L"Tetris Vision Pilot - read-only client inventory\n";
    report << L"Generated: " << GetTickCount64() << L" ms since system boot\n";
    report << L"Window HWND: 0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(target) << std::dec << L"\n";
    report << L"Window title: " << title << L"\n";
    report << L"PID: " << pid << L"\n";
    report << L"Executable: " << exePath << L"\n";

    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    auto isWow64Process2 = reinterpret_cast<IsWow64Process2Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    if (isWow64Process2) {
        USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        if (isWow64Process2(process, &processMachine, &nativeMachine)) {
            report << L"Process architecture: "
                << (processMachine == IMAGE_FILE_MACHINE_UNKNOWN
                    ? MachineName(nativeMachine) : MachineName(processMachine))
                << L" (native " << MachineName(nativeMachine) << L")\n";
        }
    } else {
        BOOL wow64 = FALSE;
        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);
        if (IsWow64Process(process, &wow64)) {
            report << L"Process architecture: "
                << (wow64 ? L"x86 under x64" :
                    (systemInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64
                        ? L"x64/native" : L"native/unknown")) << L"\n";
        }
    }

    report << L"\n[Loaded modules]\n";
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    size_t moduleCount = 0;
    if (snapshot != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W module{};
        module.dwSize = sizeof(module);
        if (Module32FirstW(snapshot, &module)) {
            do {
                report << L"Name: " << module.szModule
                    << L" | Base: 0x" << std::hex
                    << reinterpret_cast<std::uintptr_t>(module.modBaseAddr)
                    << L" | Size: 0x" << module.modBaseSize << std::dec
                    << L" | Path: " << module.szExePath << L"\n";
                ++moduleCount;
                module.dwSize = sizeof(module);
            } while (Module32NextW(snapshot, &module));
        }
        CloseHandle(snapshot);
    } else {
        report << L"Module snapshot failed, error " << GetLastError() << L"\n";
    }
    report << L"Module count: " << moduleCount << L"\n";

    report << L"\n[Committed MEM_IMAGE regions]\n";
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    std::uintptr_t cursor = reinterpret_cast<std::uintptr_t>(
        systemInfo.lpMinimumApplicationAddress);
    const std::uintptr_t maximum = reinterpret_cast<std::uintptr_t>(
        systemInfo.lpMaximumApplicationAddress);
    size_t imageRegionCount = 0;
    MEMORY_BASIC_INFORMATION mbi{};
    while (cursor < maximum && imageRegionCount < 50000) {
        const SIZE_T queried = VirtualQueryEx(process,
            reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi));
        if (!queried) break;
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const std::uintptr_t next = base + mbi.RegionSize;
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_IMAGE) {
            report << L"Base: 0x" << std::hex << base
                << L" | AllocationBase: 0x"
                << reinterpret_cast<std::uintptr_t>(mbi.AllocationBase)
                << L" | Size: 0x" << mbi.RegionSize
                << L" | Protect: 0x" << mbi.Protect << std::dec << L"\n";
            ++imageRegionCount;
        }
        if (next <= cursor) break;
        cursor = next;
    }
    report << L"Image region count: " << imageRegionCount << L"\n";
    report << L"\nNote: this report lists process/module metadata and memory-region "
        << L"layout only. It does not read or modify game memory.\n";
    CloseHandle(process);

    std::wstring reportPath = s->configPath;
    const size_t slash = reportPath.find_last_of(L"\\/");
    reportPath = (slash == std::wstring::npos ? L".\\" :
        reportPath.substr(0, slash + 1)) + L"TetrisVisionPilotBenchmark-client-report.txt";
    std::ofstream output(std::filesystem::path(reportPath), std::ios::binary);
    if (!output) {
        AppendLog(s, L"客户端信息已采集，但无法写入报告文件。");
        return;
    }
    output << "\xEF\xBB\xBF" << ToUtf8(report.str());
    output.close();
    AppendLog(s, L"客户端信息报告已导出：" + reportPath);
    AppendLog(s, Format(L"目标 PID %lu；枚举到 %llu 个模块、%llu 个 MEM_IMAGE 区域。"
        L"报告只包含模块与内存区域布局，不包含内存内容。",
        pid, static_cast<unsigned long long>(moduleCount),
        static_cast<unsigned long long>(imageRegionCount)));
}

void CreateUi(AppState* s) {
    HFONT font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto make=[&](DWORD ex,const wchar_t* cls,const wchar_t* text,DWORD style,
                  int x,int y,int w,int h,int id){
        HWND c=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,
            s->hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_instance,nullptr);
        if(c) SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        return c;
    };
    make(0,L"STATIC",L"Tetris Vision Pilot",SS_LEFT,20,12,400,28,0);
    make(0,L"STATIC",L"游戏窗口",SS_LEFT,20,48,80,24,0);
    s->combo=make(0,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,
        105,44,470,240,ID_WINDOW);
    make(0,L"BUTTON",L"刷新",BS_PUSHBUTTON,585,43,90,28,ID_REFRESH);
    make(0,L"BUTTON",L"框选棋盘",BS_PUSHBUTTON,690,43,125,28,ID_SELECT);
    make(0,L"BUTTON",L"自动定位",BS_PUSHBUTTON,830,43,125,28,ID_AUTO_LOCATE);
    make(0,L"BUTTON",L"导出客户端信息",BS_PUSHBUTTON,960,43,115,28,ID_CLIENT_REPORT);

    s->regionLabel=make(0,L"STATIC",L"棋盘：未设置",SS_LEFT,20,82,340,24,ID_REGION);
    make(0,L"BUTTON",L"框选 NEXT",BS_PUSHBUTTON,370,78,120,28,ID_SELECT_NEXT);
    s->nextRegionLabel=make(0,L"STATIC",L"NEXT：未设置",SS_LEFT,505,82,225,24,0);
    make(0,L"BUTTON",L"框选 SCORE",BS_PUSHBUTTON,745,78,120,28,ID_SELECT_SCORE);
    s->scoreRegionLabel=make(0,L"STATIC",L"SCORE：未设置",SS_LEFT,880,82,195,24,0);

    make(0,L"STATIC",L"识别阈值",SS_LEFT,20,118,75,24,0);
    s->thresholdEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"48",
        ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,100,114,48,26,ID_THRESHOLD);
    make(0,L"STATIC",L"模式",SS_LEFT,175,118,40,24,0);
    s->modeCombo=make(0,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,
        220,114,190,120,ID_MODE);
    SendMessageW(s->modeCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"CPU 多核"));
    SendMessageW(s->modeCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"GPU D3D11"));
    SendMessageW(s->modeCombo,CB_SETCURSEL,0,0);
    make(0,L"STATIC",L"NEXT 数量",SS_LEFT,435,118,65,24,0);
    s->nextCountEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"6",
        ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,505,114,42,26,ID_NEXT_COUNT);
    s->nextStatusLabel=make(0,L"STATIC",L"NEXT：未测试",SS_LEFT,565,118,205,24,0);
    make(0,L"BUTTON",L"测试 SCORE",BS_PUSHBUTTON,785,110,110,30,ID_TEST_SCORE);
    s->scoreStatusLabel=make(0,L"STATIC",L"SCORE：未测试",SS_LEFT,910,118,165,24,0);

    make(0,L"BUTTON",L"测试识别",BS_PUSHBUTTON,20,158,110,34,ID_TEST);
    s->startButton=make(0,L"BUTTON",L"开始自动落块",BS_PUSHBUTTON,140,158,140,34,ID_START);
    s->stopButton=make(0,L"BUTTON",L"停止",BS_PUSHBUTTON,290,158,80,34,ID_STOP);
    EnableWindow(s->stopButton,FALSE);
    s->statusLabel=make(0,L"STATIC",L"就绪",SS_LEFT,385,164,150,24,ID_STATUS);
    s->againRegionButton=make(0,L"BUTTON",L"设置 Again 区域",
        BS_PUSHBUTTON|WS_TABSTOP,550,158,140,30,ID_SELECT_AGAIN);
    s->autoRetryButton=make(0,L"BUTTON",L"自动重开：开",
        BS_PUSHBUTTON|WS_TABSTOP,705,158,115,30,ID_AUTO_RETRY_BUTTON);
    s->newSessionButton=make(0,L"BUTTON",L"新局重置记忆",
        BS_PUSHBUTTON|WS_TABSTOP,835,158,140,30,ID_NEW_SESSION);

    make(0,L"STATIC",L"本局分数",SS_LEFT,20,207,80,24,0);
    s->scoreEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"0",
        ES_AUTOHSCROLL|ES_NUMBER|ES_READONLY,105,203,90,27,ID_SCORE);
    make(0,L"BUTTON",L"打开性能日志",BS_PUSHBUTTON,215,201,130,32,ID_SUBMIT_SCORE);
    make(0,L"BUTTON",L"打开马拉松记忆",BS_PUSHBUTTON,355,201,145,32,ID_RESET_MODEL);

    make(0,L"STATIC",L"运行日志",SS_LEFT,20,244,400,22,0);
    make(0,L"STATIC",L"棋盘预览",SS_LEFT,805,244,260,22,0);
    s->logEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"",
        ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_HSCROLL,
        20,268,760,500,ID_LOG);
    s->preview=CreateWindowExW(WS_EX_CLIENTEDGE,L"TetrisPreview",L"",
        WS_CHILD|WS_VISIBLE,805,268,245,500,s->hwnd,
        reinterpret_cast<HMENU>(ID_PREVIEW),g_instance,nullptr);
    SetWindowLongPtrW(s->preview,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));

    s->learningRateEdit=nullptr;
    s->epsilonEdit=nullptr;
    s->temperatureEdit=nullptr;
    s->trainingStatsLabel=nullptr;

    AppendLog(s,L"程序已就绪。选择游戏窗口，自动定位或框选棋盘与 NEXT 后即可开始。");
    AppendLog(s,L"方向键固定 2 ms 点按，释放间隔 0 ms；硬降按下 2 ms，之后不添加固定落稳等待。");
    AppendLog(s,L"下一轮仍会等待新方块出生和棋盘一致性确认，确认后立即识别和搜索；失焦时暂停输入，切回后继续，F8 停止。");
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppState* s=reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);s=static_cast<AppState*>(cs->lpCreateParams);s->hwnd=hwnd;g_mainWindow=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    if(!s)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){
    case WM_CREATE:
        CreateUi(s);LoadConfig(s);UpdateRegionLabel(s);UpdateNextRegionLabel(s);UpdateAgainRegionLabel(s);UpdateTrainingStats(s);RefreshWindows(s);
        if(!RegisterHotKey(hwnd,HOTKEY_STOP,MOD_NOREPEAT,VK_F8)) AppendLog(s,L"提示：F8 全局热键注册失败；请使用界面停止按钮，并避免切换窗口。");
        return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)){
        case ID_REFRESH:RefreshWindows(s);return 0;
        case ID_CLIENT_REPORT:ExportClientReport(s);return 0;
        case ID_AUTO_LOCATE:AutoLocateRegions(s,true);return 0;
        case ID_NEW_SESSION:
            s->resetMemoryOnNextStart=!s->resetMemoryOnNextStart;
            SetWindowTextW(s->newSessionButton,s->resetMemoryOnNextStart
                ? L"已选：新局重置" : L"新局重置记忆");
            AppendLog(s,s->resetMemoryOnNextStart
                ? L"下次启动将创建新的马拉松记忆会话；旧档案不会删除。"
                : L"已取消新局重置；下次启动将自动恢复上次记忆。");
            return 0;
        case ID_AUTO_RETRY_BUTTON:
            s->autoRetryEnabled=!s->autoRetryEnabled;
            SetWindowTextW(s->autoRetryButton,s->autoRetryEnabled
                ? L"自动重开：开" : L"自动重开：关");
            WritePrivateProfileStringW(L"Automation",L"AutoRetry",
                s->autoRetryEnabled?L"1":L"0",s->configPath.c_str());
            AppendLog(s,s->autoRetryEnabled
                ? L"Game Over 后自动结算并点击已设置的 Again 区域。"
                : L"已关闭自动重开；Game Over 后等待手动处理。");
            return 0;
        case ID_SELECT:StartSelector(s,false);return 0;
        case ID_SELECT_NEXT:StartSelector(s,true);return 0;
        case ID_SELECT_SCORE:StartSelector(s,false,true);return 0;
        case ID_SELECT_AGAIN:StartSelector(s,false,false,true);return 0;
        case ID_TEST_SCORE:TestScoreRecognition(s);return 0;
        case ID_TEST:TestRecognition(s);return 0;
        case ID_START:StartBot(s);return 0;
        case ID_STOP:StopBot(s);return 0;
        case ID_SUBMIT_SCORE:OpenBenchmarkFile(s,false);return 0;
        case ID_RESET_MODEL:OpenBenchmarkFile(s,true);return 0;
        case ID_MODE:if(HIWORD(wp)==CBN_SELCHANGE){
            const bool gpu=SendMessageW(s->modeCombo,CB_GETCURSEL,0,0)==1;
            AppendLog(s,gpu?L"已选择 GPU Direct3D 11 Compute；无真实硬件 GPU 时会明确停止，不回退 CPU/WARP。":L"已选择 CPU 多核穷举；并行拆分首层候选并使用所有可用逻辑处理器。");
        }return 0;
        }break;
    case WM_HOTKEY:if(wp==HOTKEY_STOP)StopBot(s);return 0;
    case WM_APP_LOG:{std::unique_ptr<std::wstring> m(reinterpret_cast<std::wstring*>(lp));if(m)AppendLog(s,*m);return 0;}
    case WM_APP_REGION:{std::unique_ptr<RECT> r(reinterpret_cast<RECT*>(lp));if(r){Rect relative{};HWND target=SelectedWindow(s);if(target&&MakeClientRelativeRect(target,*r,relative))s->boardRect=relative;else s->boardRect={r->left,r->top,r->right-r->left,r->bottom-r->top};UpdateRegionLabel(s);SaveConfig(s);AppendLog(s,L"棋盘区域已保存为窗口关联坐标；移动游戏窗口后会自动跟随。");}return 0;}
    case WM_APP_NEXT_REGION:{std::unique_ptr<RECT> r(reinterpret_cast<RECT*>(lp));if(r){Rect relative{};HWND target=SelectedWindow(s);if(target&&MakeClientRelativeRect(target,*r,relative))s->nextRect=relative;else s->nextRect={r->left,r->top,r->right-r->left,r->bottom-r->top};UpdateNextRegionLabel(s);SaveConfig(s);AppendLog(s,L"NEXT 区域已保存为窗口关联坐标；移动游戏窗口后会自动跟随。");}return 0;}
    case WM_APP_SCORE_REGION:{std::unique_ptr<RECT> r(reinterpret_cast<RECT*>(lp));if(r){Rect relative{};HWND target=SelectedWindow(s);if(target&&MakeClientRelativeRect(target,*r,relative))s->scoreRect=relative;else s->scoreRect={r->left,r->top,r->right-r->left,r->bottom-r->top};UpdateNextRegionLabel(s);SaveConfig(s);AppendLog(s,L"SCORE 区域已保存为窗口关联坐标；移动游戏窗口后会自动跟随。");}return 0;}
    case WM_APP_AGAIN_REGION:{std::unique_ptr<RECT> r(reinterpret_cast<RECT*>(lp));if(r){s->againRect={r->left,r->top,r->right-r->left,r->bottom-r->top};UpdateAgainRegionLabel(s);SaveConfig(s);AppendLog(s,L"Again 按钮区域已保存。请只框选按钮内部，不要包含 Stop。");}return 0;}
    case WM_APP_FINISHED:
