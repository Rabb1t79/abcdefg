        for (int x = 0; x < COLS; ++x) {
            int current = board[y][x] ? 1 : 0;
            if (current != previous) ++rowTransitions;
            previous = current;
        }
        if (previous == 0) ++rowTransitions; // right wall
    }
    for (int x = 0; x < COLS; ++x) {
        int previous = 1; // ceiling
        for (int y = 0; y < ROWS; ++y) {
            int current = board[y][x] ? 1 : 0;
            if (current != previous) ++colTransitions;
            previous = current;
        }
        if (previous == 0) ++colTransitions; // floor
    }

    Features f;
    f.value[0] = static_cast<double>(aggregate) / 200.0;
    f.value[1] = static_cast<double>(holes) / 40.0;
    f.value[2] = static_cast<double>(bumpiness) / 40.0;
    f.value[3] = static_cast<double>(maxHeight) / 20.0;
    f.value[4] = static_cast<double>(wells) / 80.0;
    f.value[5] = static_cast<double>(rowTransitions) / 200.0;
    f.value[6] = static_cast<double>(colTransitions) / 200.0;
    f.value[7] = static_cast<double>(linesCleared) / 4.0;
    return f;
}

std::vector<ActionCandidate> EnumerateActions(const Board& board, char piece) {
    std::vector<ActionCandidate> candidates;
    auto it = g_rotations.find(piece);
    if (it == g_rotations.end()) return candidates;
    for (int r = 0; r < static_cast<int>(it->second.size()); ++r) {
        const auto& shape = it->second[r];
        int minX = COLS, maxX = 0;
        for (const auto& c : shape) { minX = (std::min)(minX, c.x); maxX = (std::max)(maxX, c.x); }
        for (int x = -minX; x < COLS - maxX; ++x) {
            Board next{};
            int lines = 0;
            if (!PlaceAndClear(board, shape, x, next, lines)) continue;
            ActionCandidate candidate;
            candidate.move.rotation = r;
            candidate.move.x = x;
            candidate.linesCleared = lines;
            candidate.features = ExtractFeatures(next, lines);
            candidates.push_back(std::move(candidate));
        }
    }
    return candidates;
}

struct TrainerSnapshot {
    std::array<double, FEATURE_COUNT> weights{};
    uint64_t games{};
    double averageScore{};
    int bestScore{};
    double lastAdvantage{};
    size_t pendingActions{};
};

class PolicyTrainer {
public:
    PolicyTrainer() { ResetUnlocked(); }

    std::array<double, FEATURE_COUNT> GetWeights() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return weights_;
    }

    void AddExperience(const TrainingExperience& experience) {
        std::lock_guard<std::mutex> guard(mutex_);
        pending_.push_back(experience);
    }

    bool SubmitScore(int score, double learningRate, double& advantageOut, size_t& samplesOut, std::wstring& message) {
        if (score < 0 || score > 100000000) {
            message = L"分数范围无效，请输入 0 到 100000000 之间的整数。";
            return false;
        }
        std::lock_guard<std::mutex> guard(mutex_);
        if (pending_.empty()) {
            message = L"本局没有可训练的动作样本。请先让 AI 完成至少一个落块，再提交本局分数。";
            return false;
        }
        samplesOut = pending_.size();
        const bool baselineEpisode = (games_ == 0);
        double advantage = 0.0;
        if (!baselineEpisode) {
            const double baseline = (std::max)(500.0, averageScore_);
            const double scale = (std::max)(2500.0, baseline * 0.35);
            const double rawAdvantage = (static_cast<double>(score) - baseline) / scale;
            advantage = (std::max)(-2.0, (std::min)(2.0, rawAdvantage));

            std::array<double, FEATURE_COUNT> meanGradient{};
            for (const auto& e : pending_) {
                for (size_t i = 0; i < FEATURE_COUNT; ++i) meanGradient[i] += e.gradient[i];
            }
            const double count = static_cast<double>(pending_.size());
            for (size_t i = 0; i < FEATURE_COUNT; ++i) {
