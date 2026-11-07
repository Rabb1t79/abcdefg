        SetForegroundWindow(target);
        Sleep(250);
        if(GetAncestor(GetForegroundWindow(),GA_ROOT)!=target) throw std::runtime_error("无法将游戏窗口切换到前台。请先手动点一下游戏窗口，再重新启动自动操作。");
        log(L"自动控制已启动。F8 或界面上的“停止”可紧急停止。请勿切换到其他窗口。");
        DWORD noPieceSince=GetTickCount();
        std::mt19937 rng(static_cast<unsigned int>(GetTickCount() ^ GetCurrentThreadId()));
        while(!s->stop.load()) {
            std::wstring capError;
            VisionResult vision=AnalyzeStable(rect,threshold,capError);
            if(!capError.empty()) throw std::runtime_error("屏幕截图失败："+std::string("请确认游戏窗口可见且棋盘区域有效。"));
            if(!vision.hasActive) {
                if(GetTickCount()-noPieceSince>8000) { log(L"连续 8 秒未识别到出生区方块，已停止。请检查棋盘框选范围和识别阈值。"); break; }
                SleepCheck(s->stop,120); continue;
            }
            noPieceSince=GetTickCount();
            Candidate piece=vision.active;
            SleepCheck(s->stop,85); if(s->stop.load())break;
            VisionResult check=AnalyzeStable(rect,threshold,capError);
            if(!capError.empty()) throw std::runtime_error("复核截图失败。");
            if(!check.hasActive || check.active.name!=piece.name || check.active.rotation!=piece.rotation || check.active.minX!=piece.minX) continue;
            piece=check.active;
            Board board=BoardWithoutActive(check);

            NextQueueResult queue;
            std::wstring queueError;
            if (!ReadNextQueue(nextRect,previewCount,queue,queueError)) {
                log(L"NEXT 识别不可靠，本次不执行落块："+queueError);
                SleepCheck(s->stop,180);
                continue;
            }
            PolicyDecision decision;
            if(!ChoosePolicyAction(board,piece.name,queue.pieces,lookaheadDepth,s->trainer,
                                  epsilon,temperature,rng,decision))
                throw std::runtime_error("策略模型未找到合法落点。棋盘可能已堆到顶部，或识别结果不正确。");
            Move move=decision.move;
            const auto& pieceRotations=g_rotations[piece.name];
            int rotateSteps=(move.rotation-piece.rotation+static_cast<int>(pieceRotations.size()))%static_cast<int>(pieceRotations.size());
            log(Format(L"当前 %c 型（稳定采样 %d/3）；NEXT=[%s]；前瞻 %d 块；目标旋转 %d、列 %d，路径评分 %.2f。",
                piece.name,check.activeStableSamples,FormatNextQueueConfidence(queue).c_str(),decision.searchedNextPieces,
                move.rotation,move.x,move.score));
            for(int i=0;i<rotateSteps && !s->stop.load();++i) {
                std::wstring err; if(!TapKey(target,VK_UP,42,err)) throw std::runtime_error("旋转按键失败或焦点丢失。");
                SleepCheck(s->stop,42);
            }
            if(s->stop.load())break;
            int currentX=piece.minX;
            if(rotateSteps>0) {
                SleepCheck(s->stop,125); if(s->stop.load())break;
                CapturedImage rotatedImage;
                if(CaptureScreenRect(rect,rotatedImage,capError)) {
                    VisionResult rotated=Analyze(rotatedImage,threshold,9);
                    if(rotated.hasActive && rotated.active.name==piece.name && rotated.active.rotation==move.rotation &&
                       std::abs(rotated.active.centerX-piece.centerX)<3.5) currentX=rotated.active.minX;
                    else log(L"旋转后复核未匹配，使用旋转前列位置估算；特殊踢墙规则可能导致偏差。");
                }
            }
            int delta=move.x-currentX;
            WORD moveKey=delta>0?VK_RIGHT:VK_LEFT;
            for(int i=0;i<std::abs(delta) && !s->stop.load();++i) {
                std::wstring err; if(!TapKey(target,moveKey,34,err)) throw std::runtime_error("移动按键失败或焦点丢失。");
                SleepCheck(s->stop,24);
            }
            if(s->stop.load())break;
            std::wstring err; if(!TapKey(target,VK_SPACE,45,err)) throw std::runtime_error("硬降按键失败或焦点丢失。");
            s->trainer.AddExperience(decision.experience);
            log(Format(L"已执行硬降；训练样本 +1（本局累计 %llu），预测本次消行 %d。",
                static_cast<unsigned long long>(s->trainer.Snapshot().pendingActions),decision.predictedLines));
            PostMessageW(s->hwnd,WM_APP_TRAIN_STATS,0,0);
            SleepCheck(s->stop,300);
        }
    } catch(const std::exception& e) {
        int n=MultiByteToWideChar(CP_UTF8,0,e.what(),-1,nullptr,0);
        std::wstring w(n>0?n:1,L'\0');
        if(n>0) { MultiByteToWideChar(CP_UTF8,0,e.what(),-1,w.data(),n); if(!w.empty() && w.back()==L'\0') w.pop_back(); }
        log(L"运行停止："+w);
    } catch(...) { log(L"运行停止：发生未知错误。"); }
    PostMessageW(s->hwnd,WM_APP_FINISHED,0,0);
}

