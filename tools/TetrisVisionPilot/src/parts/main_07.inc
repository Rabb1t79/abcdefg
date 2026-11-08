        SetForegroundWindow(target);
        Sleep(250);
        if(GetAncestor(GetForegroundWindow(),GA_ROOT)!=target) throw std::runtime_error("无法将游戏窗口切换到前台。请先手动点一下游戏窗口，再重新启动自动操作。");
        log(L"自动控制已启动。F8 或界面上的“停止”可紧急停止。请勿切换到其他窗口。");
        tesseract::TessBaseAPI ocr;
        const std::string tessPath = WideToUtf8(s->tessdataPath);
        const int ocrInit = tessPath.empty() ? -1 : ocr.Init(tessPath.c_str(), "eng", tesseract::OEM_LSTM_ONLY);
        const bool ocrReady = (ocrInit == 0);
        if (!ocrReady) {
            log(L"OCR 初始化失败：请确认程序目录下 tessdata\\eng.traineddata 存在。自动读分和失败重试已停用。");
            autoRetry = false;
        } else {
            log(L"英文 OCR 已就绪：运行中读取 SCORE，Game Over 时自动结算。");
        }
        const Rect gameOverRect = ClientRelativeRect(target, 0.275, 0.08, 0.455, 0.17);
        DWORD noPieceSince=GetTickCount();
        DWORD lastScoreRead=0, lastGameOverRead=0;
        int latestScore=0, gameOverHits=0;
        bool hasLatestScore=false;
        std::mt19937 rng(static_cast<unsigned int>(GetTickCount() ^ GetCurrentThreadId()));
        while(!s->stop.load()) {
            const DWORD now = GetTickCount();
            // Check GAME OVER before reading SCORE. Freeze the latest live score after
            // a second title confirmation, before the result screen clears the counter.
            bool detectedGameOver = false;
            if (ocrReady && (now - lastGameOverRead >= 180)) {
                lastGameOverRead = now;
                std::string titleText; std::wstring ocrError;
                const bool titleHit = ReadOcrText(ocr, gameOverRect, "GAMEOVER ", tesseract::PSM_SINGLE_LINE, titleText, ocrError) &&
                                      IsGameOverText(titleText);
                if (titleHit) {
                    SleepCheck(s->stop,90);
                    std::string confirmText; std::wstring confirmError;
                    detectedGameOver = ReadOcrText(ocr, gameOverRect, "GAMEOVER ", tesseract::PSM_SINGLE_LINE, confirmText, confirmError) &&
                                       IsGameOverText(confirmText);
                }
            }
            if (!detectedGameOver && ocrReady && scoreRect.width>=24 && scoreRect.height>=10 &&
                (now-lastScoreRead>=120)) {
                lastScoreRead=now;
                std::string scoreText; std::wstring ocrError; int parsedScore=0; int scoreConfidence=0;
                if (ReadOcrText(ocr,scoreRect,"0123456789,:. ",tesseract::PSM_SINGLE_LINE,scoreText,ocrError,&scoreConfidence) &&
                    scoreConfidence>=40 && ParseScoreDigits(scoreText,parsedScore)) {
                    const bool plausible=!hasLatestScore || (parsedScore>=latestScore && parsedScore-latestScore<=5000);
                    if (plausible) {
                        const bool changed=!hasLatestScore || parsedScore!=latestScore;
                        latestScore=parsedScore; hasLatestScore=true;
                        if (changed) log(Format(L"SCORE 快照：%d（OCR 置信度 %d/100）。",latestScore,scoreConfidence));
                    }
                }
            }
            if (detectedGameOver) {
                log(L"检测到 GAME OVER，正在结算本局。");
                if (hasLatestScore) {
                    PostMessageW(s->hwnd, WM_APP_SCORE_RECORDED, static_cast<WPARAM>(latestScore), 0);
                    const TrainerSnapshot training = s->trainer.Snapshot();
                    if (training.pendingActions > 0) {
                        double advantage = 0.0; size_t samples = 0; std::wstring trainingMessage;
                        if (s->trainer.SubmitScore(latestScore, learningRate, advantage, samples, trainingMessage)) {
                            s->trainer.Save(s->modelPath);
                            log(trainingMessage);
                            PostMessageW(s->hwnd, WM_APP_TRAIN_STATS, 0, 0);
                        } else log(L"自动结算未执行训练："+trainingMessage);
                    } else log(L"已保存最终分数，但本局没有待结算的 AI 动作样本。");
                    AppendTrainingCsv(s, latestScore, learningRate, epsilon, temperature);
                } else {
                    log(L"检测到 GAME OVER，但运行期间没有读到可信分数；未猜测分数，也未自动重开。");
                    break;
                }
                if (!autoRetry) {
                    log(L"自动重试未启用；本局成绩已保存，等待手动开始下一局。");
                    break;
                }
                std::wstring clickError;
                if (!ClickAgainButton(target, clickError)) { log(L"自动重试失败："+clickError); break; }
                log(L"已点击 Again，正在确认 GAME OVER 弹窗已消失。");
                bool restarted=false;
                for(int attempt=0;attempt<25 && !s->stop.load();++attempt){
                    SleepCheck(s->stop,160);
                    std::string titleText; std::wstring titleError;
                    if(ReadOcrText(ocr,gameOverRect,"GAMEOVER ",tesseract::PSM_SINGLE_LINE,titleText,titleError) && !IsGameOverText(titleText)){restarted=true;break;}
                }
                if(!restarted){log(L"点击 Again 后 GAME OVER 弹窗仍在；请检查窗口大小和按钮位置。");break;}
                log(L"已确认新一局开始，重置分数缓存。");
                SleepCheck(s->stop,450);
                latestScore = 0; hasLatestScore = false;
                lastScoreRead = 0; lastGameOverRead = 0; gameOverHits = 0;
                noPieceSince = GetTickCount();
                continue;
            }

            std::wstring capError;
            VisionResult vision=AnalyzeStable(rect,threshold,capError);
            if(!capError.empty()) { log(L"屏幕截图失败："+capError); break; }
            if(!vision.hasActive) {
                if(GetTickCount()-noPieceSince>8000) { log(L"连续 8 秒未识别到出生区方块，已停止。请检查棋盘框选范围和识别阈值。"); break; }
                SleepCheck(s->stop,120); continue;
            }
            noPieceSince=GetTickCount();
            Candidate piece=vision.active;
            SleepCheck(s->stop,85); if(s->stop.load())break;
            VisionResult check=AnalyzeStable(rect,threshold,capError);
            if(!capError.empty()) { log(L"复核截图失败："+capError); break; }
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
                std::wstring err; if(!TapKey(target,VK_UP,keyHoldMs,err)) throw std::runtime_error("旋转按键失败或焦点丢失。");
                SleepCheck(s->stop,keyGapMs);
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
                std::wstring err; if(!TapKey(target,moveKey,keyHoldMs,err)) throw std::runtime_error("移动按键失败或焦点丢失。");
                SleepCheck(s->stop,keyGapMs);
            }
            if(s->stop.load())break;
            std::wstring err; if(!TapKey(target,VK_SPACE,keyHoldMs,err)) throw std::runtime_error("硬降按键失败或焦点丢失。");
            s->trainer.AddExperience(decision.experience);
            log(Format(L"已执行硬降；训练样本 +1（本局累计 %llu），预测本次消行 %d。",
                static_cast<unsigned long long>(s->trainer.Snapshot().pendingActions),decision.predictedLines));
            PostMessageW(s->hwnd,WM_APP_TRAIN_STATS,0,0);
            SleepCheck(s->stop,dropWaitMs);
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
    WritePrivateProfileStringW(L"Score",L"Left",Format(L"%d",s->scoreRect.left).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Score",L"Top",Format(L"%d",s->scoreRect.top).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Score",L"Width",Format(L"%d",s->scoreRect.width).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Score",L"Height",Format(L"%d",s->scoreRect.height).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Automation",L"AutoRetry",SendMessageW(s->autoRetryCheck,BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0",s->configPath.c_str());
    wchar_t value[64]{};
    GetWindowTextW(s->nextCountEdit,value,_countof(value)); WritePrivateProfileStringW(L"Next",L"Count",value,s->configPath.c_str());
    GetWindowTextW(s->lookaheadEdit,value,_countof(value)); WritePrivateProfileStringW(L"Next",L"Lookahead",value,s->configPath.c_str());
    if (s->keyHoldEdit) { GetWindowTextW(s->keyHoldEdit,value,_countof(value)); WritePrivateProfileStringW(L"Input",L"KeyHoldMs",value,s->configPath.c_str()); }
    if (s->keyGapEdit) { GetWindowTextW(s->keyGapEdit,value,_countof(value)); WritePrivateProfileStringW(L"Input",L"KeyGapMs",value,s->configPath.c_str()); }
    if (s->dropWaitEdit) { GetWindowTextW(s->dropWaitEdit,value,_countof(value)); WritePrivateProfileStringW(L"Input",L"DropWaitMs",value,s->configPath.c_str()); }
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
    s->scoreRect.left=GetPrivateProfileIntW(L"Score",L"Left",0,s->configPath.c_str());
    s->scoreRect.top=GetPrivateProfileIntW(L"Score",L"Top",0,s->configPath.c_str());
    s->scoreRect.width=GetPrivateProfileIntW(L"Score",L"Width",0,s->configPath.c_str());
    s->scoreRect.height=GetPrivateProfileIntW(L"Score",L"Height",0,s->configPath.c_str());
    SendMessageW(s->autoRetryCheck,BM_SETCHECK,GetPrivateProfileIntW(L"Automation",L"AutoRetry",1,s->configPath.c_str())?BST_CHECKED:BST_UNCHECKED,0);
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"Next",L"Count",L"6",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->nextCountEdit,value);
    GetPrivateProfileStringW(L"Next",L"Lookahead",L"4",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->lookaheadEdit,value);
    GetPrivateProfileStringW(L"Input",L"KeyHoldMs",L"26",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->keyHoldEdit,value);
    GetPrivateProfileStringW(L"Input",L"KeyGapMs",L"12",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->keyGapEdit,value);
    GetPrivateProfileStringW(L"Input",L"DropWaitMs",L"190",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->dropWaitEdit,value);
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
    if (s->scoreRect.width > 0) SetWindowTextW(s->scoreRegionLabel,Format(L"SCORE区域：%d×%d",s->scoreRect.width,s->scoreRect.height).c_str());
    else SetWindowTextW(s->scoreRegionLabel,L"SCORE区域：未选择");
}

void AppendLog(AppState* s, const std::wstring& message) {
    SYSTEMTIME st{}; GetLocalTime(&st);
    std::wstring line=Format(L"[%02d:%02d:%02d] ",st.wHour,st.wMinute,st.wSecond)+message+L"\r\n";
    if(!s->logPath.empty()) {
        int bytes=WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),nullptr,0,nullptr,nullptr);
