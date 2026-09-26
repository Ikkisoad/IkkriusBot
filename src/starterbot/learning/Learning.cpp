#include "Learning.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <sstream>

namespace Learning {

namespace {
    const char* compositionNames[CompositionCount] = {
        "ZerglingQueenRush", "HydraQueenRush", "MutaQueenRush", "GuardianRush", "MassMutaDevourer",
        "LingMutaQueen", "LingMutaGuardian", "LurkerQueenMuta", "LingQueenUltra",
        "HydraQueenUltra", "ScourgeQueenUltra"
    };

    //                          Ling  Hydra Muta  Guard Devour Lurker Ultra Scourge
    const CompositionSpec specs[CompositionCount] = {
        { true,  true,  {1.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00} }, // Zergling + Queen rush
        { true,  true,  {0.00, 1.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.00} }, // Hydra + Queen rush
        { true,  true,  {0.00, 0.00, 1.00, 0.00, 0.00, 0.00, 0.00, 0.00} }, // Muta + Queen rush
        { true,  false, {0.00, 0.00, 0.20, 0.80, 0.00, 0.00, 0.00, 0.00} }, // Guardian rush (Mutas are the morph source/escort)
        { false, false, {0.00, 0.00, 0.83, 0.00, 0.17, 0.00, 0.00, 0.00} }, // Mass Muta + Devourer (5 Mutas per Devourer)
        { false, true,  {0.50, 0.00, 0.50, 0.00, 0.00, 0.00, 0.00, 0.00} }, // Ling + Muta + Queen
        { false, false, {0.35, 0.00, 0.30, 0.35, 0.00, 0.00, 0.00, 0.00} }, // Ling + Muta + Guardian
        { false, true,  {0.00, 0.10, 0.50, 0.00, 0.00, 0.40, 0.00, 0.00} }, // Lurker + Queen + Muta
        { false, true,  {0.50, 0.00, 0.00, 0.00, 0.00, 0.00, 0.50, 0.00} }, // Ling + Queen + Ultralisk
        { false, true,  {0.00, 0.55, 0.00, 0.00, 0.00, 0.00, 0.45, 0.00} }, // Hydra + Queen + Ultralisk: ranged anti-air plus tanks vs heavy air
        { false, true,  {0.00, 0.00, 0.00, 0.00, 0.00, 0.00, 0.65, 0.35} }, // Scourge + Queen + Ultralisk: cheap air snipers plus tanks vs heavy air
    };

    // Bias followed by one weight per Feature:
    //   Air  AntiAir Splash AirSplash Heavy Small Static Capital Detect Early
    const double counterWeights[CompositionCount][FeatureCount + 1] = {
        { 0.20, -0.8,  0.0, -0.6,  0.0, -0.5, -0.3, -0.6, -0.5,  0.0,  0.50 },
        { 0.20,  0.3,  0.0, -0.5,  0.0, -0.2, -0.2, -0.4, -0.2,  0.0,  0.40 },
        { 0.20, -0.3, -0.7,  0.2, -0.8,  0.2,  0.0, -0.4, -0.4,  0.0,  0.35 },
        { 0.10, -0.9, -0.3,  0.3, -0.2,  0.5,  0.1,  0.6, -0.6,  0.0,  0.20 },
        { 0.10,  0.6, -0.4,  0.1, -0.5,  0.0,  0.0, -0.2,  0.8,  0.0, -0.20 },
        { 0.30, -0.1, -0.3, -0.2, -0.4, -0.1,  0.0, -0.2, -0.2,  0.0,  0.00 },
        { 0.25, -0.4, -0.2,  0.0, -0.2,  0.3,  0.0,  0.4, -0.4,  0.0, -0.30 },
        { 0.25, -0.1,  0.0, -0.1, -0.3, -0.2,  0.7, -0.1, -0.3, -0.6, -0.20 },
        { 0.20, -0.6,  0.3,  0.2,  0.2,  0.3,  0.5,  0.1, -0.6,  0.0, -0.50 },
        { 0.15,  0.5, -0.3, -0.3, -0.3,  0.0, -0.2, -0.2, -0.3, -0.2, -0.20 }, // Hydra/Queen/Ultra: ranged AA + tanks, weak to splash
        { 0.10,  0.7, -0.2, -0.2, -0.6,  0.1,  0.0, -0.1,  0.3, -0.1, -0.40 }, // Scourge/Queen/Ultra: strong vs air incl. capital ships, dies to air splash
    };

    const GeneInfo genes[GeneCount] = {
        { "PoolDrones",            9,   14,   11,   true  },
        { "GasDrones",             9,   20,   12,   true  },
        { "ExpandDrones",          11,  22,   13,   true  },
        { "OpeningLings",          2,   12,   6,    true  },
        { "RushDroneCap",          10,  30,   18,   true  },
        { "LairDrones",            10,  28,   14,   true  },
        { "DenDrones",             10,  30,   14,   true  },
        { "SpireDrones",           12,  34,   16,   true  },
        { "QueensNestDrones",      12,  40,   18,   true  },
        { "HiveDrones",            16,  50,   24,   true  },
        { "GreaterSpireMutas",     0,   16,   4,    true  },
        { "UltraCavernDrones",     20,  55,   30,   true  },
        { "LurkerHydras",          2,   12,   4,    true  },
        { "EvoDrones",             14,  50,   24,   true  },
        { "SecondGasDrones",       14,  40,   20,   true  },
        { "ThirdBaseDrones",       20,  45,   28,   true  },
        { "DronesPerBase",         10,  20,   15,   true  },
        { "MaxDrones",             24,  72,   54,   true  },
        { "ArmyPerDrone",          0.2, 1.6,  0.6,  false },
        { "QueenCount",            1,   8,    3,    true  },
        { "RushAttackSupply",      6,   40,   14,   true  },
        { "AttackSupply",          20,  120,  40,   true  },
        { "RetreatFraction",       0.2, 0.8,  0.45, false },
        { "UpgradeArmySupply",     6,   60,   20,   true  },
        { "SwitchMargin",          0.05, 1.0, 0.30, false },
        { "SwitchCooldownSeconds", 45,  300,  120,  true  },
        { "CounterWeight",         0.2, 2.0,  1.0,  false },
    };

    double Clamp01(double value) { return std::clamp(value, 0.0, 1.0); }

    int RaceIndex(const std::string& race) {
        if (race == "Terran") return 0;
        if (race == "Protoss") return 1;
        if (race == "Zerg") return 2;
        return 3;
    }
}

const char* CompositionName(Composition composition) {
    const int index = static_cast<int>(composition);
    return index >= 0 && index < CompositionCount ? compositionNames[index] : "Unknown";
}

bool ParseComposition(const std::string& name, Composition& composition) {
    for (int i = 0; i < CompositionCount; ++i) {
        if (name == compositionNames[i] || name == std::to_string(i + 1)) {
            composition = static_cast<Composition>(i);
            return true;
        }
    }
    return false;
}

const CompositionSpec& Spec(Composition composition) { return specs[static_cast<int>(composition)]; }

double CounterScore(Composition composition, const EnemyProfile& profile) {
    const auto& weights = counterWeights[static_cast<int>(composition)];
    double score = weights[0];
    for (int i = 0; i < FeatureCount; ++i) score += weights[i + 1] * profile.value[i];
    return score;
}

int ContextKey(const std::string& enemyRace, const EnemyProfile& profile) {
    int bits = 0;
    if (profile[Feature::Air] >= 0.25) bits |= 1;
    if (profile[Feature::AntiAir] >= 0.5) bits |= 2;
    if (profile[Feature::Splash] + profile[Feature::AirSplash] >= 0.25) bits |= 4;
    if (profile[Feature::Heavy] >= 0.35) bits |= 8;
    if (profile[Feature::StaticDefense] >= 0.5) bits |= 16;
    if (profile[Feature::Early] >= 0.5) bits |= 32;
    return RaceIndex(enemyRace) * 64 + bits;
}

const GeneInfo& Info(Gene gene) { return genes[static_cast<int>(gene)]; }

Genome Genome::Default() {
    Genome genome;
    for (int i = 0; i < GeneCount; ++i) genome.Set(static_cast<Gene>(i), genes[i].defaultValue);
    return genome;
}

double Genome::Get(Gene gene) const {
    const auto& info = Info(gene);
    const double value = info.min + Clamp01(raw[static_cast<int>(gene)]) * (info.max - info.min);
    return info.integer ? std::round(value) : value;
}

int Genome::GetInt(Gene gene) const { return static_cast<int>(std::lround(Get(gene))); }

void Genome::Set(Gene gene, double value) {
    const auto& info = Info(gene);
    raw[static_cast<int>(gene)] = info.max > info.min ? Clamp01((value - info.min) / (info.max - info.min)) : 0.0;
}

std::string Genome::Describe() const {
    std::ostringstream text;
    for (int i = 0; i < GeneCount; ++i) {
        if (i) text << ' ';
        text << genes[i].name << '=' << Get(static_cast<Gene>(i));
    }
    return text.str();
}

double Individual::Fitness() const {
    // Shrink toward a neutral 0.5 so one lucky game does not dominate the population.
    return (rewardSum + 0.5) / (games + 1.0);
}

void Population::Seed(std::mt19937& rng) {
    m_individuals.clear();
    m_individuals.push_back({ Genome::Default() });
    while (static_cast<int>(m_individuals.size()) < Size)
        m_individuals.push_back({ Mutate(Genome::Default(), rng, 0.6, 0.2) });
    m_generation = 0;
}

int Population::Select(std::mt19937& rng) {
    if (m_individuals.empty()) Seed(rng);
    auto fewest = std::min_element(m_individuals.begin(), m_individuals.end(),
        [](const Individual& a, const Individual& b) { return a.games < b.games; });
    if (fewest->games >= EvaluationsPerIndividual) {
        Evolve(rng);
        fewest = std::min_element(m_individuals.begin(), m_individuals.end(),
            [](const Individual& a, const Individual& b) { return a.games < b.games; });
    }
    return static_cast<int>(fewest - m_individuals.begin());
}

void Population::Report(int index, double reward) {
    if (index < 0 || index >= static_cast<int>(m_individuals.size())) return;
    m_individuals[index].games += 1;
    m_individuals[index].rewardSum += Clamp01(reward);
}

int Population::Tournament(std::mt19937& rng) const {
    std::uniform_int_distribution<int> pick(0, static_cast<int>(m_individuals.size()) - 1);
    int best = pick(rng);
    for (int i = 0; i < 2; ++i) {
        const int candidate = pick(rng);
        if (m_individuals[candidate].Fitness() > m_individuals[best].Fitness()) best = candidate;
    }
    return best;
}

Genome Population::Mutate(const Genome& genome, std::mt19937& rng, double rate, double sigma) {
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::normal_distribution<double> noise(0.0, sigma);
    Genome child = genome;
    for (auto& gene : child.raw)
        if (chance(rng) < rate) gene = Clamp01(gene + noise(rng));
    return child;
}

Genome Population::Crossover(const Genome& a, const Genome& b, std::mt19937& rng) {
    std::bernoulli_distribution fromA(0.5);
    Genome child;
    for (int i = 0; i < GeneCount; ++i) child.raw[i] = fromA(rng) ? a.raw[i] : b.raw[i];
    return child;
}

void Population::Evolve(std::mt19937& rng) {
    if (m_individuals.empty()) { Seed(rng); return; }
    std::stable_sort(m_individuals.begin(), m_individuals.end(),
        [](const Individual& a, const Individual& b) { return a.Fitness() > b.Fitness(); });
    std::vector<Individual> next(m_individuals.begin(),
        m_individuals.begin() + std::min<size_t>(Elites, m_individuals.size()));
    // Elites keep their record but are re-evaluated once more so noisy results are corrected.
    for (auto& elite : next) {
        const int kept = std::min(elite.games, EvaluationsPerIndividual - 1);
        if (elite.games > 0) elite.rewardSum *= double(kept) / elite.games;
        elite.games = kept;
    }
    while (static_cast<int>(next.size()) < Size) {
        const auto& a = m_individuals[Tournament(rng)].genome;
        const auto& b = m_individuals[Tournament(rng)].genome;
        next.push_back({ Mutate(Crossover(a, b, rng), rng, 0.2, 0.1) });
    }
    m_individuals = std::move(next);
    ++m_generation;
}

double CompositionBandit::Estimate(int context, Composition composition) const {
    // Pool experience across contexts of the same race when this exact context is new.
    const int comp = static_cast<int>(composition);
    const auto exact = m_arms.find({ context, comp });
    double visits = 0, total = 0;
    for (const auto& [key, arm] : m_arms) {
        if (key.second != comp || key.first / 64 != context / 64 || key.first == context) continue;
        visits += arm.visits; total += arm.visits * arm.value;
    }
    const double raceValue = visits > 0 ? (total + 1.0) / (visits + 2.0) : 0.5;
    if (exact == m_arms.end()) return raceValue;
    constexpr double priorWeight = 2.0;
    return (exact->second.visits * exact->second.value + priorWeight * raceValue) / (exact->second.visits + priorWeight);
}

double CompositionBandit::Visits(int context, Composition composition) const {
    const auto arm = m_arms.find({ context, static_cast<int>(composition) });
    return arm == m_arms.end() ? 0.0 : arm->second.visits;
}

double CompositionBandit::ContextVisits(int context) const {
    double visits = 0;
    for (const auto& [key, arm] : m_arms) if (key.first == context) visits += arm.visits;
    return visits;
}

void CompositionBandit::Update(int context, Composition composition, double weight, double reward) {
    if (weight <= 0) return;
    auto& arm = m_arms[{ context, static_cast<int>(composition) }];
    arm.visits += weight;
    arm.value += weight * (Clamp01(reward) - arm.value) / arm.visits;
}

Learner::Learner(unsigned seed) : m_rng(seed) {}

void Learner::Reset() {
    m_population = Population();
    m_bandit = CompositionBandit();
    m_current = -1;
    m_activeSeconds.clear();
}

bool Learner::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file) return false;
    std::string tag;
    int version = 0;
    if (!(file >> tag >> version) || tag != "IKKRIUS_LEARNING" || version != 1) return false;
    Population population;
    CompositionBandit bandit;
    std::vector<int> geneOrder;
    std::string line;
    while (file >> tag) {
        if (tag == "generation") {
            int generation = 0; file >> generation; population.SetGeneration(generation);
        } else if (tag == "genes") {
            int count = 0; file >> count;
            geneOrder.assign(count, -1);
            for (int i = 0; i < count; ++i) {
                std::string name; file >> name;
                for (int g = 0; g < GeneCount; ++g) if (name == genes[g].name) geneOrder[i] = g;
            }
        } else if (tag == "individual") {
            Individual individual{ Genome::Default() };
            file >> individual.games >> individual.rewardSum;
            // Unknown genes are skipped and new genes keep their defaults, so saves survive gene-list edits.
            for (int index : geneOrder) {
                double value = 0; file >> value;
                if (index >= 0) individual.genome.raw[index] = Clamp01(value);
            }
            population.Individuals().push_back(individual);
        } else if (tag == "arm") {
            int context = 0; std::string name; ArmStats arm;
            file >> context >> name >> arm.visits >> arm.value;
            Composition composition;
            if (ParseComposition(name, composition)) bandit.Arms()[{ context, static_cast<int>(composition) }] = arm;
        } else {
            std::getline(file, line);
        }
        if (!file) return false;
    }
    m_population = std::move(population);
    m_bandit = std::move(bandit);
    return true;
}

bool Learner::Save(const std::string& path) const {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) return false;
        file << "IKKRIUS_LEARNING 1\n";
        file << "generation " << m_population.Generation() << "\n";
        file << "genes " << GeneCount;
        for (const auto& gene : genes) file << ' ' << gene.name;
        file << "\n";
        file.precision(6);
        for (const auto& individual : m_population.Individuals()) {
            file << "individual " << individual.games << ' ' << individual.rewardSum;
            for (double gene : individual.genome.raw) file << ' ' << gene;
            file << "\n";
        }
        for (const auto& [key, arm] : m_bandit.Arms())
            file << "arm " << key.first << ' ' << compositionNames[key.second] << ' ' << arm.visits << ' ' << arm.value << "\n";
        if (!file) return false;
    }
    std::remove(path.c_str());
    return std::rename(temporary.c_str(), path.c_str()) == 0;
}

const Genome& Learner::BeginGame(bool explore) {
    m_activeSeconds.clear();
    m_explore = explore;
    auto& individuals = m_population.Individuals();
    if (explore) {
        m_current = m_population.Select(m_rng);
    } else if (!individuals.empty()) {
        m_current = static_cast<int>(std::max_element(individuals.begin(), individuals.end(),
            [](const Individual& a, const Individual& b) { return a.Fitness() < b.Fitness(); }) - individuals.begin());
    } else {
        m_current = -1;
    }
    return CurrentGenome();
}

