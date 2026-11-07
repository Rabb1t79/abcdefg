        if(s->worker.joinable())s->worker.join();
        s->running.store(false);
        EnableWindow(s->startButton,TRUE);EnableWindow(s->stopButton,FALSE);SetWindowTextW(s->statusLabel,L"状态：已停止");AppendLog(s,L"自动控制已结束。若本局已结束，请录入最终分数并提交学习。");UpdateTrainingStats(s);return 0;
    case WM_APP_TRAIN_STATS:UpdateTrainingStats(s);return 0;
    case WM_APP_SCORE_RECORDED:SetWindowTextW(s->scoreEdit,std::to_wstring(static_cast<int>(wp)).c_str());AppendLog(s,Format(L"OCR 自动读取本局最终分数：%d。",static_cast<int>(wp)));return 0;
    case WM_GETMINMAXINFO:{auto* m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={1100,930};return 0;}
    case WM_CLOSE:StopBot(s);if(s->worker.joinable())s->worker.join();s->running.store(false);s->trainer.Save(s->modelPath);DestroyWindow(hwnd);return 0;
    case WM_DESTROY:UnregisterHotKey(hwnd,HOTKEY_STOP);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE, PWSTR,int show) {
    g_instance=instance;
    SetProcessDPIAware();
    InitShapes();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSEXW previewClass{sizeof(WNDCLASSEXW)};previewClass.lpfnWndProc=PreviewProc;previewClass.hInstance=instance;previewClass.lpszClassName=L"TetrisPreview";previewClass.hCursor=LoadCursor(nullptr,IDC_ARROW);previewClass.hbrBackground=GetSysColorBrush(COLOR_WINDOW);RegisterClassExW(&previewClass);
    WNDCLASSEXW selectorClass{sizeof(WNDCLASSEXW)};selectorClass.lpfnWndProc=SelectorProc;selectorClass.hInstance=instance;selectorClass.lpszClassName=L"TetrisBoardSelector";selectorClass.hCursor=LoadCursor(nullptr,IDC_CROSS);selectorClass.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));RegisterClassExW(&selectorClass);
    WNDCLASSEXW mainClass{sizeof(WNDCLASSEXW)};mainClass.lpfnWndProc=MainProc;mainClass.hInstance=instance;mainClass.lpszClassName=L"TetrisVisionPilotMain";mainClass.hCursor=LoadCursor(nullptr,IDC_ARROW);mainClass.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);mainClass.hIcon=LoadIcon(nullptr,IDI_APPLICATION);RegisterClassExW(&mainClass);
    AppState state;
    wchar_t exe[MAX_PATH]{};GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring path=exe;size_t slash=path.find_last_of(L"\\/");std::wstring folder=(slash==std::wstring::npos?L".":path.substr(0,slash));state.configPath=folder+L"\\TetrisVisionPilot.ini";state.logPath=folder+L"\\TetrisVisionPilot.log";state.modelPath=folder+L"\\TetrisVisionPilot.model.ini";state.trainingCsvPath=folder+L"\\TetrisVisionPilot-training.csv";state.tessdataPath=folder+L"\\tessdata";
    HWND hwnd=CreateWindowExW(0,L"TetrisVisionPilotMain",L"Tetris Vision Pilot - C++ 视觉自动落块",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,1100,930,nullptr,nullptr,instance,&state);
    if(!hwnd){MessageBoxW(nullptr,Format(L"无法创建主窗口。Windows 错误码：%lu",GetLastError()).c_str(),L"Tetris Vision Pilot 启动失败",MB_ICONERROR);return 1;}
    ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    return static_cast<int>(msg.wParam);
}
