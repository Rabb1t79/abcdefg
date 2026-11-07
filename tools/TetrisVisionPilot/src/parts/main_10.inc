}

void StartBot(AppState* s) {
    if(s->running.load())return;
    if(s->worker.joinable())s->worker.join();
    const TrainerSnapshot modelState = s->trainer.Snapshot();
    if (modelState.pendingActions > 0) {
        const std::wstring warning = Format(L"还有上一轮未提交的 %llu 个动作样本。若继续同一局，可以保留；若上一局已结束，应先提交那一局的最终分数。是否仍要启动？",
            static_cast<unsigned long long>(modelState.pendingActions));
        if (MessageBoxW(s->hwnd, warning.c_str(), L"存在未结算训练样本", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    }
    if(s->boardRect.width<=0){MessageBoxW(s->hwnd,L"请先框选棋盘区域。",L"尚未选择棋盘",MB_ICONWARNING);return;}
    HWND target=SelectedWindow(s);
    if(!target||!IsWindow(target)){MessageBoxW(s->hwnd,L"请从窗口列表中选择正在运行的游戏。",L"尚未选择游戏窗口",MB_ICONWARNING);return;}
    TestRecognition(s);
    if(!s->hasPreview || !s->previewData.hasActive){MessageBoxW(s->hwnd,L"尚未识别到当前方块。请调整棋盘框选范围或颜色阈值，再测试一次。",L"识别未通过",MB_ICONWARNING);return;}
    if(MessageBoxW(s->hwnd,L"确认棋盘框选和识别结果正确，并允许程序向游戏窗口发送 ← / → / ↑ / Space？\n\n运行中按 F8 或点击“停止”可紧急停止。",L"确认自动控制",MB_OKCANCEL|MB_ICONWARNING)!=IDOK)return;
    s->stop.store(false);s->running.store(true);
    EnableWindow(s->startButton,FALSE);EnableWindow(s->stopButton,TRUE);SetWindowTextW(s->statusLabel,L"状态：运行中（F8 紧急停止）");
    Rect rect=s->boardRect;int threshold=ReadThreshold(s);
    const double epsilon=ReadDoubleEdit(s->epsilonEdit,15.0,0.0,60.0)/100.0;
    const double temperature=ReadDoubleEdit(s->temperatureEdit,0.80,0.05,5.0);
    SaveConfig(s);
    try { s->worker=std::thread(BotThread,s,target,rect,threshold,epsilon,temperature); }
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
    s->regionLabel=make(0,L"STATIC",L"棋盘区域：尚未选择",SS_LEFT,20,95,1000,24,ID_REGION);
    make(0,L"STATIC",L"颜色差阈值：",SS_LEFT,20,130,90,24,0);
    s->thresholdEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"48",ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,110,126,55,26,ID_THRESHOLD);
    make(0,L"STATIC",L"（建议 35–70；背景与方块颜色越接近，阈值越需要调低）",SS_LEFT,175,130,480,24,0);
    make(0,L"BUTTON",L"测试识别",BS_PUSHBUTTON,20,170,115,34,ID_TEST);
    s->startButton=make(0,L"BUTTON",L"开始自动落块",BS_PUSHBUTTON,145,170,145,34,ID_START);
    s->stopButton=make(0,L"BUTTON",L"停止",BS_PUSHBUTTON,300,170,90,34,ID_STOP);EnableWindow(s->stopButton,FALSE);
    s->statusLabel=make(0,L"STATIC",L"状态：就绪",SS_LEFT,410,176,400,24,ID_STATUS);
    make(0,L"STATIC",L"按键：← 左移　→ 右移　↑ 顺时针旋转　Space 硬降　｜　F8 紧急停止",SS_LEFT,20,215,760,24,ID_HELP);

    // Training controls: reward comes from the real end-of-game score entered by the user.
    make(0,L"STATIC",L"在线强化学习（每局结束后录入实际分数，模型再更新）",SS_LEFT,20,252,800,22,0);
    make(0,L"STATIC",L"本局最终分数：",SS_LEFT,20,284,100,24,0);
    s->scoreEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"0",ES_AUTOHSCROLL|ES_NUMBER|WS_TABSTOP,120,280,90,27,ID_SCORE);
    make(0,L"BUTTON",L"提交成绩并学习",BS_PUSHBUTTON,225,278,145,32,ID_SUBMIT_SCORE);
    make(0,L"BUTTON",L"重置模型",BS_PUSHBUTTON,380,278,100,32,ID_RESET_MODEL);
    make(0,L"STATIC",L"学习率：",SS_LEFT,20,322,58,24,0);
    s->learningRateEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"0.08",ES_AUTOHSCROLL,82,318,55,26,ID_LEARNING_RATE);
    make(0,L"STATIC",L"探索率 %：",SS_LEFT,155,322,72,24,0);
    s->epsilonEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"15",ES_AUTOHSCROLL,230,318,50,26,ID_EPSILON);
    make(0,L"STATIC",L"温度：",SS_LEFT,300,322,48,24,0);
    s->temperatureEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"0.80",ES_AUTOHSCROLL,350,318,55,26,ID_TEMPERATURE);
    make(0,L"STATIC",L"学习率控制每次更新幅度；探索率控制尝试非当前最优动作的概率；温度控制策略随机性。",SS_LEFT,425,322,625,24,0);
    s->trainingStatsLabel=make(0,L"STATIC",L"模型状态：正在加载……",SS_LEFT,20,354,1030,55,ID_TRAIN_STATS);
    make(0,L"STATIC",L"算法说明：Softmax + ε 探索 + REINFORCE。每局实际分数作为回报，和历史平均分比较后强化/削弱本局动作偏好。",SS_LEFT,20,410,1040,24,0);

    make(0,L"STATIC",L"运行日志",SS_LEFT,20,443,500,22,0);
    s->logEdit=make(WS_EX_CLIENTEDGE,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_HSCROLL,20,468,760,410,ID_LOG);
    make(0,L"STATIC",L"棋盘预览（绿：当前方块；蓝：已占用格）",SS_LEFT,805,443,260,22,0);
    s->preview=CreateWindowExW(WS_EX_CLIENTEDGE,L"TetrisPreview",L"",WS_CHILD|WS_VISIBLE,805,468,245,410,s->hwnd,reinterpret_cast<HMENU>(ID_PREVIEW),g_instance,nullptr);
    SetWindowLongPtrW(s->preview,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));
    AppendLog(s,L"程序已就绪。请选择游戏窗口、框选棋盘，再测试识别。");
    AppendLog(s,L"训练说明：每次落块产生一个动作样本；整局结束后在“本局最终分数”中录入实际成绩并提交。");
    AppendLog(s,L"算法不是大型神经网络，而是可解释的线性策略模型；界面显示的 8 个权重会根据成绩逐局调整并保存在本地。");
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppState* s=reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);s=static_cast<AppState*>(cs->lpCreateParams);s->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    if(!s)return DefWindowProcW(hwnd,msg,wp,lp);
    switch(msg){
    case WM_CREATE:
        CreateUi(s);LoadConfig(s);UpdateRegionLabel(s);s->trainer.Load(s->modelPath);UpdateTrainingStats(s);RefreshWindows(s);
        if(!RegisterHotKey(hwnd,HOTKEY_STOP,MOD_NOREPEAT,VK_F8)) AppendLog(s,L"提示：F8 全局热键注册失败；请使用界面停止按钮，并避免切换窗口。");
        return 0;
    case WM_COMMAND:
        switch(LOWORD(wp)){
        case ID_REFRESH:RefreshWindows(s);return 0;
        case ID_SELECT:StartSelector(s);return 0;
        case ID_TEST:TestRecognition(s);return 0;
        case ID_START:StartBot(s);return 0;
        case ID_STOP:StopBot(s);return 0;
        case ID_SUBMIT_SCORE:SubmitTrainingScore(s);return 0;
        case ID_RESET_MODEL:ResetTrainingModel(s);return 0;
        }break;
    case WM_HOTKEY:if(wp==HOTKEY_STOP)StopBot(s);return 0;
    case WM_APP_LOG:{std::unique_ptr<std::wstring> m(reinterpret_cast<std::wstring*>(lp));if(m)AppendLog(s,*m);return 0;}
    case WM_APP_REGION:{std::unique_ptr<RECT> r(reinterpret_cast<RECT*>(lp));if(r){s->boardRect={r->left,r->top,r->right-r->left,r->bottom-r->top};UpdateRegionLabel(s);SaveConfig(s);AppendLog(s,L"棋盘区域已保存。请确认框选区域正好覆盖 10 列 × 20 行格子内部。");}return 0;}
    case WM_APP_FINISHED:
