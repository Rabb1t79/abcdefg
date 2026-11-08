                meanGradient[i] /= count;
                weights_[i] += learningRate * advantage * meanGradient[i] * 2.0;
                weights_[i] = (std::max)(-20.0, (std::min)(20.0, weights_[i]));
            }
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
        } else if (advantage > 0.0) {
            message = Format(L"成绩已记录：%d 分；样本 %llu；正向优势值 +%.3f；策略权重已向本局有效方向更新。",
                score, static_cast<unsigned long long>(samplesOut), advantage);
        } else {
            message = Format(L"成绩已记录：%d 分；样本 %llu；成绩差值 %.3f。本局保留为诊断记录，模型权重保持不变；没有执行负向惩罚更新。",
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
        s.bootstrapSamples = bootstrapSamples_;
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
        const uint64_t storedBootstrapSamples=static_cast<uint64_t>(
            GetPrivateProfileIntW(L"PolicyModel",L"BootstrapSamples",0,path.c_str()));
        if (storedBootstrapSamples>0) {
            for (size_t i = 0; i < FEATURE_COUNT; ++i) {
                wchar_t key[32]{}; _snwprintf_s(key, _countof(key), _TRUNCATE, L"Weight%llu", static_cast<unsigned long long>(i));
                wchar_t value[64]{};
                GetPrivateProfileStringW(L"PolicyModel", key, L"", value, _countof(value), path.c_str());
                if (value[0]) weights_[i] = std::wcstod(value, nullptr);
            }
            bootstrapSamples_=storedBootstrapSamples;
        } // Legacy files deliberately keep the new bootstrap weights.
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
        WritePrivateProfileStringW(L"PolicyModel", L"BootstrapSamples", std::to_wstring(s.bootstrapSamples).c_str(), path.c_str());
        for (size_t i = 0; i < FEATURE_COUNT; ++i) {
            wchar_t key[32]{}; _snwprintf_s(key, _countof(key), _TRUNCATE, L"Weight%llu", static_cast<unsigned long long>(i));
            WritePrivateProfileStringW(L"PolicyModel", key, std::to_wstring(s.weights[i]).c_str(), path.c_str());
        }
    }

private:
    void ResetUnlocked() {
        // Seed the policy from a small rule-generated demonstration corpus before live play.
        weights_ = PretrainFromBootstrap(bootstrapSamples_);
        games_ = 0;
        averageScore_ = 0.0;
        bestScore_ = 0;
        lastAdvantage_ = 0.0;
        pending_.clear();
    }

    mutable std::mutex mutex_;
    std::array<double, FEATURE_COUNT> weights_{};
    uint64_t bootstrapSamples_{};
    std::vector<TrainingExperience> pending_;
    uint64_t games_{};
    double averageScore_{};
    int bestScore_{};
    double lastAdvantage_{};
};

struct SuccessExperienceRecord {
    std::array<double, FEATURE_COUNT> stateFeatures{};
    char piece{};
    int rotation{};
    int x{};
    std::array<char, 6> nextPieces{};
    int nextCount{};
};

class ExperienceMemory {
public:
    static constexpr size_t kMaxRetrievalRecords = 5000;

    void Load(const std::wstring& path) {
        std::lock_guard<std::mutex> guard(mutex_);
        records_.clear();
        std::ifstream input{std::filesystem::path(path)};
        std::string line;
        while (std::getline(input,line)) {
            std::vector<std::string> fields;
            std::stringstream row(line);
            std::string field;
            while (std::getline(row,field,',')) fields.push_back(field);
            if (fields.size()!=15 || fields[2]!="1" || fields[3].empty()) continue;
            SuccessExperienceRecord record;
            try {
                record.piece=fields[3][0];
                record.rotation=std::stoi(fields[4]);
                record.x=std::stoi(fields[5]);
                const std::string& next=fields[6];
                record.nextCount=(std::min)(6,static_cast<int>(next.size()));
                for(int i=0;i<record.nextCount;++i) record.nextPieces[i]=next[static_cast<size_t>(i)];
                for(size_t i=0;i<FEATURE_COUNT;++i) record.stateFeatures[i]=std::stod(fields[7+i]);
                records_.push_back(record);
            } catch(...) { continue; }
            if(records_.size()>kMaxRetrievalRecords) records_.erase(records_.begin());
        }
    }

    bool AppendEpisode(const std::wstring& path, int score, double baseline,
                       bool aboveBaseline, const std::vector<TrainingExperience>& experiences,
                       size_t& written, std::wstring& error) {
        written=0;
        if(experiences.empty()) return true;
        std::ofstream output{std::filesystem::path(path),std::ios::out|std::ios::app};
        if(!output) { error=L"无法写入经验档案："+path; return false; }
        std::lock_guard<std::mutex> guard(mutex_);
        for(const auto& experience:experiences) {
            std::string next;
            for(int i=0;i<experience.nextCount && i<6;++i) next.push_back(experience.nextPieces[i]);
            output<<score<<','<<std::fixed<<std::setprecision(3)<<baseline<<','
                  <<(aboveBaseline?1:0)<<','<<experience.piece<<','
                  <<experience.rotation<<','<<experience.x<<','<<next;
            for(double value:experience.stateFeatures)
                output<<','<<std::setprecision(7)<<value;
            output<<'\n';
            ++written;
            if(aboveBaseline) {
                SuccessExperienceRecord record;
                record.stateFeatures=experience.stateFeatures;
                record.piece=experience.piece;
                record.rotation=experience.rotation;
                record.x=experience.x;
                record.nextPieces=experience.nextPieces;
                record.nextCount=(std::min)(6,experience.nextCount);
                records_.push_back(record);
                if(records_.size()>kMaxRetrievalRecords) records_.erase(records_.begin());
            }
        }
        output.flush();
        if(!output) { error=L"经验档案写入未完成。"; return false; }
        return true;
    }

    double PreferenceBonus(const Board& board, char piece,
                           const std::vector<char>& nextQueue, const Move& move) const {
        std::lock_guard<std::mutex> guard(mutex_);
        if(records_.empty()) return 0.0;
        const auto current=ExtractFeatures(board,0).value;
        double bestBonus=0.0;
        for(const auto& record:records_) {
            if(record.piece!=piece || record.rotation!=move.rotation || record.x!=move.x) continue;
            double distanceSquared=0.0;
            for(size_t i=0;i<FEATURE_COUNT;++i) {
                const double d=current[i]-record.stateFeatures[i];
                distanceSquared+=d*d;
            }
            const double distance=std::sqrt(distanceSquared/static_cast<double>(FEATURE_COUNT));
            const double similarity=std::exp(-3.5*distance);
            if(similarity<0.55) continue;
            const int compared=(std::min)(2,(std::min)(record.nextCount,static_cast<int>(nextQueue.size())));
            double queueMatch=0.5;
            if(compared>0) {
                int matched=0;
                for(int i=0;i<compared;++i)
                    if(record.nextPieces[i]==nextQueue[static_cast<size_t>(i)]) ++matched;
                queueMatch=static_cast<double>(matched)/compared;
                if(queueMatch==0.0) continue;
            }
            bestBonus=(std::max)(bestBonus,0.45*similarity*(0.75+0.25*queueMatch));
        }
        return bestBonus;
    }

    size_t Size() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return records_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<SuccessExperienceRecord> records_;
};

double DotFeatures(const std::array<double, FEATURE_COUNT>& weights, const Features& features) {
    double score = 0.0;
    for (size_t i = 0; i < FEATURE_COUNT; ++i) score += weights[i] * features.value[i];
    return score;
}
Features AddDiscounted(const Features& base, const Features& extra, double discount) {
    Features value = base;
    for (size_t i = 0; i < FEATURE_COUNT; ++i) value.value[i] += extra.value[i] * discount;
    return value;
}
struct SearchNode { Board board{}; Features trajectory{}; double score{}; };

bool ChoosePolicyAction(const Board& board, char piece, const std::vector<char>& nextQueue,
                        int lookaheadDepth, PolicyTrainer& trainer, const ExperienceMemory& memory,
                        double epsilon, double temperature, std::mt19937& rng, PolicyDecision& decision) {
    std::vector<ActionCandidate> roots = EnumerateActions(board, piece);
    if (roots.empty()) return false;
    const auto weights = trainer.GetWeights();
    temperature = (std::max)(0.05, temperature);
    epsilon = (std::max)(0.0, (std::min)(0.60, epsilon));
    lookaheadDepth = (std::max)(0, (std::min)(lookaheadDepth, static_cast<int>(nextQueue.size())));
    constexpr size_t kBeamWidthPerRoot = 5;
    constexpr double kDiscount = 0.82;

    for (auto& root : roots) {
        Features initial = root.features;
        std::vector<SearchNode> beam{{root.nextBoard, initial, DotFeatures(weights, initial)}};
        for (int depth = 0; depth < lookaheadDepth; ++depth) {
            std::vector<SearchNode> expanded;
            for (const auto& node : beam) {
                const auto options = EnumerateActions(node.board, nextQueue[depth]);
                for (const auto& option : options) {
                    Features trajectory = AddDiscounted(node.trajectory, option.features, std::pow(kDiscount, depth + 1));
                    expanded.push_back({option.nextBoard, trajectory, DotFeatures(weights, trajectory)});
                }
            }
            if (expanded.empty()) break;
            std::sort(expanded.begin(), expanded.end(), [](const SearchNode& a, const SearchNode& b) { return a.score > b.score; });
            if (expanded.size() > kBeamWidthPerRoot) expanded.resize(kBeamWidthPerRoot);
            beam = std::move(expanded);
        }
        if (!beam.empty()) {
            const auto best = std::max_element(beam.begin(), beam.end(),
                [](const SearchNode& a, const SearchNode& b) { return a.score < b.score; });
            root.features = best->trajectory;
            root.move.score = best->score / temperature;
        } else root.move.score = DotFeatures(weights, root.features) / temperature;
    }

    std::vector<double> logits(roots.size());
    double maxLogit = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < roots.size(); ++i) {
        roots[i].move.score += memory.PreferenceBonus(board,piece,nextQueue,roots[i].move);
        logits[i] = roots[i].move.score;
        maxLogit = (std::max)(maxLogit, logits[i]);
    }
    std::vector<double> probabilities(roots.size());
    double total = 0.0;
    for (size_t i = 0; i < roots.size(); ++i) { probabilities[i] = std::exp((std::max)(-60.0, logits[i] - maxLogit)); total += probabilities[i]; }
    if (total <= 0.0) return false;
    for (double& p : probabilities) p /= total;

    std::array<double, FEATURE_COUNT> expected{};
    for (size_t i = 0; i < roots.size(); ++i)
        for (size_t j = 0; j < FEATURE_COUNT; ++j) expected[j] += probabilities[i] * roots[i].features.value[j];

    std::uniform_real_distribution<double> unit(0.0, 1.0);
    size_t chosen = 0;
    if (unit(rng) < epsilon) {
        std::uniform_int_distribution<size_t> pick(0, roots.size() - 1);
        chosen = pick(rng);
    } else {
        std::discrete_distribution<size_t> pick(probabilities.begin(), probabilities.end());
        chosen = pick(rng);
    }
    const double actionProbability = (1.0 - epsilon) * probabilities[chosen] + epsilon / static_cast<double>(roots.size());
    const double scale = ((1.0 - epsilon) * probabilities[chosen] / (std::max)(1e-12, actionProbability)) / temperature;
    for (size_t j = 0; j < FEATURE_COUNT; ++j)
        decision.experience.gradient[j] = scale * (roots[chosen].features.value[j] - expected[j]);
    decision.experience.stateFeatures = ExtractFeatures(board,0).value;
    decision.experience.piece = piece;
    decision.experience.rotation = roots[chosen].move.rotation;
    decision.experience.x = roots[chosen].move.x;
    decision.experience.nextCount = (std::min)(6,static_cast<int>(nextQueue.size()));
    for(int i=0;i<decision.experience.nextCount;++i)
        decision.experience.nextPieces[i] = nextQueue[static_cast<size_t>(i)];
    decision.move = roots[chosen].move;
    decision.move.score = logits[chosen];
    decision.predictedLines = roots[chosen].linesCleared;
    decision.searchedNextPieces = lookaheadDepth;
    return true;
}

