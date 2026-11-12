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
    const bool autoRetry=SendMessageW(s->autoRetryCheck,BM_GETCHECK,0,0)==BST_CHECKED;
    const bool gpuMode=SendMessageW(s->modeCombo,CB_GETCURSEL,0,0)==1;
    const bool mathMode=true; // Benchmark build never updates training weights.
    if(s->worker.joinable())s->worker.join();
    HWND target=SelectedWindow(s);
    if(!target||!IsWindow(target)){MessageBoxW(s->hwnd,L"请从窗口列表中选择正在运行的游戏。",L"尚未选择游戏窗口",MB_ICONWARNING);return;}
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
    if (!boardRegionValid || !nextRegionValid || !scoreRegionValid) {
        AppendLog(s,L"发现识别区域缺失、失效或超出当前窗口，自动重新定位棋盘、NEXT 和 SCORE。");
        AutoLocateRegions(s);
        NormalizeCaptureRegion(s->boardRect,target);
        NormalizeCaptureRegion(s->nextRect,target);
        NormalizeCaptureRegion(s->scoreRect,target);
    }
    if(s->boardRect.width<=0){MessageBoxW(s->hwnd,L"自动定位棋盘失败。请使用“框选棋盘区域”手动设置。",L"尚未找到棋盘",MB_ICONWARNING);return;}
    if (autoRetry) {
        if (s->scoreRect.width < 24 || s->scoreRect.height < 10) {
            MessageBoxW(s->hwnd,L"自动定位 SCORE 失败。请手动框选 SCORE 数字区域，并通过识别测试。",L"尚未设置分数区域",MB_ICONWARNING); return;
        }
        if (s->againRect.width < 70 || s->againRect.height < 22) {
            MessageBoxW(s->hwnd,L"自动重开还需要框选 Again 按钮。请在 Game Over 画面点“框选 Again 按钮”，只框住按钮内部。",L"尚未选择 Again 按钮",MB_ICONWARNING); return;
        }
        if (!IsCaptureRegionInsideClient(target,s->scoreRect)) {
            MessageBoxW(s->hwnd,L"SCORE 区域超出当前游戏窗口；请重新自动定位或手动框选。",L"SCORE 区域不匹配",MB_ICONWARNING); return;
        }
        if (!IsScreenRectInsideClient(target,s->againRect)) {
            MessageBoxW(s->hwnd,L"Again 按钮区域不在所选游戏窗口内部；请重新框选 Again。",L"Again 区域不匹配",MB_ICONWARNING); return;
        }
        if (!TestScoreRecognition(s)) {
            MessageBoxW(s->hwnd,L"SCORE 识别测试未通过。请重新自动定位或手动框选数字区域。",L"分数识别未通过",MB_ICONWARNING); return;
        }
    }
    TestRecognition(s);
    if(!s->hasPreview || !s->previewData.hasActive){MessageBoxW(s->hwnd,L"尚未识别到当前方块。请调整棋盘框选范围或颜色阈值，再测试一次。",L"识别未通过",MB_ICONWARNING);return;}
    if(!s->hasNextPreview){MessageBoxW(s->hwnd,L"NEXT 队列识别未通过。请重新框选 NEXT 预览列、检查预览块数量，然后再次测试。",L"NEXT 识别未通过",MB_ICONWARNING);return;}
    if(MessageBoxW(s->hwnd,L"确认棋盘框选和识别结果正确，并允许程序向游戏窗口发送 ← / → / ↑ / Space？\n\n运行中按 F8 或点击“停止”可紧急停止。",L"确认自动控制",MB_OKCANCEL|MB_ICONWARNING)!=IDOK)return;
    s->stop.store(false);s->running.store(true);
    EnableWindow(s->startButton,FALSE);EnableWindow(s->stopButton,TRUE);SetWindowTextW(s->statusLabel,L"状态：运行中（F8 紧急停止）");
    Rect rect=s->boardRect, nextRect=s->nextRect;int threshold=ReadThreshold(s);
    const int previewCount=static_cast<int>(ReadDoubleEdit(s->nextCountEdit,6,1,10));
    const int lookaheadDepth=static_cast<int>(ReadDoubleEdit(s->lookaheadEdit,4,1,6));
    const double epsilon=ReadDoubleEdit(s->epsilonEdit,5.0,0.0,60.0)/100.0;
    const double temperature=ReadDoubleEdit(s->temperatureEdit,0.40,0.05,5.0);
    const double learningRate=ReadDoubleEdit(s->learningRateEdit,0.08,0.001,0.50);
    const int tapMs=static_cast<int>(ReadDoubleEdit(s->keyHoldEdit,1,1,4));
    int keyGapMs=static_cast<int>(ReadDoubleEdit(s->keyGapEdit,0,0,2));
    if (tapMs + keyGapMs > 6) {
        keyGapMs=(std::max)(0,6-tapMs);
        SetWindowTextW(s->keyGapEdit,Format(L"%d",keyGapMs).c_str());
    }
    const int dropWaitMs=static_cast<int>(ReadDoubleEdit(s->dropWaitEdit,5,0,40));
    SaveConfig(s);
    try { s->worker=std::thread(BotThread,s,target,rect,nextRect,threshold,previewCount,lookaheadDepth,epsilon,temperature,tapMs,keyGapMs,dropWaitMs,s->scoreRect,s->againRect,learningRate,autoRetry,mathMode,gpuMode); }
    catch(...) { s->running.store(false);EnableWindow(s->startButton,TRUE);EnableWindow(s->stopButton,FALSE);SetWindowTextW(s->statusLabel,L"状态：启动失败");MessageBoxW(s->hwnd,L"无法创建自动控制线程。",L"启动失败",MB_ICONERROR); }
}

void StopBot(AppState* s) {
    if(s->running.load()){s->stop.store(true);AppendLog(s,L"已请求停止……");SetWindowTextW(s->statusLabel,L"状态：正在停止");}
}

void CreateUi(AppState* s) {
    HFONT font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto make=[&](DWORD ex,const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id){
        HWND c=CreateWindowExW(ex,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,s->hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),g_instance,nullptr);
        if(c)SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;};
    make(0,L"STATIC",L"Tetris Vision Pilot  |  C++ 视觉控制 + 在线强化学习",SS_LEFT,20,14,760,30,0);
    make(0,L"STATIC",L"游戏窗口：",SS_LEFT,20,58,90,24,0);
    s->combo=make(0,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,105,54,550,240,ID_WINDOW);
    make(0,L"BUTTON",L"刷新窗口",BS_PUSHBUTTON,665,53,105,28,ID_REFRESH);
    make(0,L"BUTTON",L"框选棋盘区域",BS_PUSHBUTTON,780,53,150,28,ID_SELECT);
    make(0,L"BUTTON",L"自动定位全部",BS_PUSHBUTTON,935,53,140,28,ID_AUTO_LOCATE);
    s->regionLabel=make(0,L"STATIC",L"棋盘区域：尚未选择",SS_LEFT,20,95,360,24,ID_REGION);
    make(0,L"BUTTON",L"框选 NEXT 区域",BS_PUSHBUTTON,390,92,125,28,ID_SELECT_NEXT);
    s->nextRegionLabel=make(0,L"STATIC",L"NEXT区域：尚未选择",SS_LEFT,530,95,235,24,0);
    make(0,L"BUTTON",L"框选 SCORE 数字",BS_PUSHBUTTON,780,92,130,28,ID_SELECT_SCORE);
    s->scoreRegionLabel=make(0,L"STATIC",L"SCORE区域：未选择",SS_LEFT,920,95,155,24,0);
    make(0,L"STATIC",L"颜色差阈值：",SS_LEFT,20,130,90,24,0);
    s->thresholdEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"48",ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,110,126,55,26,ID_THRESHOLD);
    make(0,L"STATIC",L"（背景阈值）",SS_LEFT,175,130,100,24,0);
    make(0,L"STATIC",L"模式：",SS_LEFT,300,130,45,24,0);
    s->modeCombo=make(0,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,350,126,215,120,ID_MODE);
    SendMessageW(s->modeCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"CPU 多核穷举"));
    SendMessageW(s->modeCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"GPU Direct3D 11 计算"));
    SendMessageW(s->modeCombo,CB_SETCURSEL,0,0);
    make(0,L"STATIC",L"NEXT块数：",SS_LEFT,580,130,65,24,0);
    s->nextCountEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"6",ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,650,126,38,26,ID_NEXT_COUNT);
    make(0,L"STATIC",L"前瞻NEXT：",SS_LEFT,715,130,65,24,0);
    s->lookaheadEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"4",ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,785,126,38,26,ID_LOOKAHEAD);
    make(0,L"STATIC",L"点按",SS_LEFT,835,130,35,24,0);
    s->keyHoldEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"1",ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,870,126,38,26,ID_KEY_HOLD);
    make(0,L"STATIC",L"间隔",SS_LEFT,914,130,35,24,0);
    s->keyGapEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"0",ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,948,126,38,26,ID_KEY_GAP);
    make(0,L"STATIC",L"落稳",SS_LEFT,992,130,35,24,0);
    s->dropWaitEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"5",ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,1027,126,48,26,ID_DROP_WAIT);
    s->nextStatusLabel=make(0,L"STATIC",L"NEXT识别：尚未测试",SS_LEFT,600,176,175,24,0);
    make(0,L"BUTTON",L"测试 SCORE 识别",BS_PUSHBUTTON,785,170,125,32,ID_TEST_SCORE);
    s->scoreStatusLabel=make(0,L"STATIC",L"SCORE 识别：未测试",SS_LEFT,920,176,165,24,0);
    make(0,L"BUTTON",L"测试识别",BS_PUSHBUTTON,20,170,115,34,ID_TEST);
    s->startButton=make(0,L"BUTTON",L"开始自动落块",BS_PUSHBUTTON,145,170,145,34,ID_START);
    s->stopButton=make(0,L"BUTTON",L"停止",BS_PUSHBUTTON,300,170,90,34,ID_STOP);EnableWindow(s->stopButton,FALSE);
    s->statusLabel=make(0,L"STATIC",L"状态：就绪",SS_LEFT,410,176,175,24,ID_STATUS);
    make(0,L"STATIC",L"按键：← 左移　→ 右移　↑ 旋转　Space 硬降　｜　F8 停止",SS_LEFT,20,215,550,24,ID_HELP);
    s->againRegionButton=make(0,L"BUTTON",L"框选 Again 按钮",BS_PUSHBUTTON|WS_TABSTOP,580,212,145,27,ID_SELECT_AGAIN);
    s->autoRetryCheck=make(0,L"BUTTON",L"Game Over 后自动结算并点击 Again 重开",BS_AUTOCHECKBOX|WS_TABSTOP,735,212,340,27,ID_AUTO_RETRY);
    SendMessageW(s->autoRetryCheck,BM_SETCHECK,BST_CHECKED,0);

    // Training controls: reward comes from the real end-of-game score entered by the user.
    make(0,L"STATIC",L"成绩反馈学习（高于基准时正向强化；低分只记录原因，不反向惩罚权重）",SS_LEFT,20,252,900,22,0);
    make(0,L"STATIC",L"本局最终分数：",SS_LEFT,20,284,100,24,0);
    s->scoreEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"0",ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,120,280,90,27,ID_SCORE);
    make(0,L"BUTTON",L"提交成绩并学习",BS_PUSHBUTTON,225,278,145,32,ID_SUBMIT_SCORE);
    make(0,L"BUTTON",L"重置模型",BS_PUSHBUTTON,380,278,100,32,ID_RESET_MODEL);
    make(0,L"STATIC",L"学习率：",SS_LEFT,20,322,58,24,0);
    s->learningRateEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"0.08",ES_AUTOHSCROLL,82,318,55,26,ID_LEARNING_RATE);
    make(0,L"STATIC",L"探索率 %：",SS_LEFT,155,322,72,24,0);
    s->epsilonEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"5",ES_AUTOHSCROLL,230,318,50,26,ID_EPSILON);
    make(0,L"STATIC",L"温度：",SS_LEFT,300,322,48,24,0);
    s->temperatureEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"0.40",ES_AUTOHSCROLL,350,318,55,26,ID_TEMPERATURE);
    make(0,L"STATIC",L"学习率控制每次更新幅度；探索率控制尝试非当前最优动作的概率；温度控制策略随机性。",SS_LEFT,425,322,625,24,0);
    s->trainingStatsLabel=make(0,L"STATIC",L"模型状态：正在加载……",SS_LEFT,20,354,1030,55,ID_TRAIN_STATS);
    make(0,L"STATIC",L"算法说明：Softmax + ε 探索 + REINFORCE；当前落点会模拟 NEXT 队列，并用有限宽度 Beam Search 规划多步路径。",SS_LEFT,20,410,1040,24,0);

    make(0,L"STATIC",L"运行日志",SS_LEFT,20,443,500,22,0);
    s->logEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_HSCROLL,20,468,760,410,ID_LOG);
    make(0,L"STATIC",L"棋盘预览（绿：当前方块；蓝：已占用格）",SS_LEFT,805,443,260,22,0);
    s->preview=CreateWindowExW(WS_EX_CLIENTEDGE,L"TetrisPreview",L"",WS_CHILD|WS_VISIBLE,805,468,245,410,s->hwnd,reinterpret_cast<HMENU>(ID_PREVIEW),g_instance,nullptr);
    SetWindowLongPtrW(s->preview,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
    AppendLog(s,L"程序已就绪。请选择游戏窗口，分别框选棋盘和 NEXT 预览列，再测试识别。");
    AppendLog(s,L"训练说明：首次使用会载入规则生成的基础示范；每次落块记录动作样本；运行中自动缓存 SCORE，识别 GAME OVER 后自动结算（也可停止后手动提交成绩）。");
    AppendLog(s,L"模式可切换：模拟智能会使用基础示范、策略学习和经验记忆；纯数学穷举会遍历当前方块与最多两个 NEXT 方块的合法落点，不进行训练。");
    AppendLog(s,L"温和训练模式：成绩高于基准时做正向更新；低于基准时仅保存诊断记录，不执行负向权重更新。");
    AppendLog(s,L"输入节奏：方向键采用高精度独立点按；默认按下 1 ms、间隔 0 ms，启动时以 1–4 ms 校准；游戏实际响应仍受帧轮询限制。");
    AppendLog(s,L"自动结算：SCORE 优先使用游戏原生数字模板，匹配不确定时回退 OCR；GAME OVER 使用 OCR 检测。识别后冻结分数，勾选自动重开时会点击你框选的 Again 按钮。");
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppState* s=reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);s=static_cast<AppState*>(cs->lpCreateParams);s->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    if(!s)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){
    case WM_CREATE:
        CreateUi(s);LoadConfig(s);{const bool learning=SendMessageW(s->modeCombo,CB_GETCURSEL,0,0)==0;EnableWindow(s->lookaheadEdit,learning);EnableWindow(s->learningRateEdit,learning);EnableWindow(s->epsilonEdit,learning);EnableWindow(s->temperatureEdit,learning);EnableWindow(GetDlgItem(s->hwnd,ID_SUBMIT_SCORE),learning);EnableWindow(GetDlgItem(s->hwnd,ID_RESET_MODEL),learning);}UpdateRegionLabel(s);UpdateNextRegionLabel(s);UpdateAgainRegionLabel(s);s->trainer.Load(s->modelPath);s->experienceMemory.Load(s->experiencePath);UpdateTrainingStats(s);RefreshWindows(s);
        AppendLog(s,Format(L"已加载成功经验 %llu 条；只有高于本阶段基准的整局经验会成为后续决策的轻量参考。",
            static_cast<unsigned long long>(s->experienceMemory.Size())));
        if(!RegisterHotKey(hwnd,HOTKEY_STOP,MOD_NOREPEAT,VK_F8)) AppendLog(s,L"提示：F8 全局热键注册失败；请使用界面停止按钮，并避免切换窗口。");
        return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)){
        case ID_REFRESH:RefreshWindows(s);return 0;
        case ID_AUTO_LOCATE:AutoLocateRegions(s);return 0;
        case ID_SELECT:StartSelector(s,false);return 0;
        case ID_SELECT_NEXT:StartSelector(s,true);return 0;
        case ID_SELECT_SCORE:StartSelector(s,false,true);return 0;
        case ID_SELECT_AGAIN:StartSelector(s,false,false,true);return 0;
        case ID_TEST_SCORE:TestScoreRecognition(s);return 0;
        case ID_TEST:TestRecognition(s);return 0;
        case ID_START:StartBot(s);return 0;
        case ID_STOP:StopBot(s);return 0;
        case ID_SUBMIT_SCORE:SubmitTrainingScore(s);return 0;
        case ID_RESET_MODEL:ResetTrainingModel(s);return 0;
        case ID_MODE:if(HIWORD(wp)==CBN_SELCHANGE){
            const bool learning=SendMessageW(s->modeCombo,CB_GETCURSEL,0,0)==0;
            EnableWindow(s->lookaheadEdit,learning);
            EnableWindow(s->learningRateEdit,learning);
            EnableWindow(s->epsilonEdit,learning);
            EnableWindow(s->temperatureEdit,learning);
            EnableWindow(GetDlgItem(s->hwnd,ID_SUBMIT_SCORE),learning);
            EnableWindow(GetDlgItem(s->hwnd,ID_RESET_MODEL),learning);
            AppendLog(s,learning?L"已切换到模拟智能学习模式。":L"已切换到纯数学穷举模式；前瞻固定为当前方块 + 最多两个 NEXT，学习参数与训练操作已停用。");
        }return 0;
        }break;
    case WM_HOTKEY:if(wp==HOTKEY_STOP)StopBot(s);return 0;
    case WM_APP_LOG:{std::unique_ptr<std::wstring> m(reinterpret_cast<std::wstring*>(lp));if(m)AppendLog(s,*m);return 0;}
    case WM_APP_REGION:{std::unique_ptr<RECT> r(reinterpret_cast<RECT*>(lp));if(r){Rect relative{};HWND target=SelectedWindow(s);if(target&&MakeClientRelativeRect(target,*r,relative))s->boardRect=relative;else s->boardRect={r->left,r->top,r->right-r->left,r->bottom-r->top};UpdateRegionLabel(s);SaveConfig(s);AppendLog(s,L"棋盘区域已保存为窗口关联坐标；移动游戏窗口后会自动跟随。");}return 0;}
    case WM_APP_NEXT_REGION:{std::unique_ptr<RECT> r(reinterpret_cast<RECT*>(lp));if(r){Rect relative{};HWND target=SelectedWindow(s);if(target&&MakeClientRelativeRect(target,*r,relative))s->nextRect=relative;else s->nextRect={r->left,r->top,r->right-r->left,r->bottom-r->top};UpdateNextRegionLabel(s);SaveConfig(s);AppendLog(s,L"NEXT 区域已保存为窗口关联坐标；移动游戏窗口后会自动跟随。");}return 0;}
    case WM_APP_SCORE_REGION:{std::unique_ptr<RECT> r(reinterpret_cast<RECT*>(lp));if(r){Rect relative{};HWND target=SelectedWindow(s);if(target&&MakeClientRelativeRect(target,*r,relative))s->scoreRect=relative;else s->scoreRect={r->left,r->top,r->right-r->left,r->bottom-r->top};UpdateNextRegionLabel(s);SaveConfig(s);AppendLog(s,L"SCORE 区域已保存为窗口关联坐标；移动游戏窗口后会自动跟随。");}return 0;}
    case WM_APP_AGAIN_REGION:{std::unique_ptr<RECT> r(reinterpret_cast<RECT*>(lp));if(r){s->againRect={r->left,r->top,r->right-r->left,r->bottom-r->top};UpdateAgainRegionLabel(s);SaveConfig(s);AppendLog(s,L"Again 按钮区域已保存。请只框选按钮内部，不要包含 Stop。");}return 0;}
    case WM_APP_FINISHED:
