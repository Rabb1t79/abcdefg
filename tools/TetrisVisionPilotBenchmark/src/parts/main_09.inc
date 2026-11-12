    HGDIOBJ oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    for(int y=0;y<ROWS;++y)for(int x=0;x<COLS;++x){
        RECT r{ox+x*cell+1,oy+y*cell+1,ox+(x+1)*cell-1,oy+(y+1)*cell-1};
        bool active=false;
        if(s->hasPreview && s->previewData.hasActive) for(const auto& c:s->previewData.active.cells) if(c.x==x&&c.y==y)active=true;
        HBRUSH fill=CreateSolidBrush((s->hasPreview && s->previewData.occupied[y][x]) ? (active?RGB(90,205,135):RGB(92,130,190)) : RGB(42,45,51));
        FillRect(dc,&r,fill); DeleteObject(fill);
        Rectangle(dc,r.left,r.top,r.right,r.bottom);
    }
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(gridPen);
    SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(225,228,233));
    std::wstring caption=s->hasPreview?(s->previewData.hasActive?Format(L"当前方块：%c",s->previewData.active.name):L"未识别到当前方块"):L"点击“测试识别”查看棋盘";
    RECT textRc{6,4,rc.right-6,24}; DrawTextW(dc,caption.c_str(),-1,&textRc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    EndPaint(hwnd,&ps);
}

LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppState* s=reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_PAINT && s){PaintPreview(hwnd,s);return 0;}
    return DefWindowProcW(hwnd,msg,wp,lp);
}

LRESULT CALLBACK SelectorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps); RECT rc{}; GetClientRect(hwnd,&rc);
        HBRUSH black=CreateSolidBrush(RGB(0,0,0)); FillRect(dc,&rc,black); DeleteObject(black);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
        const wchar_t* hintText = g_selectAgainRegion
            ? L"框选 Game Over 画面中的 Again 按钮内部，不要包含 Stop 或按钮外框；松开鼠标确认，按 Esc 取消。"
            : (g_selectScoreRegion
                ? L"框选游戏中实际分数的数字区域：只包含数字，不要包含 SCORE 标题或旁边计数器；松开鼠标确认，按 Esc 取消。"
                : (g_selectNextRegion
                    ? L"框选 NEXT 预览卡片区域：包含从上到下的全部预览方块，不要包含 NEXT 标题、箭头或外侧灰色边框；松开鼠标确认，按 Esc 取消。"
                    : L"框选 10 列 × 20 行棋盘内部区域；不要包含 NEXT、HOLD 或棋盘外框；松开鼠标确认，按 Esc 取消。"));
        RECT hint{24,20,rc.right-24,72};
        DrawTextW(dc,hintText,-1,&hint,DT_LEFT|DT_VCENTER|DT_WORDBREAK);
        if(g_selectDragging) {
            RECT r{(std::min)(g_selectStart.x,g_selectCurrent.x),(std::min)(g_selectStart.y,g_selectCurrent.y),
                   (std::max)(g_selectStart.x,g_selectCurrent.x),(std::max)(g_selectStart.y,g_selectCurrent.y)};
            HPEN pen=CreatePen(PS_SOLID,3,RGB(255,70,70)); HGDIOBJ oldPen=SelectObject(dc,pen);
            HGDIOBJ oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH)); Rectangle(dc,r.left,r.top,r.right,r.bottom);
            SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
            std::wstring dims=Format(L"%d × %d px",r.right-r.left,r.bottom-r.top);
            RECT tr{r.left,r.bottom+5,r.left+200,r.bottom+30}; DrawTextW(dc,dims.c_str(),-1,&tr,DT_LEFT|DT_SINGLELINE);
        }
        EndPaint(hwnd,&ps); return 0;
    }
    case WM_LBUTTONDOWN:
        g_selectStart={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; g_selectCurrent=g_selectStart; g_selectDragging=true; SetCapture(hwnd); InvalidateRect(hwnd,nullptr,TRUE); return 0;
    case WM_MOUSEMOVE:
        if(g_selectDragging){g_selectCurrent={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};InvalidateRect(hwnd,nullptr,TRUE);} return 0;
    case WM_LBUTTONUP:
        if(g_selectDragging){
            g_selectCurrent={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};g_selectDragging=false;ReleaseCapture();
            RECT* r=new RECT{}; r->left=g_virtualLeft+(std::min)(g_selectStart.x,g_selectCurrent.x); r->top=g_virtualTop+(std::min)(g_selectStart.y,g_selectCurrent.y);
            r->right=g_virtualLeft+(std::max)(g_selectStart.x,g_selectCurrent.x); r->bottom=g_virtualTop+(std::max)(g_selectStart.y,g_selectCurrent.y);
            const UINT message=g_selectAgainRegion?WM_APP_AGAIN_REGION:(g_selectScoreRegion?WM_APP_SCORE_REGION:(g_selectNextRegion?WM_APP_NEXT_REGION:WM_APP_REGION));
            const int minWidth=g_selectAgainRegion?70:(g_selectScoreRegion?24:(g_selectNextRegion?30:80));
            const int minHeight=g_selectAgainRegion?22:(g_selectScoreRegion?10:(g_selectNextRegion?80:160));
            if(r->right-r->left>=minWidth && r->bottom-r->top>=minHeight) PostMessageW(g_mainWindow,message,0,reinterpret_cast<LPARAM>(r)); else delete r;
            DestroyWindow(hwnd);
        } return 0;
    case WM_KEYDOWN: if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;} break;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

void StartSelector(AppState* s, bool nextRegion, bool scoreRegion=false, bool againRegion=false) {
    g_selectNextRegion=nextRegion;
    g_selectScoreRegion=scoreRegion;
    g_selectAgainRegion=againRegion;
    g_virtualLeft=GetSystemMetrics(SM_XVIRTUALSCREEN);g_virtualTop=GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    g_mainWindow=s->hwnd;
    const wchar_t* windowTitle = againRegion ? L"选择 Again 按钮区域" : (scoreRegion ? L"选择 SCORE 数字区域" : (nextRegion ? L"选择 NEXT 预览区域" : L"选择棋盘区域"));
    HWND overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TOOLWINDOW,L"TetrisBoardSelector",windowTitle,WS_POPUP,
        g_virtualLeft,g_virtualTop,width,height,nullptr,nullptr,g_instance,nullptr);
    if(!overlay){AppendLog(s,L"无法打开棋盘框选界面。");return;}
    SetLayeredWindowAttributes(overlay,0,105,LWA_ALPHA);
    ShowWindow(overlay,SW_SHOW);SetForegroundWindow(overlay);
    if (againRegion)
        AppendLog(s,L"请框选 Game Over 画面中的 Again 按钮内部，不要包含 Stop 或按钮外框。");
    else if (scoreRegion)
        AppendLog(s,L"请框选游戏中实际分数的数字区域：只包含数字，不要包含 SCORE 标题或旁边计数器。");
    else if (nextRegion)
        AppendLog(s,L"请框选 NEXT 预览卡片列：从最上方到最下方的全部预览方块；不要包含 NEXT 标题、箭头或灰色外框。");
    else
        AppendLog(s,L"请框选完整的 10×20 棋盘内部区域；不要包含 NEXT、HOLD 或棋盘外框。");
}

bool TestScoreRecognition(AppState* s) {
    HWND target = SelectedWindow(s);
    std::wstring captureInitError;
    if (target && EnsureWindowCapture(target, captureInitError)) {
        AppendLog(s, L"捕获后端：Windows Graphics Capture（WGC）窗口捕获；遮挡窗口不会混入游戏画面。");
    } else if (!captureInitError.empty()) {
        AppendLog(s, L"WGC 初始化失败，将回退桌面截图（可能受遮挡影响）："+captureInitError);
    }
    const bool scoreRegionValid = target && NormalizeCaptureRegion(s->scoreRect, target) &&
        IsCaptureRegionInsideClient(target, s->scoreRect);
    if (scoreRegionValid) SaveConfig(s);
    if (!scoreRegionValid || s->scoreRect.width < 24 || s->scoreRect.height < 10) {
        AppendLog(s, L"未设置 SCORE 区域，正在按 Tetris Online 布局自动定位。");
        AutoLocateRegions(s);
        if (target && NormalizeCaptureRegion(s->scoreRect, target)) SaveConfig(s);
    }
    if (s->scoreRect.width < 24 || s->scoreRect.height < 10) {
        AppendLog(s, L"SCORE 区域仍不可用；请手动框选 SCORE 数字区域。");
        return false;
    }
    tesseract::TessBaseAPI ocr;
    const std::string tessPath = WideToUtf8(s->tessdataPath);
    const bool ocrReady = !tessPath.empty() && ocr.Init(tessPath.c_str(), "eng", tesseract::OEM_LSTM_ONLY) == 0;
    std::string text; std::wstring error; int confidence = 0; int score = 0;
    bool zeroVerified = false;
    const bool parsed = ReadScoreValue(ocr,s->scoreRect,score,confidence,text,error,&zeroVerified,ocrReady);
    if (ocrReady) ocr.End();
    const bool spriteMatch = text.rfind("sprite-template:", 0) == 0;
    if (!parsed) {
        AppendLog(s, Format(L"SCORE 识别未通过：置信度 %d/100，数字模板和 OCR 都未能可信解析。请只框选数字区域。", confidence));
        if (!error.empty()) AppendLog(s,L"诊断："+error);
        SetWindowTextW(s->scoreStatusLabel, L"SCORE 识别：失败");
        return false;
    }
    const wchar_t* method = spriteMatch ? L"原生模板" : L"OCR 回退";
    SetWindowTextW(s->scoreStatusLabel, Format(L"SCORE 识别：%d（%s，%d）", score, method, confidence).c_str());
    AppendLog(s, Format(L"SCORE 识别测试成功：%d 分；来源：%s；置信度 %d/100%s。", score, method, confidence,
        zeroVerified ? (spriteMatch ? L"；零分由模板确认" : L"；零分已通过多模式复核") : L""));
    return true;
}