const Genome& Learner::CurrentGenome() const {
    const auto& individuals = m_population.Individuals();
    return m_current >= 0 && m_current < static_cast<int>(individuals.size()) ? individuals[m_current].genome : m_fallback;
}

Decision Learner::Choose(int context, const EnemyProfile& profile, const std::array<double, CompositionCount>& ownedTech,
                         bool hasCurrent, Composition current, double secondsSinceSwitch, bool opening) {
    const auto& genome = CurrentGenome();
    const double counterWeight = genome.Get(Gene::CounterWeight);
    const double totalVisits = m_bandit.ContextVisits(context);
    Decision decision{ hasCurrent ? current : Composition::LingMutaQueen, false, {} };
    int best = -1;
    for (int i = 0; i < CompositionCount; ++i) {
        const auto composition = static_cast<Composition>(i);
        const double learned = m_bandit.Estimate(context, composition) - 0.5;
        // UCB exploration only at the opening; mid-match choices exploit what has been learned.
        const double explore = opening && m_explore ? 0.35 * std::sqrt(std::log(totalVisits + 2.0) / (m_bandit.Visits(context, composition) + 1.0)) : 0.0;
        const double techBonus = opening ? 0.0 : 0.35 * ownedTech[i];
        decision.scores[i] = counterWeight * CounterScore(composition, profile) + learned + explore + techBonus;
        if (best < 0 || decision.scores[i] > decision.scores[best]) best = i;
    }
    if (opening) {
        std::uniform_real_distribution<double> chance(0.0, 1.0);
        if (m_explore && chance(m_rng) < 0.1) best = std::uniform_int_distribution<int>(0, CompositionCount - 1)(m_rng);
        decision.switched = !hasCurrent || static_cast<Composition>(best) != current;
        decision.composition = static_cast<Composition>(best);
        return decision;
    }
    if (!hasCurrent) {
        decision.composition = static_cast<Composition>(best);
        decision.switched = true;
        return decision;
    }
    // Hysteresis: a switch must be clearly better and the previous plan must have had time to work.
    const int currentIndex = static_cast<int>(current);
    if (best != currentIndex && secondsSinceSwitch >= genome.Get(Gene::SwitchCooldownSeconds) &&
        decision.scores[best] >= decision.scores[currentIndex] + genome.Get(Gene::SwitchMargin)) {
        decision.composition = static_cast<Composition>(best);
        decision.switched = true;
    }
    return decision;
}

void Learner::RecordActive(int context, Composition composition, double seconds) {
    if (seconds > 0) m_activeSeconds[{ context, static_cast<int>(composition) }] += seconds;
}

void Learner::EndGame(double reward) {
    m_population.Report(m_current, reward);
    double total = 0;
    for (const auto& [key, seconds] : m_activeSeconds) total += seconds;
    if (total > 0) {
        for (const auto& [key, seconds] : m_activeSeconds)
            m_bandit.Update(key.first, static_cast<Composition>(key.second), seconds / total, reward);
    }
    m_activeSeconds.clear();
}

double Reward(bool won, double killScore, double lossScore, double minutes) {
    const double trade = killScore + lossScore > 0 ? killScore / (killScore + lossScore) : 0.5;
    if (won) return Clamp01(0.8 + 0.2 * trade);
    return Clamp01(0.2 * trade + 0.1 * std::min(1.0, minutes / 25.0));
}

}
