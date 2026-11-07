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
        RECT hint{24,20,rc.right-24,58}; DrawTextW(dc,L"拖动鼠标框选 10 列 × 20 行棋盘内部区域；松开鼠标确认，按 Esc 取消。",-1,&hint,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
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
            if(r->right-r->left>=80 && r->bottom-r->top>=160) PostMessageW(g_mainWindow,WM_APP_REGION,0,reinterpret_cast<LPARAM>(r)); else delete r;
            DestroyWindow(hwnd);
        } return 0;
    case WM_KEYDOWN: if(wp==VK_ESCAPE){DestroyWindow(hwnd);return 0;} break;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

void StartSelector(AppState* s) {
    g_virtualLeft=GetSystemMetrics(SM_XVIRTUALSCREEN);g_virtualTop=GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    g_mainWindow=s->hwnd;
    HWND overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TOOLWINDOW,L"TetrisBoardSelector",L"选择棋盘区域",WS_POPUP,
        g_virtualLeft,g_virtualTop,width,height,nullptr,nullptr,g_instance,nullptr);
    if(!overlay){AppendLog(s,L"无法打开棋盘框选界面。");return;}
    SetLayeredWindowAttributes(overlay,0,105,LWA_ALPHA);
    ShowWindow(overlay,SW_SHOW);SetForegroundWindow(overlay);
    AppendLog(s,L"请拖动鼠标框选完整棋盘；按 Esc 可取消。");
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
    if(s->boardRect.width<=0){MessageBoxW(s->hwnd,L"请先点击“框选棋盘区域”。",L"尚未选择棋盘",MB_ICONWARNING);return;}
    CapturedImage image;std::wstring error;
    if(!CaptureScreenRect(s->boardRect,image,error)){MessageBoxW(s->hwnd,error.c_str(),L"截图失败",MB_ICONERROR);AppendLog(s,error);return;}
    VisionResult result=Analyze(image,ReadThreshold(s));s->previewData=result;s->hasPreview=true;InvalidateRect(s->preview,nullptr,TRUE);
    if(result.hasActive)AppendLog(s,Format(L"识别测试：占用格 %d；当前方块 %c 型，旋转 %d，位置列 %d、行 %d。",result.occupiedCount,result.active.name,result.active.rotation,result.active.minX,result.active.minY));
    else AppendLog(s,Format(L"识别测试：占用格 %d；没有识别到出生区四格方块。请检查框选范围和识别阈值。",result.occupiedCount));
