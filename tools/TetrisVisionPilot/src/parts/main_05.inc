                meanGradient[i] /= count;
                weights_[i] += learningRate * advantage * meanGradient[i] * 2.0;
                weights_[i] = (std::max)(-20.0, (std::min)(20.0, weights_[i]));
            }
        }
        advantageOut = advantage;

        if (baselineEpisode) averageScore_ = static_cast<double>(score);
        else averageScore_ = averageScore_ * 0.80 + static_cast<double>(score) * 0.20;
        bestScore_ = (std::max)(bestScore_, score);
        ++games_;
        lastAdvantage_ = advantage;
        pending_.clear();
        if (baselineEpisode) {
            message = Format(L"首局基准已建立：%d 分；收集样本 %llu。下一局起会按成绩优势更新策略权重。",
                score, static_cast<unsigned long long>(samplesOut));
        } else {
            message = Format(L"成绩已记录：%d 分；样本 %llu；优势值 %+.3f；模型权重已更新。",
                score, static_cast<unsigned long long>(samplesOut), advantage);
        }
        return true;
    }

    void Reset() {
        std::lock_guard<std::mutex> guard(mutex_);
        ResetUnlocked();
    }

    TrainerSnapshot Snapshot() const {
        std::lock_guard<std::mutex> guard(mutex_);
        TrainerSnapshot s;
        s.weights = weights_;
        s.games = games_;
        s.averageScore = averageScore_;
        s.bestScore = bestScore_;
        s.lastAdvantage = lastAdvantage_;
        s.pendingActions = pending_.size();
        return s;
    }

    void Load(const std::wstring& path) {
        std::lock_guard<std::mutex> guard(mutex_);
        ResetUnlocked();
        for (size_t i = 0; i < FEATURE_COUNT; ++i) {
            wchar_t key[32]{}; _snwprintf_s(key, _countof(key), _TRUNCATE, L"Weight%llu", static_cast<unsigned long long>(i));
            wchar_t value[64]{};
            GetPrivateProfileStringW(L"PolicyModel", key, L"", value, _countof(value), path.c_str());
            if (value[0]) weights_[i] = std::wcstod(value, nullptr);
        }
        games_ = static_cast<uint64_t>(GetPrivateProfileIntW(L"PolicyModel", L"Games", 0, path.c_str()));
        bestScore_ = GetPrivateProfileIntW(L"PolicyModel", L"BestScore", 0, path.c_str());
        wchar_t avg[64]{}; GetPrivateProfileStringW(L"PolicyModel", L"AverageScore", L"0", avg, _countof(avg), path.c_str());
        averageScore_ = std::wcstod(avg, nullptr);
        wchar_t adv[64]{}; GetPrivateProfileStringW(L"PolicyModel", L"LastAdvantage", L"0", adv, _countof(adv), path.c_str());
        lastAdvantage_ = std::wcstod(adv, nullptr);
    }

    void Save(const std::wstring& path) const {
        TrainerSnapshot s = Snapshot();
        WritePrivateProfileStringW(L"PolicyModel", L"Games", std::to_wstring(s.games).c_str(), path.c_str());
        WritePrivateProfileStringW(L"PolicyModel", L"BestScore", std::to_wstring(s.bestScore).c_str(), path.c_str());
        WritePrivateProfileStringW(L"PolicyModel", L"AverageScore", std::to_wstring(s.averageScore).c_str(), path.c_str());
        WritePrivateProfileStringW(L"PolicyModel", L"LastAdvantage", std::to_wstring(s.lastAdvantage).c_str(), path.c_str());
        for (size_t i = 0; i < FEATURE_COUNT; ++i) {
            wchar_t key[32]{}; _snwprintf_s(key, _countof(key), _TRUNCATE, L"Weight%llu", static_cast<unsigned long long>(i));
            WritePrivateProfileStringW(L"PolicyModel", key, std::to_wstring(s.weights[i]).c_str(), path.c_str());
        }
    }

private:
    void ResetUnlocked() {
        // 初始权重是可解释的启发式先验；训练会基于实际整局得分改变它们。
        weights_ = {{-1.4, -6.0, -1.1, -1.5, -0.3, -0.4, -0.3, 6.5}};
        games_ = 0;
        averageScore_ = 0.0;
        bestScore_ = 0;
        lastAdvantage_ = 0.0;
        pending_.clear();
    }

    mutable std::mutex mutex_;
    std::array<double, FEATURE_COUNT> weights_{};
    std::vector<TrainingExperience> pending_;
    uint64_t games_{};
    double averageScore_{};
    int bestScore_{};
    double lastAdvantage_{};
};

bool ChoosePolicyAction(const Board& board, char piece, PolicyTrainer& trainer,
                        double epsilon, double temperature, std::mt19937& rng,
                        PolicyDecision& decision) {
    std::vector<ActionCandidate> candidates = EnumerateActions(board, piece);
    if (candidates.empty()) return false;
    const auto weights = trainer.GetWeights();
    temperature = (std::max)(0.05, temperature);
    epsilon = (std::max)(0.0, (std::min)(0.60, epsilon));

    std::vector<double> logits(candidates.size());
    double maxLogit = -std::numeric_limits<double>::infinity();
