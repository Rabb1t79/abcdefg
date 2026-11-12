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
            candidate.nextBoard = next;
            candidates.push_back(std::move(candidate));
        }
    }
    return candidates;
}

struct BootstrapPreference {
    std::array<double, FEATURE_COUNT> preferred{};
    std::array<double, FEATURE_COUNT> alternative{};
};

constexpr std::array<double, FEATURE_COUNT> kRuleTeacherWeights =
    {{-1.4, -6.0, -1.1, -1.5, -0.3, -0.4, -0.3, 6.5}};

double FeatureDot(const std::array<double, FEATURE_COUNT>& weights,
                  const std::array<double, FEATURE_COUNT>& values) {
    double total = 0.0;
    for (size_t i=0;i<FEATURE_COUNT;++i) total += weights[i] * values[i];
    return total;
}

// Compact synthetic curriculum: empty/flat boards, ramps, wells, uneven stacks,
// almost-complete rows and buried holes. These are rule-generated examples,
// not recordings of a real player.
std::vector<BootstrapPreference> BuildBootstrapPreferences() {
    const std::vector<std::array<int, COLS>> profiles = {
        {{0,0,0,0,0,0,0,0,0,0}},
        {{1,1,1,1,1,1,1,1,1,1}},
        {{2,2,2,2,2,2,2,2,2,2}},
        {{1,2,3,4,4,3,2,1,1,1}},
        {{4,3,2,1,1,2,3,4,4,3}},
        {{0,1,2,3,4,5,6,5,4,3}},
        {{6,5,4,3,2,1,0,1,2,3}},
        {{2,4,6,4,2,4,6,4,2,1}},
        {{5,5,5,1,1,1,5,5,5,5}},
        {{7,6,5,4,3,2,1,0,1,2}},
        {{0,1,1,2,2,3,3,4,4,5}},
        {{5,4,3,2,1,0,1,2,3,4}},
        {{3,3,3,6,6,6,3,3,3,3}},
        {{8,7,6,5,4,3,2,1,0,0}},
        {{0,0,1,2,3,4,5,6,7,8}},
        {{4,4,2,2,4,4,2,2,4,4}},
        {{2,5,2,5,2,5,2,5,2,5}},
        {{6,2,6,2,6,2,6,2,6,2}}
    };
    std::vector<Board> states;
    for (size_t n=0;n<profiles.size();++n) {
        Board board{};
        for (int x=0;x<COLS;++x)
            for (int y=ROWS-profiles[n][x];y<ROWS;++y)
                if (y>=0) board[y][x]=1;
        states.push_back(board);

        // Buried-hole variant: remove one cell while preserving cells below/above it.
        if (n % 2 == 0) {
            Board withHole=board;
            const int column=static_cast<int>((n*3+2)%COLS);
            if (profiles[n][column] >= 2) {
                withHole[ROWS-2][column]=0;
                states.push_back(withHole);
            }
        }

        // Near-clear variant with a single, two-cell or three-cell gap.
        Board nearClear=board;
        for (int x=0;x<COLS;++x) nearClear[ROWS-1][x]=1;
        nearClear[ROWS-1][static_cast<int>((n*3+1)%COLS)]=0;
        if (n%3==0) nearClear[ROWS-1][static_cast<int>((n*3+2)%COLS)]=0;
        states.push_back(nearClear);
    }

    const char pieces[]={'I','O','T','S','Z','J','L'};
    std::vector<BootstrapPreference> preferences;
    for (const Board& state:states) for (char piece:pieces) {
        const auto actions=EnumerateActions(state,piece);
        if (actions.size()<2) continue;
        size_t best=0;
        double bestValue=FeatureDot(kRuleTeacherWeights,actions[0].features.value);
        for (size_t i=1;i<actions.size();++i) {
            const double value=FeatureDot(kRuleTeacherWeights,actions[i].features.value);
            if (value>bestValue) { bestValue=value; best=i; }
        }
        for (size_t i=0;i<actions.size();++i) {
            if (i==best) continue;
            const double alternativeValue=FeatureDot(kRuleTeacherWeights,actions[i].features.value);
            if (bestValue-alternativeValue<=1e-7) continue;
            BootstrapPreference sample;
            sample.preferred=actions[best].features.value;
            sample.alternative=actions[i].features.value;
            preferences.push_back(sample);
        }
    }
    return preferences;
}

std::array<double, FEATURE_COUNT> PretrainFromBootstrap(size_t& sampleCount) {
    const auto preferences=BuildBootstrapPreferences();
    sampleCount=preferences.size();
    auto weights=std::array<double, FEATURE_COUNT>{};
    if (preferences.empty()) return kRuleTeacherWeights;

    // Pairwise imitation: learn to rank the rule-teacher's better placements above
    // weaker placements. No negative reward or punitive update is used.
    for (int epoch=0;epoch<80;++epoch) {
        for (const auto& sample:preferences) {
            std::array<double, FEATURE_COUNT> delta{};
            double norm=1e-9;
            for (size_t i=0;i<FEATURE_COUNT;++i) {
                delta[i]=sample.preferred[i]-sample.alternative[i];
                norm+=delta[i]*delta[i];
            }
            const double target=FeatureDot(kRuleTeacherWeights,delta);
            const double current=FeatureDot(weights,delta);
            if (current+0.04<target) {
                const double scale=(std::min)(0.35,0.65*(target-current)/norm);
                for (size_t i=0;i<FEATURE_COUNT;++i) weights[i]+=scale*delta[i];
            }
        }
    }
    for (double& weight:weights) weight=(std::max)(-20.0,(std::min)(20.0,weight));
    return weights;
}

struct TrainerSnapshot {
    std::array<double, FEATURE_COUNT> weights{};
    uint64_t bootstrapSamples{};
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

    std::vector<TrainingExperience> PendingExperiences() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return pending_;
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

            // Gentle training mode: below-baseline episodes are diagnostic data only.
            // Never apply a negative-gradient update to the policy weights.
            if (advantage > 0.0) {
            std::array<double, FEATURE_COUNT> meanGradient{};
            for (const auto& e : pending_) {
                for (size_t i = 0; i < FEATURE_COUNT; ++i) meanGradient[i] += e.gradient[i];
            }
            const double count = static_cast<double>(pending_.size());
            for (size_t i = 0; i < FEATURE_COUNT; ++i) {
