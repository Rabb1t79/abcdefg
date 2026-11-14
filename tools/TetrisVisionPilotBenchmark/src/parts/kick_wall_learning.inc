// Empirical Wall Kick learner for the real Tetris Online client.
// We do not assume a generic SRS table: only observed in-game outcomes are kept.
struct KickWallProfile {
    char piece{};
    int fromRotation{};
    int toRotation{};
    char side{};
    int attempts{};
    int successes{};
    int sumDeltaX{};
    int sumDeltaY{};
};

class KickWallLearner {
public:
    void Load(const std::wstring& path) {
        path_=path;
        profiles_.clear();
        std::ifstream input{std::filesystem::path(path)};
        std::string line;
        while (std::getline(input,line)) {
            std::stringstream row(line);
            std::string piece,from,to,side,success,preX,preY,postX,postY,dx,dy;
            if (!std::getline(row,piece,',')) continue;
            if (piece=="piece" || piece.empty()) continue;
            if (!std::getline(row,from,',') || !std::getline(row,to,',') ||
                !std::getline(row,side,',') || !std::getline(row,success,',') ||
                !std::getline(row,preX,',') || !std::getline(row,preY,',') ||
                !std::getline(row,postX,',') || !std::getline(row,postY,',') ||
                !std::getline(row,dx,',') || !std::getline(row,dy,',')) continue;
            try {
                const bool succeeded=std::stoi(success)!=0;
                ObserveMemory(piece[0],std::stoi(from),std::stoi(to),side[0],
                    succeeded,std::stoi(dx),std::stoi(dy));
            } catch (...) { continue; }
        }
    }

    bool ShouldAttempt(char piece, int fromRotation, int toRotation, char side) const {
        const KickWallProfile* profile=Find(piece,fromRotation,toRotation,side);
        if (!profile || profile->attempts<3) return true;
        const double rate=static_cast<double>(profile->successes)/profile->attempts;
        return rate>=0.50;
    }

    void Observe(char piece, int fromRotation, int toRotation, char side,
                 bool success, int preX, int preY, int postX, int postY) {
        const int dx=success ? postX-preX : 0;
        const int dy=success ? postY-preY : 0;
        ObserveMemory(piece,fromRotation,toRotation,side,success,dx,dy);
        if (path_.empty()) return;
        const bool header=!std::filesystem::exists(std::filesystem::path(path_));
        std::ofstream output{std::filesystem::path(path_),std::ios::out|std::ios::app};
        if (!output) return;
        if (header)
            output<<"piece,from_rotation,to_rotation,side,success,pre_x,pre_y,post_x,post_y,observed_dx,observed_dy\n";
        output<<piece<<','<<fromRotation<<','<<toRotation<<','<<side<<','
              <<(success?1:0)<<','<<preX<<','<<preY<<','
              <<(success?postX:preX)<<','<<(success?postY:preY)<<','
              <<dx<<','<<dy<<'\n';
    }

    size_t Attempts() const {
        size_t total=0;
        for (const auto& p:profiles_) total+=static_cast<size_t>(p.attempts);
        return total;
    }

    std::wstring Describe(char piece, int fromRotation, int toRotation, char side) const {
        const KickWallProfile* p=Find(piece,fromRotation,toRotation,side);
        if (!p) return L"尚无该组合的实测数据";
        const int rate=p->attempts>0 ? 100*p->successes/p->attempts : 0;
        const int avgX=p->successes>0 ? p->sumDeltaX/p->successes : 0;
        const int avgY=p->successes>0 ? p->sumDeltaY/p->successes : 0;
        return Format(L"实测 %d 次，成功 %d 次（%d%%），平均锚点偏移 (%d,%d)",
            p->attempts,p->successes,rate,avgX,avgY);
    }

private:
    const KickWallProfile* Find(char piece, int fromRotation, int toRotation, char side) const {
        for (const auto& p:profiles_)
            if (p.piece==piece && p.fromRotation==fromRotation &&
                p.toRotation==toRotation && p.side==side) return &p;
        return nullptr;
    }

    void ObserveMemory(char piece, int fromRotation, int toRotation, char side,
                       bool success, int dx, int dy) {
        for (auto& p:profiles_) {
            if (p.piece==piece && p.fromRotation==fromRotation &&
                p.toRotation==toRotation && p.side==side) {
                ++p.attempts;
                if (success) { ++p.successes; p.sumDeltaX+=dx; p.sumDeltaY+=dy; }
                return;
            }
        }
        KickWallProfile p;
        p.piece=piece; p.fromRotation=fromRotation; p.toRotation=toRotation;
        p.side=side; p.attempts=1;
        if (success) { p.successes=1; p.sumDeltaX=dx; p.sumDeltaY=dy; }
        profiles_.push_back(p);
    }

    std::wstring path_;
    std::vector<KickWallProfile> profiles_;
};

int ShapeWidth(const std::vector<Cell>& shape) {
    int maxX=-1;
    for (const Cell& c:shape) maxX=(std::max)(maxX,c.x);
    return maxX+1;
}

bool FindWallKickPreRotationX(const Board& board, char piece, int fromRotation,
                              int toRotation, int landingX, int currentY,
                              int& preRotationX, char& sideOut) {
    const auto rotations=g_rotations.find(piece);
    if (rotations==g_rotations.end() ||
        fromRotation<0 || toRotation<0 ||
        fromRotation>=static_cast<int>(rotations->second.size()) ||
        toRotation>=static_cast<int>(rotations->second.size())) return false;
    const auto& fromShape=rotations->second[static_cast<size_t>(fromRotation)];
    const auto& toShape=rotations->second[static_cast<size_t>(toRotation)];
    const int toWidth=ShapeWidth(toShape);
    const bool leftTarget=landingX<=1;
    const bool rightTarget=landingX+toWidth>=COLS-1;
    if (!leftTarget && !rightTarget) return false;
    sideOut=leftTarget?'L':'R';
    const int fromMaxX=COLS-ShapeWidth(fromShape);
    if (fromMaxX<0) return false;
    const int begin=leftTarget?0:(std::max)(0,fromMaxX-2);
    const int end=leftTarget?(std::min)(2,fromMaxX):fromMaxX;
    int best=-1;
    int bestDistance=(std::numeric_limits<int>::max)();
    for (int x=begin;x<=end;++x) {
        if (!Valid(board,fromShape,x,currentY)) continue;
        // A kick is only worth testing when the destination orientation does
        // not fit at the same anchor and therefore needs a real kick candidate.
        if (Valid(board,toShape,x,currentY)) continue;
        const int distance=std::abs(x-landingX);
        if (distance<bestDistance) { best=x; bestDistance=distance; }
    }
    if (best<0) return false;
    preRotationX=best;
    return true;
}

enum class WallKickAttemptOutcome {
    NotApplicable,
    Applied,
    FailedAndRestored,
    Unsafe
};

bool MoveActiveToColumnVerified(const std::atomic<bool>& stop, HWND target,
                                const Rect& rect, int threshold, const Board& board,
                                char piece, int rotation, int targetX,
                                int leftTapMs, int rightTapMs, int keyGapMs,
                                int& currentX, int& currentY, std::wstring& error) {
    const auto rotations=g_rotations.find(piece);
    if (rotations==g_rotations.end() ||
        rotation<0 || rotation>=static_cast<int>(rotations->second.size())) return false;
    const auto& shape=rotations->second[static_cast<size_t>(rotation)];
    for (int step=0;step<10 && currentX!=targetX;++step) {
        if (stop.load()) { error=L"用户请求停止。"; return false; }
        const int direction=targetX>currentX?1:-1;
        if (!Valid(board,shape,currentX+direction,currentY)) {
            error=L"预旋转横移路径被墙壁或堆叠阻挡。";
            return false;
        }
        const WORD key=direction>0?VK_RIGHT:VK_LEFT;
        const int pulse=direction>0?rightTapMs:leftTapMs;
        if (!TapKey(target,key,pulse,stop,error)) return false;
        SleepCheck(stop,keyGapMs);
        SleepCheck(stop,35);
        if (stop.load()) { error=L"用户请求停止。"; return false; }
        int observedX=0,observedY=0;
        if (!ReadCalibrationPiece(target,rect,threshold,piece,rotation,
                                  currentX,currentY,observedX,observedY,error)) return false;
        if (observedX-currentX!=direction) {
            error=L"预旋转横移未能稳定移动恰好一格。";
            return false;
        }
        currentX=observedX;
        currentY=observedY;
    }
    return currentX==targetX;
}

WallKickAttemptOutcome TryEmpiricalWallKick(const std::atomic<bool>& stop,
                                            HWND target, const Rect& rect,
                                            int threshold, const Board& board,
                                            const Candidate& piece, int targetRotation,
                                            int landingX, int rotationTapMs, int leftTapMs,
                                            int rightTapMs, int keyGapMs,
                                            KickWallLearner& learner,
                                            int& resultingX, std::wstring& logText) {
    int preX=0;
    char side=0;
    if (!FindWallKickPreRotationX(board,piece.name,piece.rotation,targetRotation,
                                  landingX,piece.minY,preX,side))
        return WallKickAttemptOutcome::NotApplicable;
    if (!learner.ShouldAttempt(piece.name,piece.rotation,targetRotation,side))
        return WallKickAttemptOutcome::NotApplicable;

    int currentX=piece.minX,currentY=piece.minY;
    std::wstring moveError;
    if (!MoveActiveToColumnVerified(stop,target,rect,threshold,board,piece.name,
            piece.rotation,preX,leftTapMs,rightTapMs,keyGapMs,
            currentX,currentY,moveError)) {
        if (currentX==piece.minX &&
            moveError==L"预旋转横移路径被墙壁或堆叠阻挡。") {
            logText=L"墙边预定位路径被阻挡；不尝试踢墙，改用普通旋转。";
            return WallKickAttemptOutcome::NotApplicable;
        }
        if (currentX!=piece.minX) {
            std::wstring restoreError;
            if (MoveActiveToColumnVerified(stop,target,rect,threshold,board,piece.name,
                    piece.rotation,piece.minX,leftTapMs,rightTapMs,keyGapMs,
                    currentX,currentY,restoreError)) {
                logText=L"墙边预定位路径中途受阻，已复位；改用普通旋转。";
                return WallKickAttemptOutcome::FailedAndRestored;
            }
            logText=L"墙边预定位路径受阻且无法复位："+restoreError;
            return WallKickAttemptOutcome::Unsafe;
        }
        logText=L"Wall Kick 预定位未完成："+moveError;
        return WallKickAttemptOutcome::Unsafe;
    }

    const int beforeKickX=currentX,beforeKickY=currentY;
    std::wstring inputError;
    if (!TapKey(target,VK_UP,rotationTapMs,stop,inputError)) {
        logText=L"Wall Kick 旋转输入失败："+inputError;
        return WallKickAttemptOutcome::Unsafe;
    }
    SleepCheck(stop,55);
    if (stop.load()) {
        logText=L"用户请求停止。";
        return WallKickAttemptOutcome::Unsafe;
    }

    std::wstring visionError;
    VisionResult after=AnalyzeStable(rect,threshold,visionError);
    if (!visionError.empty()) {
        logText=L"Wall Kick 后复核失败："+visionError;
        return WallKickAttemptOutcome::Unsafe;
    }
    if (after.hasActive && after.active.name==piece.name &&
        after.active.rotation==targetRotation) {
        resultingX=after.active.minX;
        learner.Observe(piece.name,piece.rotation,targetRotation,side,true,
                        beforeKickX,beforeKickY,after.active.minX,after.active.minY);
        logText=Format(L"Wall Kick 实测成功：%c %d→%d，%s侧；观察到锚点位移 (%d,%d)；%s。",
            piece.name,piece.rotation,targetRotation,side=='L'?L"左":L"右",
            after.active.minX-beforeKickX,after.active.minY-beforeKickY,
            learner.Describe(piece.name,piece.rotation,targetRotation,side).c_str());
        return WallKickAttemptOutcome::Applied;
    }

    if (after.hasActive && after.active.name==piece.name &&
        after.active.rotation==piece.rotation) {
        learner.Observe(piece.name,piece.rotation,targetRotation,side,false,
                        beforeKickX,beforeKickY,beforeKickX,beforeKickY);
        int restoreX=after.active.minX,restoreY=after.active.minY;
        if (!MoveActiveToColumnVerified(stop,target,rect,threshold,board,piece.name,
                piece.rotation,piece.minX,leftTapMs,rightTapMs,keyGapMs,
                restoreX,restoreY,moveError)) {
            logText=L"Wall Kick 未成功，且无法安全复位："+moveError;
            return WallKickAttemptOutcome::Unsafe;
        }
        logText=Format(L"Wall Kick 实测未成功：%c %d→%d，%s侧；已复位，改用普通旋转；%s。",
            piece.name,piece.rotation,targetRotation,side=='L'?L"左":L"右",
            learner.Describe(piece.name,piece.rotation,targetRotation,side).c_str());
        return WallKickAttemptOutcome::FailedAndRestored;
    }

    logText=L"Wall Kick 后无法确认活动方块方向；取消本次落块，不写入未经确认的数据。";
    return WallKickAttemptOutcome::Unsafe;
}