void SaveConfig(AppState* s) {
    WritePrivateProfileStringW(L"Board",L"Left",Format(L"%d",s->boardRect.left).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Board",L"Top",Format(L"%d",s->boardRect.top).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Board",L"Width",Format(L"%d",s->boardRect.width).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Board",L"Height",Format(L"%d",s->boardRect.height).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"Left",Format(L"%d",s->nextRect.left).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"Top",Format(L"%d",s->nextRect.top).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"Width",Format(L"%d",s->nextRect.width).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"Height",Format(L"%d",s->nextRect.height).c_str(),s->configPath.c_str());
    wchar_t value[64]{};
    GetWindowTextW(s->nextCountEdit,value,_countof(value)); WritePrivateProfileStringW(L"Next",L"Count",value,s->configPath.c_str());
    GetWindowTextW(s->lookaheadEdit,value,_countof(value)); WritePrivateProfileStringW(L"Next",L"Lookahead",value,s->configPath.c_str());
    if (s->learningRateEdit) { GetWindowTextW(s->learningRateEdit,value,_countof(value)); WritePrivateProfileStringW(L"Training",L"LearningRate",value,s->configPath.c_str()); }
    if (s->epsilonEdit) { GetWindowTextW(s->epsilonEdit,value,_countof(value)); WritePrivateProfileStringW(L"Training",L"ExplorationRate",value,s->configPath.c_str()); }
    if (s->temperatureEdit) { GetWindowTextW(s->temperatureEdit,value,_countof(value)); WritePrivateProfileStringW(L"Training",L"Temperature",value,s->configPath.c_str()); }
}

void LoadConfig(AppState* s) {
    s->boardRect.left=GetPrivateProfileIntW(L"Board",L"Left",0,s->configPath.c_str());
    s->boardRect.top=GetPrivateProfileIntW(L"Board",L"Top",0,s->configPath.c_str());
    s->boardRect.width=GetPrivateProfileIntW(L"Board",L"Width",0,s->configPath.c_str());
    s->boardRect.height=GetPrivateProfileIntW(L"Board",L"Height",0,s->configPath.c_str());
    s->nextRect.left=GetPrivateProfileIntW(L"Next",L"Left",0,s->configPath.c_str());
    s->nextRect.top=GetPrivateProfileIntW(L"Next",L"Top",0,s->configPath.c_str());
    s->nextRect.width=GetPrivateProfileIntW(L"Next",L"Width",0,s->configPath.c_str());
    s->nextRect.height=GetPrivateProfileIntW(L"Next",L"Height",0,s->configPath.c_str());
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"Next",L"Count",L"6",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->nextCountEdit,value);
    GetPrivateProfileStringW(L"Next",L"Lookahead",L"4",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->lookaheadEdit,value);
    GetPrivateProfileStringW(L"Training",L"LearningRate",L"0.08",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->learningRateEdit,value);
    GetPrivateProfileStringW(L"Training",L"ExplorationRate",L"15",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->epsilonEdit,value);
    GetPrivateProfileStringW(L"Training",L"Temperature",L"0.80",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->temperatureEdit,value);
}

void UpdateRegionLabel(AppState* s) {
    if(s->boardRect.width>0) SetWindowTextW(s->regionLabel,Format(L"棋盘区域：X=%d  Y=%d  W=%d  H=%d",s->boardRect.left,s->boardRect.top,s->boardRect.width,s->boardRect.height).c_str());
    else SetWindowTextW(s->regionLabel,L"棋盘区域：尚未选择");
}

void UpdateNextRegionLabel(AppState* s) {
    if (s->nextRect.width > 0) SetWindowTextW(s->nextRegionLabel,
        Format(L"NEXT区域：X=%d Y=%d W=%d H=%d",s->nextRect.left,s->nextRect.top,s->nextRect.width,s->nextRect.height).c_str());
    else SetWindowTextW(s->nextRegionLabel,L"NEXT区域：尚未选择");
}

void AppendLog(AppState* s, const std::wstring& message) {
    SYSTEMTIME st{}; GetLocalTime(&st);
    std::wstring line=Format(L"[%02d:%02d:%02d] ",st.wHour,st.wMinute,st.wSecond)+message+L"\r\n";
    if(!s->logPath.empty()) {
        int bytes=WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),nullptr,0,nullptr,nullptr);
