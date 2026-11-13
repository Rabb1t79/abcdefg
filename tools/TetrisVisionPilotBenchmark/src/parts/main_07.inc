        SetForegroundWindow(target);
        Sleep(250);
        if(GetAncestor(GetForegroundWindow(),GA_ROOT)!=target) throw std::runtime_error("无法将游戏窗口切换到前台。请先手动点一下游戏窗口，再重新启动自动操作。");
        log(gpuMode ? L"GPU Direct3D 11 硬件穷举已启动。" : L"CPU 多核穷举已启动。两者使用相同规则和多行消除优先目标。");
        KickWallLearner kickLearner;
        constexpr bool benchmarkUseWallKick = true;
        if (benchmarkUseWallKick) {
            std::wstring kickPath=s->configPath;
            const size_t configSlash=kickPath.find_last_of(L"\\/");
            kickPath=(configSlash==std::wstring::npos?L".\\":kickPath.substr(0,configSlash+1))+L"TetrisVisionPilotBenchmark-kick-wall.csv";
            kickLearner.Load(kickPath);
            log(Format(L"已载入 Wall Kick 实测档案：%llu 条观测。",static_cast<unsigned long long>(kickLearner.Attempts())));
        } else {
            log(L"Wall Kick 实测辅助已关闭。");
        }
        GpuExhaustiveSolver gpuSolver;
        if (gpuMode) {
            std::wstring gpuInitError;
            if (!gpuSolver.Initialize(gpuInitError))
                throw std::runtime_error(WideToUtf8(gpuInitError));
            log(Format(L"GPU 设备：%s；设备与着色器初始化 %.2f ms；GPU 模式不回退到 CPU/WARP。",
                gpuSolver.DeviceName().c_str(), gpuSolver.InitializationMs()));
        }
        MarathonBoardMemory marathonMemory;
        std::wstring memoryPath=s->configPath;
        const size_t memorySlash=memoryPath.find_last_of(L"\\/");
        memoryPath=(memorySlash==std::wstring::npos?L".\\":memoryPath.substr(0,memorySlash+1))+L"TetrisVisionPilotBenchmark-marathon-memory-v3.csv";
        marathonMemory.SetPath(memoryPath);
        if (!marathonMemory.Ready())
            throw std::runtime_error(WideToUtf8(marathonMemory.LoadStatus()));
        if (marathonMemory.HasSavedState()) {
            const int resumeChoice = MessageBoxW(s->hwnd,
                L"检测到上一局的马拉松记忆档案。\n\n是：恢复上次棋盘和每格所属方块（仅当游戏仍是同一局时选择）。\n否：开始新的记忆会话，不继承旧棋盘。\n取消：停止本次启动。\n\n恢复后若画面出现记忆中不存在的新格，程序仍会停止，不会猜测。",
                L"恢复马拉松记忆", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (resumeChoice == IDYES) {
                log(Format(L"已从事件档案恢复：会话 %llu；已确认落块 %llu；记忆格 %d。",
                    static_cast<unsigned long long>(marathonMemory.SessionId()),
                    static_cast<unsigned long long>(marathonMemory.HistoryCount()),
                    marathonMemory.RememberedCells()));
            } else if (resumeChoice == IDNO) {
                if (!marathonMemory.ResetForNewGame())
                    throw std::runtime_error(WideToUtf8(marathonMemory.LoadStatus()));
                log(L"已选择新一局：创建新的记忆会话；旧档案事件仍保留。");
            } else {
                throw std::runtime_error("用户取消马拉松记忆恢复；未开始自动落块。");
            }
        } else {
            if (!marathonMemory.ResetForNewGame())
                throw std::runtime_error(WideToUtf8(marathonMemory.LoadStatus()));
            log(L"已建立新的马拉松记忆会话；每次确认锁定都会保存形状令牌、四格归属和棋盘快照。");
        }
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
        bool pendingMemoryCommit=false;
        Board pendingSourceBoard{};
        char pendingMemoryPiece{};
        char pendingMemoryNextPiece{};
        int pendingMemoryRotation{}, pendingMemoryTokenRotationPhase{}, pendingMemoryX{}, pendingMemoryMinY{};
        char trackedPieceForPhase{};
        int trackedRotationPhase{};
        bool trackedRotationPhaseInitialized=false;
        int pendingMemoryLines{}, pendingMemoryMultiEvents{};
        double pendingMemoryComputeMs{}, pendingMemoryInputMs{};
        std::uint64_t pendingMemoryCandidateChecks{};
        int pendingMemoryBackendCode{};
        std::chrono::steady_clock::time_point pendingMemoryDecisionStart{};
        DWORD pendingDropTick{};
        DWORD lastScoreRead=0, lastGameOverRead=0;
        int latestScore=0, gameOverHits=0;
        bool hasLatestScore=false;
        bool scoreResetObserved=false;
        int leftTapMs=tapMs, rightTapMs=tapMs;
        bool calibrationPending=true;
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
                if (pendingMemoryCommit) {
                    pendingMemoryCommit = false;
                    log(L"GAME OVER 发生在下一块出生验证之前；最后一步未得到确认，因此不写入已确认的马拉松记忆。");
                }
                if (pendingActionValidation) {
                    log(L"最后一次硬降未等到下一块棋盘验证；该动作不计入训练样本。");
                    pendingActionValidation=false;
                }
                if (hasLatestScore) {
                    PostMessageW(s->hwnd, WM_APP_SCORE_RECORDED, static_cast<WPARAM>(latestScore), 0);
                    log(Format(L"本局最终 SCORE：%d；本测试版本不执行在线训练，性能数据独立记录到日志与马拉松记忆 CSV。",latestScore));
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
                    const VisionResult newGame = AnalyzeStable(rect,threshold,boardError,2,4);
                    if (boardError.empty() && newGame.hasActive) { restarted=true; break; }
                }
                if(!restarted){log(L"点击 Again 后未确认新方块出现；已停止自动重试，避免在旧弹窗上重复点击。");break;}
                log(L"已确认新一局开始，重置分数缓存和透明砖块记忆。");
                if (!marathonMemory.ResetForNewGame())
                    throw std::runtime_error(WideToUtf8(marathonMemory.LoadStatus()));
                trackedPieceForPhase = 0;
                trackedRotationPhase = 0;
                trackedRotationPhaseInitialized = false;
                pendingActionValidation=false;
                pendingMemoryCommit=false;
                SleepCheck(s->stop,450);
                latestScore = 0; hasLatestScore = false;
                scoreResetObserved = false;
                leftTapMs=tapMs; rightTapMs=tapMs; calibrationPending=true;
                lastScoreRead = 0; lastGameOverRead = 0; gameOverHits = 0;
                noPieceSince = GetTickCount();
                continue;
            }

            std::wstring capError;
            VisionResult vision=AnalyzeStable(rect,threshold,capError,2,4);
            if(!capError.empty()) { log(L"屏幕截图失败："+capError); break; }

            if (pendingMemoryCommit) {
                const bool expectedSpawnType = pendingMemoryNextPiece == 0 ||
                    vision.active.name == pendingMemoryNextPiece;
                const bool newPieceAtSpawn = vision.hasActive &&
                    expectedSpawnType && vision.active.minY <= 2 &&
                    (vision.active.name != pendingMemoryPiece || pendingMemoryMinY > 2 ||
                     GetTickCount() - pendingDropTick >= 80);
                if (!newPieceAtSpawn) {
                    if (GetTickCount() - pendingDropTick > 5000) {
                        log(L"硬降后 5 秒内未确认 NEXT 新方块出生；未提交猜测棋盘，自动操作已停止。");
                        break;
                    }
                    SleepCheck(s->stop,8);
                    continue;
                }

                const Board observedFixedBoard = BoardWithoutActive(vision);
                int unexpectedAfterLock = 0;
                for (int y = 0; y < ROWS; ++y)
                    for (int x = 0; x < COLS; ++x)
                        if (observedFixedBoard[y][x] && !pendingExpectedBoard[y][x])
                            ++unexpectedAfterLock;
                if (unexpectedAfterLock > 0) {
                    log(Format(L"新方块出生后发现 %d 个超出预测棋盘的固定格；不写入不一致记忆，已停止。",unexpectedAfterLock));
                    break;
                }

                const double endToEndMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - pendingMemoryDecisionStart).count();
                std::string memoryToken;
                std::uint32_t memoryInstance = 0;
                std::wstring memoryCommitError;
                if (!marathonMemory.Commit(pendingExpectedBoard, pendingMemoryPiece,
                    pendingMemoryRotation, pendingMemoryTokenRotationPhase,
                    pendingMemoryX, pendingMemoryLines,
                    pendingMemoryMultiEvents, pendingMemoryComputeMs,
                    pendingMemoryInputMs, endToEndMs, pendingSourceBoard,
                    pendingMemoryCandidateChecks, pendingMemoryBackendCode, latestScore,
                    memoryToken, memoryInstance, memoryCommitError)) {
                    log(L"马拉松记忆提交失败："+memoryCommitError+L"；停止自动操作，避免记忆与真实棋盘脱节。");
                    break;
                }
                pendingMemoryCommit = false;
                pendingActionValidation = true;
                // A confirmed NEXT spawn resets the per-piece clockwise phase.
                trackedPieceForPhase = vision.active.name;
                trackedRotationPhase = vision.active.rotation;
                trackedRotationPhaseInitialized = true;
                const std::wstring memoryTokenWide(memoryToken.begin(), memoryToken.end());
                log(Format(L"马拉松记忆已确认：实例 #%u；形状令牌 %s；当前固定格 %d；本次消行 %d；纯计算 %.3f ms；输入链路 %.3f ms；端到端 %.3f ms。",
                    memoryInstance,memoryTokenWide.c_str(),marathonMemory.RememberedCells(),pendingMemoryLines,
                    pendingMemoryComputeMs,pendingMemoryInputMs,endToEndMs));
            }

            int recoveredCells=0, unexpectedCells=0;
            Board rememberedVisionBoard=marathonMemory.Reconcile(vision,recoveredCells,unexpectedCells);
            if (!marathonMemory.Ready()) {
                log(L"马拉松记忆初始化/写入失败："+marathonMemory.LoadStatus()+L"；停止自动操作。");
                break;
            }
            if (marathonMemory.ShouldReportRecovery(recoveredCells)) {
                log(Format(L"透明砖块记忆恢复 %d 格；当前固定砖块记忆 %d 格；本局已记录 %llu 次落块。",
                    recoveredCells,marathonMemory.RememberedCells(),
                    static_cast<unsigned long long>(marathonMemory.HistoryCount())));
            }
            if (unexpectedCells>0) {
                log(Format(L"视觉棋盘出现 %d 个记忆中不存在的新增格；与预测状态不一致，为避免错误硬降已停止。",
                    unexpectedCells));
                break;
            }
            if (pendingActionValidation && vision.hasActive) {
                if (rememberedVisionBoard==pendingExpectedBoard) {
                    pendingActionValidation=false;
                    log(L"落点锁定验证通过：记忆棋盘与预测一致；即使透明砖块暂时不可见，也已由历史状态补全。");
                } else {
                    log(L"记忆棋盘与上一落点预测不一致；可能存在输入偏差或识别异常，停止继续落块。");
                    break;
                }
            }

            if(!vision.hasActive) {
                if(GetTickCount()-noPieceSince>8000) { log(L"连续 8 秒未识别到出生区方块，已停止。请检查棋盘框选范围和识别阈值。"); break; }
                SleepCheck(s->stop,120); continue;
            }
            noPieceSince=GetTickCount();
            Candidate piece=vision.active;
            SleepCheck(s->stop,25); if(s->stop.load())break;
            VisionResult check=AnalyzeStable(rect,threshold,capError,2,4);
            if(!capError.empty()) { log(L"复核截图失败："+capError); break; }
            if(!check.hasActive || check.active.name!=piece.name || check.active.rotation!=piece.rotation || check.active.minX!=piece.minX) continue;
            piece=check.active;
            if (!trackedRotationPhaseInitialized || trackedPieceForPhase != piece.name) {
                // At first attachment, initialize from the currently visible
                // geometry. For exact 180-degree phase on symmetric pieces,
                // start a fresh game at spawn; later pieces are tracked from spawn.
                trackedPieceForPhase = piece.name;
                trackedRotationPhase = piece.rotation;
                trackedRotationPhaseInitialized = true;
            } else {
                const auto phaseShapeIt = g_rotations.find(piece.name);
                if (phaseShapeIt == g_rotations.end() || phaseShapeIt->second.empty()) {
                    log(L"当前方块缺少旋转模板；不能可靠维护形状令牌，已停止。");
                    break;
                }
                const int phaseGeometry = trackedRotationPhase %
                    static_cast<int>(phaseShapeIt->second.size());
                if (phaseGeometry != piece.rotation) {
                    // An external/manual turn or a missed input invalidated our
                    // phase counter. Rebase to the visible geometry and log it.
                    trackedRotationPhase = piece.rotation;
                    log(L"旋转相位与当前可见轮廓不一致；已按可见朝向重新校准本块相位。");
                }
            }
            int checkRecovered=0, checkUnexpected=0;
            Board board=marathonMemory.Reconcile(check,checkRecovered,checkUnexpected);
            if (!marathonMemory.Ready()) {
                log(L"马拉松记忆校验失败："+marathonMemory.LoadStatus()+L"；停止自动操作。");
                break;
            }
            if (marathonMemory.ShouldReportRecovery(checkRecovered))
                log(Format(L"复核时通过马拉松记忆补全透明砖块 %d 格。",checkRecovered));
            if (checkUnexpected>0) {
                log(Format(L"二次复核发现 %d 个超出记忆棋盘的格子；停止以避免误降。",checkUnexpected));
                break;
            }
            if (calibrationPending) {
                InputCalibrationResult calibration{};
                std::wstring calibrationSummary;
                const CalibrationOutcome outcome=CalibrateOpeningInput(
                    s->stop,target,rect,threshold,check,board,tapMs,keyGapMs,
                    calibration,calibrationSummary);
                if (outcome==CalibrationOutcome::Unsafe) {
                    log(L"开局输入校准无法安全复位："+calibrationSummary+L" 自动操作已停止，未硬降。");
                    break;
                }
                leftTapMs=calibration.leftTapMs;
                rightTapMs=calibration.rightTapMs;
                WritePrivateProfileStringW(L"InputCalibration",L"LeftTapMs",
                    Format(L"%d",leftTapMs).c_str(),s->configPath.c_str());
                WritePrivateProfileStringW(L"InputCalibration",L"RightTapMs",
                    Format(L"%d",rightTapMs).c_str(),s->configPath.c_str());
                if (!calibrationSummary.empty()) log(L"开局输入校准："+calibrationSummary);
                calibrationPending=false;
                if (s->stop.load()) break;
                VisionResult afterCalibration=AnalyzeStable(rect,threshold,capError,2,4);
                if (!capError.empty()) { log(L"输入校准后复核截图失败："+capError); break; }
                if (!afterCalibration.hasActive ||
                    afterCalibration.active.name!=piece.name ||
                    afterCalibration.active.rotation!=piece.rotation ||
                    afterCalibration.active.minX!=piece.minX) {
                    log(L"输入校准后无法确认原方块仍处于活动状态；取消本次操作，不记录校准动作。");
                    break;
                }
                check=std::move(afterCalibration);
                piece=check.active;
                int afterCalRecovered=0, afterCalUnexpected=0;
                board=marathonMemory.Reconcile(check,afterCalRecovered,afterCalUnexpected);
                if (!marathonMemory.Ready()) {
                    log(L"输入校准后的马拉松记忆写入失败："+marathonMemory.LoadStatus()+L"；停止自动操作。");
                    break;
                }
                if(afterCalUnexpected>0) { log(L"校准后棋盘与马拉松记忆不一致；停止本次操作。"); break; }
            }
            if (pendingActionValidation) {
                if (board == pendingExpectedBoard) {
                    pendingActionValidation=false;
                    log(L"当前棋盘复核通过；马拉松状态记忆与预测一致。");
                } else {
                    log(L"当前棋盘与上一落点预测不一致；停止自动操作以保护后续决策。");
                    break;
                }
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
            bool trainableAction = false; // This benchmark never writes policy samples.
            const auto solverStart = std::chrono::steady_clock::now();
            ExhaustiveDecision exhaustive{};
            CpuGpuBenchmarkInfo backendInfo{};
            const bool solved = gpuMode
                ? gpuSolver.Solve(board,piece.name,piece.rotation,piece.minX,queue.pieces,
                                  exhaustive,backendInfo,capError)
                : ChooseCpuParallelAction(board,piece.name,piece.rotation,piece.minX,queue.pieces,
                                          exhaustive,backendInfo);
            if (!solved) {
                if (gpuMode && !capError.empty()) throw std::runtime_error(WideToUtf8(capError));
                throw std::runtime_error("所选硬件后端未找到合法落点；棋盘可能已堆到顶部，或识别结果不正确。");
            }
            move = exhaustive.move;
            move.score = 0.0;
            decision.move = move;
            decision.expectedBoard = exhaustive.expectedBoard;
            decision.predictedLines = exhaustive.predictedLines;
            decision.searchedNextPieces = exhaustive.searchedNextPieces;
            log(Format(L"%s 搜索完成：候选节点 %llu；纯计算 %.3f ms；CPU 线程 %u；设备 %s；前瞻当前方块 + %d NEXT；首步立即消行 %d；前瞻内多行清除计划 %d 次（%d 行）；目标旋转 %d、列 %d。",
                backendInfo.backend.c_str(),
                static_cast<unsigned long long>(exhaustive.nodes),backendInfo.computeMs,
                backendInfo.cpuThreads,backendInfo.device.empty()?L"CPU":backendInfo.device.c_str(),
                exhaustive.searchedNextPieces,exhaustive.predictedLines,
                exhaustive.multiClearEvents,exhaustive.multiClearLines,move.rotation,move.x));
            if (gpuMode && backendInfo.initializationMs > 0.0)
                log(Format(L"GPU 冷启动初始化另计：%.3f ms；不计入稳态搜索耗时。",backendInfo.initializationMs));
            const auto inputStarted = std::chrono::steady_clock::now();
            const auto& pieceRotations=g_rotations[piece.name];
            int rotateSteps=(move.rotation-piece.rotation+static_cast<int>(pieceRotations.size()))%static_cast<int>(pieceRotations.size());
            log(Format(L"当前 %c 型（稳定采样 %d/3）；NEXT=[%s]；前瞻 %d 块；目标旋转 %d、列 %d，路径评分 %.2f。",
                piece.name,check.activeStableSamples,FormatNextQueueConfidence(queue).c_str(),decision.searchedNextPieces,
                move.rotation,move.x,move.score));
            int currentX=piece.minX;
            bool rotatedWithWallKick=false;
            if (benchmarkUseWallKick && rotateSteps==1 && piece.minY<=4 &&
                ExtractFeatures(board,0).value[3] <= 0.45) {
                int kickedX=piece.minX;
                std::wstring kickMessage;
                const WallKickAttemptOutcome kickOutcome=TryEmpiricalWallKick(
                    s->stop,target,rect,threshold,board,piece,move.rotation,move.x,
                    tapMs,leftTapMs,rightTapMs,keyGapMs,
                    kickLearner,kickedX,kickMessage);
                if (kickOutcome==WallKickAttemptOutcome::Unsafe) {
                    log(L"Wall Kick 尝试未能安全完成："+kickMessage+L" 自动操作已停止，未硬降。");
                    break;
                }
                if (kickOutcome==WallKickAttemptOutcome::Applied) {
                    currentX=kickedX;
                    rotatedWithWallKick=true;
                    trackedRotationPhase = (trackedRotationPhase + 1) % 4;
                    rotateSteps=0;
                    log(kickMessage);
                } else if (kickOutcome==WallKickAttemptOutcome::FailedAndRestored) {
                    log(kickMessage);
                }
            }
            if (!rotatedWithWallKick) {
                for(int i=0;i<rotateSteps && !s->stop.load();++i) {
                    std::wstring err; if(!TapKey(target,VK_UP,tapMs,err)) throw std::runtime_error("旋转按键失败或焦点丢失。");
                    SleepCheck(s->stop,keyGapMs);
                }
                if(s->stop.load())break;
            }
            if(rotateSteps>0) {
                SleepCheck(s->stop,55);
                if(s->stop.load())break;
                VisionResult rotated=AnalyzeStable(rect,threshold,capError,2,4);
                if(!capError.empty()) { log(L"旋转后复核截图失败："+capError); break; }
                bool usedGhost=false;
                if(!ReadPieceColumn(rotated,piece.name,move.rotation,currentX,usedGhost,piece.minY)) {
                    log(L"旋转后无法通过活动方块或 Ghost 确认目标朝向；取消本次落块并重新规划。");
                    SleepCheck(s->stop,100);
                    continue;
                }
                if(usedGhost) log(Format(L"旋转复核：Ghost 确认 %c 型、旋转 %d、落点列 %d。",
                    piece.name,move.rotation,currentX));
                // The visible geometry confirmed the requested turn count.
                // Keep the full 0..3 phase even for 180-degree symmetric pieces.
                trackedRotationPhase = (trackedRotationPhase + rotateSteps) % 4;
            }

            int delta=move.x-currentX;
            WORD moveKey=delta>0?VK_RIGHT:VK_LEFT;
            for(int i=0;i<std::abs(delta) && !s->stop.load();++i) {
                std::wstring err;
                if(!TapKey(target,moveKey,moveKey==VK_RIGHT?rightTapMs:leftTapMs,err)) throw std::runtime_error("移动按键失败或焦点丢失。");
                SleepCheck(s->stop,keyGapMs);
            }
            if(s->stop.load())break;

            // Verify the actual screen position before hard drop. A key pulse is not
            // proof that the game advanced exactly one column.
            SleepCheck(s->stop,45);
            if(s->stop.load())break;
            VisionResult positioned=AnalyzeStable(rect,threshold,capError,2,4);
            if(!capError.empty()) { log(L"横移后位置复核截图失败："+capError); break; }
            int observedX=0;
            bool usedGhost=false;
            if(!ReadPieceColumn(positioned,piece.name,move.rotation,observedX,usedGhost,piece.minY)) {
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
                    if(!TapKey(target,correctionKey,correctionKey==VK_RIGHT?rightTapMs:leftTapMs,err)) throw std::runtime_error("位置纠正按键失败或焦点丢失。");
                    SleepCheck(s->stop,keyGapMs);
                }
                if(s->stop.load())break;
                SleepCheck(s->stop,45);
                if(s->stop.load())break;
                VisionResult corrected=AnalyzeStable(rect,threshold,capError,2,4);
                if(!capError.empty()) { log(L"位置纠正后复核截图失败："+capError); break; }
                int correctedX=0;
                bool correctedByGhost=false;
                if(!ReadPieceColumn(corrected,piece.name,move.rotation,correctedX,correctedByGhost,piece.minY) ||
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
            if(!TapKey(target,VK_SPACE,tapMs,err)) throw std::runtime_error("硬降按键失败或焦点丢失。");
            pendingExpectedBoard = decision.expectedBoard;
            pendingSourceBoard = board;
            pendingMemoryPiece = piece.name;
            pendingMemoryNextPiece = queue.pieces.empty() ? 0 : queue.pieces.front();
            pendingMemoryRotation = move.rotation;
            pendingMemoryTokenRotationPhase = trackedRotationPhase;
            pendingMemoryX = move.x;
            pendingMemoryMinY = positioned.hasActive ? positioned.active.minY : piece.minY;
            pendingMemoryLines = decision.predictedLines;
            pendingMemoryMultiEvents = exhaustive.multiClearEvents;
            pendingMemoryComputeMs = backendInfo.computeMs;
            pendingMemoryInputMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - inputStarted).count();
            pendingMemoryCandidateChecks = backendInfo.candidateChecks;
            pendingMemoryBackendCode = gpuMode ? 1 : 0;
            pendingMemoryDecisionStart = solverStart;
            pendingDropTick = GetTickCount();
            pendingMemoryCommit = true;
            pendingActionValidation = false;
            log(Format(L"已发送硬降；等待确认 NEXT 方块出生，再将锁定棋盘写入马拉松记忆；本次预测消行 %d。",
                decision.predictedLines));
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
    WritePrivateProfileStringW(L"Board",L"ClientRelative",s->boardRect.clientRelative?L"1":L"0",s->configPath.c_str());
    WritePrivateProfileStringW(L"Board",L"ReferenceClientWidth",Format(L"%d",s->boardRect.referenceClientWidth).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Board",L"ReferenceClientHeight",Format(L"%d",s->boardRect.referenceClientHeight).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"Left",Format(L"%d",s->nextRect.left).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"Top",Format(L"%d",s->nextRect.top).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"Width",Format(L"%d",s->nextRect.width).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"Height",Format(L"%d",s->nextRect.height).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"ClientRelative",s->nextRect.clientRelative?L"1":L"0",s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"ReferenceClientWidth",Format(L"%d",s->nextRect.referenceClientWidth).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Next",L"ReferenceClientHeight",Format(L"%d",s->nextRect.referenceClientHeight).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Score",L"Left",Format(L"%d",s->scoreRect.left).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Score",L"Top",Format(L"%d",s->scoreRect.top).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Score",L"Width",Format(L"%d",s->scoreRect.width).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Score",L"Height",Format(L"%d",s->scoreRect.height).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Score",L"ClientRelative",s->scoreRect.clientRelative?L"1":L"0",s->configPath.c_str());
    WritePrivateProfileStringW(L"Score",L"ReferenceClientWidth",Format(L"%d",s->scoreRect.referenceClientWidth).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Score",L"ReferenceClientHeight",Format(L"%d",s->scoreRect.referenceClientHeight).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Again",L"Left",Format(L"%d",s->againRect.left).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Again",L"Top",Format(L"%d",s->againRect.top).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Again",L"Width",Format(L"%d",s->againRect.width).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Again",L"Height",Format(L"%d",s->againRect.height).c_str(),s->configPath.c_str());
    WritePrivateProfileStringW(L"Automation",L"AutoRetry",SendMessageW(s->autoRetryCheck,BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0",s->configPath.c_str());
    wchar_t value[64]{};
    GetWindowTextW(s->nextCountEdit,value,_countof(value)); WritePrivateProfileStringW(L"Next",L"Count",value,s->configPath.c_str());
    GetWindowTextW(s->lookaheadEdit,value,_countof(value)); WritePrivateProfileStringW(L"Next",L"Lookahead",value,s->configPath.c_str());
    if (s->keyHoldEdit) { GetWindowTextW(s->keyHoldEdit,value,_countof(value)); WritePrivateProfileStringW(L"Input",L"TapMs",value,s->configPath.c_str()); }
    if (s->keyGapEdit) { GetWindowTextW(s->keyGapEdit,value,_countof(value)); WritePrivateProfileStringW(L"Input",L"TapGapMs",value,s->configPath.c_str()); }
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
    s->boardRect.clientRelative=GetPrivateProfileIntW(L"Board",L"ClientRelative",0,s->configPath.c_str())!=0;
    s->boardRect.referenceClientWidth=GetPrivateProfileIntW(L"Board",L"ReferenceClientWidth",0,s->configPath.c_str());
    s->boardRect.referenceClientHeight=GetPrivateProfileIntW(L"Board",L"ReferenceClientHeight",0,s->configPath.c_str());
    s->nextRect.clientRelative=GetPrivateProfileIntW(L"Next",L"ClientRelative",0,s->configPath.c_str())!=0;
    s->nextRect.referenceClientWidth=GetPrivateProfileIntW(L"Next",L"ReferenceClientWidth",0,s->configPath.c_str());
    s->nextRect.referenceClientHeight=GetPrivateProfileIntW(L"Next",L"ReferenceClientHeight",0,s->configPath.c_str());
    s->scoreRect.clientRelative=GetPrivateProfileIntW(L"Score",L"ClientRelative",0,s->configPath.c_str())!=0;
    s->scoreRect.referenceClientWidth=GetPrivateProfileIntW(L"Score",L"ReferenceClientWidth",0,s->configPath.c_str());
    s->scoreRect.referenceClientHeight=GetPrivateProfileIntW(L"Score",L"ReferenceClientHeight",0,s->configPath.c_str());
    s->againRect.left=GetPrivateProfileIntW(L"Again",L"Left",0,s->configPath.c_str());
    s->againRect.top=GetPrivateProfileIntW(L"Again",L"Top",0,s->configPath.c_str());
    s->againRect.width=GetPrivateProfileIntW(L"Again",L"Width",0,s->configPath.c_str());
    s->againRect.height=GetPrivateProfileIntW(L"Again",L"Height",0,s->configPath.c_str());
    SendMessageW(s->autoRetryCheck,BM_SETCHECK,GetPrivateProfileIntW(L"Automation",L"AutoRetry",1,s->configPath.c_str())?BST_CHECKED:BST_UNCHECKED,0);
    wchar_t value[64]{};
    GetPrivateProfileStringW(L"Next",L"Count",L"6",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->nextCountEdit,value);
    GetPrivateProfileStringW(L"Next",L"Lookahead",L"4",value,_countof(value),s->configPath.c_str()); SetWindowTextW(s->lookaheadEdit,value);
    int tapMs=GetPrivateProfileIntW(L"Input",L"TapMs",-1,s->configPath.c_str());
    if (tapMs < 0) tapMs=GetPrivateProfileIntW(L"Input",L"KeyHoldMs",8,s->configPath.c_str());
    if (tapMs < 4 || tapMs > 10) tapMs=8; // migrate old long-hold settings to a safe discrete tap
    SetWindowTextW(s->keyHoldEdit,Format(L"%d",tapMs).c_str());
    int tapGapMs=GetPrivateProfileIntW(L"Input",L"TapGapMs",-1,s->configPath.c_str());
    if (tapGapMs < 0) tapGapMs=GetPrivateProfileIntW(L"Input",L"KeyGapMs",8,s->configPath.c_str());
    if (tapGapMs < 0 || tapGapMs > 10) tapGapMs=8;
    SetWindowTextW(s->keyGapEdit,Format(L"%d",tapGapMs).c_str());
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

bool MakeClientRelativeRect(HWND target, const RECT& screenRect, Rect& relativeOut) {
    if (!target || !IsWindow(target)) return false;
    RECT client{};
    if (!GetClientRect(target, &client)) return false;
    POINT first{screenRect.left, screenRect.top};
    POINT last{screenRect.right, screenRect.bottom};
    if (!ScreenToClient(target, &first) || !ScreenToClient(target, &last)) return false;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (first.x < 0 || first.y < 0 || last.x > width || last.y > height ||
        last.x <= first.x || last.y <= first.y) return false;
    relativeOut = {first.x, first.y, last.x - first.x, last.y - first.y,
                   true, width, height};
    return true;
}

bool NormalizeCaptureRegion(Rect& region, HWND target) {
    if (region.width <= 0 || region.height <= 0) return false;
    RECT client{};
    if (!target || !IsWindow(target) || !GetClientRect(target, &client)) return false;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return false;

    if (!region.clientRelative) {
        RECT screenRect{region.left, region.top,
                        region.left + region.width, region.top + region.height};
        Rect converted{};
        if (!MakeClientRelativeRect(target, screenRect, converted)) return false;
        region = converted;
        return true;
    }
    if (region.referenceClientWidth <= 0) region.referenceClientWidth = width;
    if (region.referenceClientHeight <= 0) region.referenceClientHeight = height;
    return true;
}

bool IsCaptureRegionInsideClient(HWND target, const Rect& region) {
    if (!region.clientRelative) {
        return IsScreenRectInsideClient(target, region);
    }
    RECT client{};
    if (!target || !GetClientRect(target, &client)) return false;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const double sx = region.referenceClientWidth > 0
        ? static_cast<double>(width) / region.referenceClientWidth : 1.0;
    const double sy = region.referenceClientHeight > 0
        ? static_cast<double>(height) / region.referenceClientHeight : 1.0;
    const int left = static_cast<int>(std::lround(region.left * sx));
    const int top = static_cast<int>(std::lround(region.top * sy));
    const int w = static_cast<int>(std::lround(region.width * sx));
    const int h = static_cast<int>(std::lround(region.height * sy));
    return left >= 0 && top >= 0 && w > 0 && h > 0 &&
           left + w <= width && top + h <= height;
}

void AppendLog(AppState* s, const std::wstring& message);

bool AutoLocateRegions(AppState* s) {
    HWND target = SelectedWindow(s);
    if (!target || !IsWindow(target)) {
        AppendLog(s, L"自动定位失败：请先选择正在运行的 Tetris Online 游戏窗口。");
        return false;
    }
    std::wstring captureError;
    if (!EnsureWindowCapture(target, captureError)) {
        AppendLog(s, L"窗口捕获初始化失败，尝试使用桌面截图："+captureError);
    }

    RECT client{};
    if (!GetClientRect(target, &client)) {
        AppendLog(s, L"自动定位失败：无法读取游戏窗口客户区。");
        return false;
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width < 500 || height < 360) {
        AppendLog(s, Format(L"自动定位失败：游戏客户区只有 %d×%d，低于可识别布局的最小尺寸。",
            width, height));
        return false;
    }

    // Tetris Online's fixed HUD layout, expressed as client-area ratios taken
    // from the supplied 802×602 reference. The ratios survive window movement
    // and scale proportionally when the game window is resized.
    s->boardRect = {
        static_cast<int>(std::lround(width * 0.151)),
        static_cast<int>(std::lround(height * 0.134)),
        static_cast<int>(std::lround(width * 0.228)),
        static_cast<int>(std::lround(height * 0.631)),
        true, width, height
    };
    s->nextRect = {
        static_cast<int>(std::lround(width * 0.410)),
        static_cast<int>(std::lround(height * 0.205)),
        static_cast<int>(std::lround(width * 0.095)),
        static_cast<int>(std::lround(height * 0.570)),
        true, width, height
    };
    s->scoreRect = {
        static_cast<int>(std::lround(width * 0.840)),
        static_cast<int>(std::lround(height * 0.800)),
        static_cast<int>(std::lround(width * 0.085)),
        static_cast<int>(std::lround(height * 0.105)),
        true, width, height
    };
    UpdateRegionLabel(s);
    UpdateNextRegionLabel(s);
    SaveConfig(s);

    CapturedImage boardImage;
    std::wstring boardError;
    const bool boardCaptured = CaptureScreenRect(s->boardRect, boardImage, boardError);
    int occupied = -1;
    int effectiveThreshold = ReadThreshold(s);
    if (boardCaptured && boardImage.width >= 80 && boardImage.height >= 160) {
        const VisionResult board = Analyze(boardImage, ReadThreshold(s));
        occupied = board.occupiedCount;
        effectiveThreshold = board.usedThreshold;
        AppendLog(s, Format(L"自动定位棋盘成功：%d×%d；占用格 %d；当前自适应颜色阈值 %d。",
            boardImage.width, boardImage.height, occupied, effectiveThreshold));
    } else {
        AppendLog(s, L"自动定位已计算棋盘区域，但截图验证失败："+boardError);
    }

    const int previewCount = static_cast<int>(ReadDoubleEdit(s->nextCountEdit, 6, 1, 10));
    NextQueueResult queue;
    std::wstring nextError;
    const bool nextOk = ReadNextQueue(s->nextRect, previewCount, queue, nextError);
    if (nextOk) {
        s->previewNextPieces = queue.pieces;
        s->hasNextPreview = true;
        SetWindowTextW(s->nextStatusLabel, (L"NEXT识别： "+FormatNextQueueConfidence(queue)).c_str());
        AppendLog(s, L"自动定位 NEXT 成功："+FormatNextQueueConfidence(queue));
    } else {
        s->hasNextPreview = false;
        s->previewNextPieces.clear();
        SetWindowTextW(s->nextStatusLabel, L"NEXT识别：待校准");
        AppendLog(s, L"自动定位 NEXT 区域已设置，但内容验证未通过："+nextError);
    }

    tesseract::TessBaseAPI ocr;
    const std::string tessPath = WideToUtf8(s->tessdataPath);
    const bool ocrReady = !tessPath.empty() &&
        ocr.Init(tessPath.c_str(), "eng", tesseract::OEM_LSTM_ONLY) == 0;
    int score = 0, confidence = 0;
    std::string scoreText;
    std::wstring scoreError;
    bool zeroVerified = false;
    const bool scoreOk = ReadScoreValue(ocr, s->scoreRect, score, confidence,
        scoreText, scoreError, &zeroVerified, ocrReady);
    if (ocrReady) ocr.End();
    if (scoreOk) {
        SetWindowTextW(s->scoreStatusLabel,
            Format(L"SCORE识别：%d（%d）", score, confidence).c_str());
        AppendLog(s, Format(L"自动定位 SCORE 成功：当前读数 %d，置信度 %d/100。",
            score, confidence));
    } else {
        SetWindowTextW(s->scoreStatusLabel, L"SCORE识别：待校准");
        AppendLog(s, L"自动定位 SCORE 区域已设置，但暂未读出可信数字："+scoreError);
    }

    return boardCaptured && nextOk;
}

void AppendLog(AppState* s, const std::wstring& message) {
    SYSTEMTIME st{}; GetLocalTime(&st);
    std::wstring line=Format(L"[%02d:%02d:%02d] ",st.wHour,st.wMinute,st.wSecond)+message+L"\r\n";
    if(!s->logPath.empty()) {
        int bytes=WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),nullptr,0,nullptr,nullptr);