BOOL CALLBACK EnumWindowProc(HWND hwnd, LPARAM param) {
    AppState* s=reinterpret_cast<AppState*>(param);
    if(!IsWindowVisible(hwnd)||hwnd==s->hwnd||GetWindow(hwnd,GW_OWNER))return TRUE;
    DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);if(pid==GetCurrentProcessId())return TRUE;
    int len=GetWindowTextLengthW(hwnd);if(len<2||len>256)return TRUE;
    std::wstring title(static_cast<size_t>(len)+1,L'\0');GetWindowTextW(hwnd,title.data(),len+1);title.resize(len);
    int index=static_cast<int>(SendMessageW(s->combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str())));
    if(index>=0)SendMessageW(s->combo,CB_SETITEMDATA,index,reinterpret_cast<LPARAM>(hwnd));
    return TRUE;
}

void RefreshWindows(AppState* s) {
    SendMessageW(s->combo,CB_RESETCONTENT,0,0);
    EnumWindows(EnumWindowProc,reinterpret_cast<LPARAM>(s));
    int count=static_cast<int>(SendMessageW(s->combo,CB_GETCOUNT,0,0));
    AppendLog(s,Format(L"已列出 %d 个可见窗口；请选择俄罗斯方块游戏窗口。",count));
}

HWND SelectedWindow(AppState* s) {
    LRESULT index=SendMessageW(s->combo,CB_GETCURSEL,0,0); if(index==CB_ERR)return nullptr;
    return reinterpret_cast<HWND>(SendMessageW(s->combo,CB_GETITEMDATA,index,0));
}

void TestRecognition(AppState* s) {
    HWND target = SelectedWindow(s);
    std::wstring captureInitError;
    if (target && EnsureWindowCapture(target, captureInitError)) {
        AppendLog(s, L"捕获后端：WGC 窗口捕获已就绪；采集的是所选游戏窗口，不读取桌面合成画面。");
    } else if (!captureInitError.empty()) {
        AppendLog(s, L"WGC 初始化失败，将回退桌面截图（可能受遮挡影响）："+captureInitError);
    }
    if (target) {
        const bool boardRebased = NormalizeCaptureRegion(s->boardRect, target);
        const bool nextRebased = NormalizeCaptureRegion(s->nextRect, target);
        if (boardRebased || nextRebased) SaveConfig(s);
    }
    const bool boardRegionValid = target && IsCaptureRegionInsideClient(target, s->boardRect);
    const bool nextRegionValid = target && IsCaptureRegionInsideClient(target, s->nextRect);
    const bool scoreRegionValid = target && IsCaptureRegionInsideClient(target, s->scoreRect);
    if (s->boardRect.width <= 0 || s->nextRect.width <= 0 ||
        !boardRegionValid || !nextRegionValid || !scoreRegionValid) {
        AppendLog(s, L"棋盘或 NEXT 区域未设置，正在自动定位全部识别区域。");
        AutoLocateRegions(s);
        if (target) {
            NormalizeCaptureRegion(s->boardRect, target);
            NormalizeCaptureRegion(s->nextRect, target);
            NormalizeCaptureRegion(s->scoreRect, target);
        }
    }
    if(s->boardRect.width<=0){MessageBoxW(s->hwnd,L"自动定位失败，请尝试手动框选棋盘区域。",L"尚未找到棋盘",MB_ICONWARNING);return;}
    if(s->boardRect.width<80 || s->boardRect.height<160) {
        MessageBoxW(s->hwnd,Format(L"棋盘区域只有 %d × %d px；10×20 棋盘至少需要 80 × 160 px。请重新框选棋盘。",
            s->boardRect.width,s->boardRect.height).c_str(),L"棋盘区域尺寸不正确",MB_ICONWARNING);
        return;
    }
    std::wstring error;
    VisionResult result=AnalyzeStable(s->boardRect,ReadThreshold(s),error);
    if(!error.empty()){MessageBoxW(s->hwnd,error.c_str(),L"截图失败",MB_ICONERROR);AppendLog(s,error);return;}
    s->previewData=result;s->hasPreview=true;InvalidateRect(s->preview,nullptr,TRUE);
    if(result.hasActive)AppendLog(s,Format(L"识别测试：占用格 %d；自适应阈值 %d；当前方块 %c 型，旋转 %d，位置列 %d、行 %d；轨迹采样 %d/3。",
        result.occupiedCount,result.usedThreshold,result.active.name,result.active.rotation,result.active.minX,result.active.minY,result.activeStableSamples));
    else AppendLog(s,Format(L"识别测试：占用格 %d；自适应阈值 %d；当前方块暂未锁定到活动轨迹（三帧轨迹匹配 %d/3）。程序会继续追踪，不把一次视觉丢帧当作按键失败。",
        result.occupiedCount,result.usedThreshold,result.activeStableSamples));
