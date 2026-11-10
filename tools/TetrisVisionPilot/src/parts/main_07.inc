        SetForegroundWindow(target);
        Sleep(250);
        if(GetAncestor(GetForegroundWindow(),GA_ROOT)!=target) throw std::runtime_error("无法将游戏窗口切换到前台。请先手动点一下游戏窗口，再重新启动自动操作。");
        log(L"自动控制已启动。F8 或界面上的“停止”可紧急停止。请勿切换到其他窗口。");
        tesseract::TessBaseAPI ocr;
        const std::string tessPath = WideToUtf8(s->tessdataPath);
        const int ocrInit = tessPath.empty() ? -1 : ocr.Init(tessPath.c_str(), "eng", tesseract::OEM_LSTM_ONLY);
        const bool ocrReady = (ocrInit == 0);
        if (!ocrReady) {
            log(L"OCR 初始化失败：原生数字模板仍可读取 SCORE；GAME OVER 文字检测与自动重试已停用。");
            autoRetry = false;
        } else {
            log(L"SCORE 优先使用游戏原生数字模板，无法可靠匹配时回退 OCR；GAME OVER 使用 OCR 检测。");
        }
        const Rect gameOverRect = ClientRelativeRect(target, 0.275, 0.08, 0.455, 0.17);
        DWORD noPieceSince=GetTickCount();
        bool pendingActionValidation=false;
        TrainingExperience pendingExperience{};
        Board pendingExpectedBoard{};
        DWORD lastScoreRead=0, lastGameOverRead=0;
        int latestScore=0, gameOverHits=0;
        bool hasLatestScore=false;
        bool scoreResetObserved=false;
        std::mt19937 rng(static_cast<unsigned int>(GetTickCount() ^ GetCurrentThreadId()));
        while(!s->stop.load()) {
            const DWORD now = GetTickCount();

            // Sample SCORE before checking GAME OVER. The game may clear its score
            // during the transition, so only non-decreasing readings may replace
            // the last trusted snapshot from the active game.
            if (scoreRect.width>=24 && scoreRect.height>=10 &&
                (now-lastScoreRead>=120)) {
                lastScoreRead=now;
                std::string scoreText; std::wstring ocrError;
                int parsedScore=0, scoreConfidence=0;
                bool zeroVerified=false;
                if (ReadScoreValue(ocr,scoreRect,parsedScore,scoreConfidence,scoreText,ocrError,&zeroVerified,ocrReady)) {
                    const bool plausible=!hasLatestScore ||
                        (parsedScore>=latestScore && parsedScore-latestScore<=5000);
                    if (plausible) {
                        const bool changed=!hasLatestScore || parsedScore!=latestScore;
                        latestScore=parsedScore;
                        hasLatestScore=true;
                        if (changed) {
                            const bool spriteMatch = scoreText.rfind("sprite-template:", 0) == 0;
                            const wchar_t* method = spriteMatch ? L"原生数字模板" : L"OCR";
                            const wchar_t* zeroNote = zeroVerified
                                ? (spriteMatch ? L"，零分由数字模板确认" : L"，零分已通过多模式复核")
                                : L"";
                            log(Format(L"SCORE 快照：%d（%s 置信度 %d/100%s）。",
                                latestScore, method, scoreConfidence, zeroNote));
                        }
                    } else if (parsedScore==0 && latestScore>0 && !scoreResetObserved) {
                        scoreResetObserved=true;
                        log(Format(L"检测到分数显示已提前清零；保留最后可信分数 %d，不用 0 覆盖本局成绩。",latestScore));
                    }
                }
            }

            // Run the two-pass GAME OVER check only after this cycle's SCORE sample.
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

            if (detectedGameOver) {
                log(L"检测到 GAME OVER，正在结算本局。");
                if (pendingActionValidation) {
                    log(L"最后一次硬降未等到下一块棋盘验证；该动作不计入训练样本。");
                    pendingActionValidation=false;
                }
                if (hasLatestScore) {
                    PostMessageW(s->hwnd, WM_APP_SCORE_RECORDED, static_cast<WPARAM>(latestScore), 0);
                    const TrainerSnapshot training = s->trainer.Snapshot();
                    if (training.pendingActions > 0) {
                        SaveCurrentEpisodeMemory(s,latestScore);
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
                if (!ClickAgainButton(target, againRect, clickError)) { log(L"自动重试失败："+clickError); break; }
                log(L"已点击 Again，正在确认 GAME OVER 弹窗已消失。");
                bool restarted=false;
                for(int attempt=0;attempt<25 && !s->stop.load();++attempt){
                    SleepCheck(s->stop,160);
                    std::string titleText; std::wstring titleError;
                    const bool titleCleared = ReadOcrText(ocr,gameOverRect,"GAMEOVER ",tesseract::PSM_SINGLE_LINE,titleText,titleError) &&
                                              !IsGameOverText(titleText);
                    if (!titleCleared) continue;
                    // Do not treat an OCR miss as a restart: the playfield must also
                    // contain a newly spawned active tetromino before retry continues.
                    std::wstring boardError;
                    const VisionResult newGame = AnalyzeStable(rect,threshold,boardError);
                    if (boardError.empty() && newGame.hasActive) { restarted=true; break; }
                }
                if(!restarted){log(L"点击 Again 后未确认新方块出现；已停止自动重试，避免在旧弹窗上重复点击。");break;}
                log(L"已确认新一局开始，重置分数缓存。");
                SleepCheck(s->stop,450);
                latestScore = 0; hasLatestScore = false;
                scoreResetObserved = false;
                lastScoreRead = 0; lastGameOverRead = 0; gameOverHits = 0;
                noPieceSince = GetTickCount();
                continue;
            }

            std::wstring capError;
            VisionResult vision=AnalyzeStable(rect,threshold,capError);
            if(!capError.empty()) { log(L"屏幕截图失败："+capError); break; }

            // A hard-dropped piece should fill the ghost footprint. Compare the
            // resulting fixed board immediately; this can confirm the action
            // before a new active piece appears at the spawn point.
            if (pendingActionValidation &&
                BoardWithoutActive(vision) == pendingExpectedBoard) {
                s->trainer.AddExperience(pendingExperience);
                pendingActionValidation=false;
                log(Format(L"落点锁定验证通过：实际棋盘与预测棋盘一致；%s；训练样本 +1（本局累计 %llu）。",
                    vision.hasActive?L"已排除新活动方块":L"无需等待新方块出生",
                    static_cast<unsigned long long>(s->trainer.Snapshot().pendingActions)));
                PostMessageW(s->hwnd,WM_APP_TRAIN_STATS,0,0);
            }

            if(!vision.hasActive) {
                if(GetTickCount()-noPieceSince>8000) { log(L"连续 8 秒未识别到出生区方块，已停止。请检查棋盘框选范围和识别阈值。"); break; }
                SleepCheck(s->stop,120); continue;
            }
            noPieceSince=GetTickCount();
            Candidate piece=vision.active;
            SleepCheck(s->stop,25); if(s->stop.load())break;
            VisionResult check=AnalyzeStable(rect,threshold,capError);
            if(!capError.empty()) { log(L"复核截图失败："+capError); break; }
            if(!check.hasActive || check.active.name!=piece.name || check.active.rotation!=piece.rotation || check.active.minX!=piece.minX) continue;
            piece=check.active;
            Board board=BoardWithoutActive(check);
            if (pendingActionValidation) {
                if (board == pendingExpectedBoard) {
                    s->trainer.AddExperience(pendingExperience);
                    log(Format(L"落点验证通过：实际棋盘与预测棋盘一致；训练样本 +1（本局累计 %llu）。",
                        static_cast<unsigned long long>(s->trainer.Snapshot().pendingActions)));
                } else {
                    log(L"落点验证未通过：实际棋盘与预测棋盘不一致；本次动作不加入训练，避免错误归因。");
                }
                pendingActionValidation=false;
                PostMessageW(s->hwnd,WM_APP_TRAIN_STATS,0,0);
            }

            NextQueueResult queue;
            std::wstring queueError;
            if (!ReadNextQueue(nextRect,previewCount,queue,queueError)) {
                log(L"NEXT 识别不可靠，本次不执行落块："+queueError);
                SleepCheck(s->stop,180);
                continue;
            }
            PolicyDecision decision{};
            Move move{};
            bool trainableAction = !mathMode;
            if (mathMode) {
                ExhaustiveDecision exhaustive{};
                if (!ChooseExhaustiveAction(board,piece.name,piece.rotation,piece.minX,queue.pieces,exhaustive))
                    throw std::runtime_error("穷举搜索未找到合法落点。棋盘可能已堆到顶部，或识别结果不正确。");
                move = exhaustive.move;
                decision.move = exhaustive.move;
                decision.expectedBoard = exhaustive.expectedBoard;
                decision.predictedLines = exhaustive.predictedLines;
                decision.searchedNextPieces = exhaustive.searchedNextPieces;
                log(Format(L"穷举完成：节点 %llu；耗时 %.0f ms；前瞻当前方块 + %d 个 NEXT；旋转按键估算 %d 次、横移 %d 次；目标旋转 %d、列 %d。",
                    static_cast<unsigned long long>(exhaustive.nodes), exhaustive.elapsedMs,
                    exhaustive.searchedNextPieces, exhaustive.rotationTaps, exhaustive.horizontalTaps,
                    move.rotation, move.x));
            } else {
                if(!ChoosePolicyAction(board,piece.name,queue.pieces,lookaheadDepth,s->trainer,s->experienceMemory,
                                      epsilon,temperature,rng,decision))
                    throw std::runtime_error("策略模型未找到合法落点。棋盘可能已堆到顶部，或识别结果不正确。");
                move = decision.move;
            }
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
                SleepCheck(s->stop,55);
                if(s->stop.load())break;
                VisionResult rotated=AnalyzeStable(rect,threshold,capError);
                if(!capError.empty()) { log(L"旋转后复核截图失败："+capError); break; }
                bool usedGhost=false;
                if(!ReadPieceColumn(rotated,piece.name,move.rotation,currentX,usedGhost)) {
                    log(L"旋转后无法通过活动方块或 Ghost 确认目标朝向；取消本次落块并重新规划。");
                    SleepCheck(s->stop,100);
                    continue;
                }
                if(usedGhost) log(Format(L"旋转复核：Ghost 确认 %c 型、旋转 %d、落点列 %d。",
                    piece.name,move.rotation,currentX));
            }

            int delta=move.x-currentX;
            WORD moveKey=delta>0?VK_RIGHT:VK_LEFT;
            for(int i=0;i<std::abs(delta) && !s->stop.load();++i) {
                std::wstring err;
                if(!TapKey(target,moveKey,keyHoldMs,err)) throw std::runtime_error("移动按键失败或焦点丢失。");
                SleepCheck(s->stop,keyGapMs);
            }
            if(s->stop.load())break;

            // Verify the actual screen position before hard drop. A key pulse is not
            // proof that the game advanced exactly one column.
            SleepCheck(s->stop,45);
            if(s->stop.load())break;
            VisionResult positioned=AnalyzeStable(rect,threshold,capError);
            if(!capError.empty()) { log(L"横移后位置复核截图失败："+capError); break; }
            int observedX=0;
            bool usedGhost=false;
            if(!ReadPieceColumn(positioned,piece.name,move.rotation,observedX,usedGhost)) {
                log(L"横移后无法通过活动方块或 Ghost 确认当前位置；取消本次硬降，不记录训练样本。");
                SleepCheck(s->stop,100);
                continue;
            }
            if(usedGhost) log(Format(L"Ghost 落点反馈：旋转 %d、落点列 %d。",
                positioned.ghost.rotation,positioned.ghost.minX));

            int correction=move.x-observedX;
            if(correction!=0) {
                log(Format(L"横移复核发现偏差：目标列 %d，实际落点列 %d；尝试按反馈纠正。",
                    move.x,observedX));
                const WORD correctionKey=correction>0?VK_RIGHT:VK_LEFT;
                for(int i=0;i<std::abs(correction) && !s->stop.load();++i) {
                    std::wstring err;
                    if(!TapKey(target,correctionKey,keyHoldMs,err)) throw std::runtime_error("位置纠正按键失败或焦点丢失。");
                    SleepCheck(s->stop,keyGapMs);
                }
                if(s->stop.load())break;
                SleepCheck(s->stop,45);
                if(s->stop.load())break;
                VisionResult corrected=AnalyzeStable(rect,threshold,capError);
                if(!capError.empty()) { log(L"位置纠正后复核截图失败："+capError); break; }
                int correctedX=0;
                bool correctedByGhost=false;
                if(!ReadPieceColumn(corrected,piece.name,move.rotation,correctedX,correctedByGhost) ||
                   correctedX!=move.x) {
                    log(L"位置纠正后仍未到达目标列；本次取消硬降并重新规划。");
                    SleepCheck(s->stop,100);
                    continue;
                }
                positioned=std::move(corrected);
                observedX=correctedX;
                usedGhost=correctedByGhost;
            } else {
                // Exact match: the move pulses reached the intended column.
            }

            log(Format(L"位置验证通过：旋转 %d、实际落点列 %d（%s）；准备硬降。",
                move.rotation,observedX,usedGhost?L"Ghost轮廓":L"活动方块"));
            std::wstring err;
            if(!TapKey(target,VK_SPACE,keyHoldMs,err)) throw std::runtime_error("硬降按键失败或焦点丢失。");
            if (trainableAction) {
                pendingExperience=decision.experience;
                pendingExpectedBoard=decision.expectedBoard;
                pendingActionValidation=true;
                log(Format(L"已发送硬降；等待下一块出现后核对棋盘，再决定是否记录训练样本；本次预测消行 %d。",
                    decision.predictedLines));
            } else {
                pendingActionValidation=false;
                log(Format(L"已发送硬降；穷举模式不写入训练样本；本次预测消行 %d。",decision.predictedLines));
            }
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

void UpdateAgainRegionLabel(AppState* s) {
    if (!s->againRegionButton) return;
    if (s->againRect.width > 0 && s->againRect.height > 0)
        SetWindowTextW(s->againRegionButton, Format(L"Again区域已选择（%d×%d）",s->againRect.width,s->againRect.height).c_str());
    else
        SetWindowTextW(s->againRegionButton,L"框选 Again 按钮");
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
    WritePrivateProfileStringW(L"Again",L"Left",Format(L"%d",s->againRect.left).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Again",L"Top",Format(L"%d",s->againRect.top).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Again",L"Width",Format(L"%d",s->againRect.width).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Again",L"Height",Format(L"%d",s->againRect.height).c_str(),s->configPath.c_str());
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
    if (s->modeCombo) WritePrivateProfileStringW(L"Automation",L"Mode",SendMessageW(s->modeCombo,CB_GETCURSEL,0,0)==1?L"1":L"0",s->configPath.c_str());
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
    s->againRect.left=GetPrivateProfileIntW(L"Again",L"Left",0,s->configPath.c_str());
    s->againRect.top=GetPrivateProfileIntW(L"Again",L"Top",0,s->configPath.c_str());
    s->againRect.width=GetPrivateProfileIntW(L"Again",L"Width",0,s->configPath.c_str());
    s->againRect.height=GetPrivateProfileIntW(L"Again",L"Height",0,s->configPath.c_str());
    SendMessageW(s->autoRetryCheck,BM_SETCHECK,GetPrivateProfileIntW(L"Automation",L"AutoRetry",1,s->configPath.c_str())?BST_CHECKED:BST_UNCHECKED,0);
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"Next",L"Count",L"6",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->nextCountEdit,value);
    GetPrivateProfileStringW(L"Next",L"Lookahead",L"4",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->lookaheadEdit,value);
    GetPrivateProfileStringW(L"Input",L"KeyHoldMs",L"26",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->keyHoldEdit,value);
    GetPrivateProfileStringW(L"Input",L"KeyGapMs",L"12",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->keyGapEdit,value);
    GetPrivateProfileStringW(L"Input",L"DropWaitMs",L"190",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->dropWaitEdit,value);
    GetPrivateProfileStringW(L"Training",L"LearningRate",L"0.08",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->learningRateEdit,value);
    GetPrivateProfileStringW(L"Training",L"ExplorationRate",L"5",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->epsilonEdit,value);
    GetPrivateProfileStringW(L"Training",L"Temperature",L"0.40",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->temperatureEdit,value);
    SendMessageW(s->modeCombo,CB_SETCURSEL,GetPrivateProfileIntW(L"Automation",L"Mode",0,s->configPath.c_str())==1?1:0,0);
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
