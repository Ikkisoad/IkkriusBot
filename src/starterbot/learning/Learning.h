#pragma once
#include <array>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

// BWAPI-free learning core used by the Adaptive build order.
// - A genetic algorithm evolves *when* build steps happen (drone counts, army sizes, switch patience).
// - A contextual bandit (Monte-Carlo reinforcement learning) learns *which* composition to play
//   against a given enemy tech profile, and may switch compositions mid-match.
// Kept free of BWAPI so it can be tested by tests/learning_regression.py without StarCraft.
namespace Learning {

    enum class Composition {
        ZerglingQueenRush,
        HydraQueenRush,
        MutaQueenRush,
        GuardianRush,
        MassMutaDevourer,
        LingMutaQueen,
        LingMutaGuardian,
        LurkerQueenMuta,
        LingQueenUltra,
        Count
    };
    constexpr int CompositionCount = static_cast<int>(Composition::Count);
    const char* CompositionName(Composition composition);
    bool ParseComposition(const std::string& name, Composition& composition);

    // Army share targets are expressed as a fraction of army supply.
    enum class Army { Zergling, Hydralisk, Mutalisk, Guardian, Devourer, Lurker, Ultralisk, Count };
    constexpr int ArmyCount = static_cast<int>(Army::Count);

    struct CompositionSpec {
        bool rush;    // Hit early with a capped economy, then macro behind it.
        bool queens;  // Keep a Queen squad (Ensnare / Spawn Broodlings) with the army.
        std::array<double, ArmyCount> share;
        double Share(Army army) const { return share[static_cast<int>(army)]; }
    };
    const CompositionSpec& Spec(Composition composition);

    // Enemy tech features, each roughly in [0, 1].
    enum class Feature { Air, AntiAir, Splash, AirSplash, Heavy, Small, StaticDefense, CapitalAir, Detection, Early, Count };
    constexpr int FeatureCount = static_cast<int>(Feature::Count);

    struct EnemyProfile {
        std::array<double, FeatureCount> value{};
        double& operator[](Feature feature) { return value[static_cast<int>(feature)]; }
        double operator[](Feature feature) const { return value[static_cast<int>(feature)]; }
    };

    // Hand-written counter knowledge used as the prior before the bandit has data.
    double CounterScore(Composition composition, const EnemyProfile& profile);
    // Discretised enemy tech signature, combined with the race, used as the bandit context.
    int ContextKey(const std::string& enemyRace, const EnemyProfile& profile);

    enum class Gene {
        PoolDrones, GasDrones, ExpandDrones, OpeningLings, RushDroneCap,
        LairDrones, DenDrones, SpireDrones, QueensNestDrones, HiveDrones,
        GreaterSpireMutas, UltraCavernDrones, LurkerHydras, EvoDrones, SecondGasDrones,
        ThirdBaseDrones, DronesPerBase, MaxDrones, ArmyPerDrone, QueenCount,
        RushAttackSupply, AttackSupply, RetreatFraction, UpgradeArmySupply,
        SwitchMargin, SwitchCooldownSeconds, CounterWeight,
        Count
    };
    constexpr int GeneCount = static_cast<int>(Gene::Count);

    struct GeneInfo {
        const char* name;
        double min;
        double max;
        double defaultValue;
        bool integer;
    };
    const GeneInfo& Info(Gene gene);

    // Genes are stored normalised to [0, 1] so mutation and crossover treat all of them evenly.
    struct Genome {
        std::array<double, GeneCount> raw{};
        static Genome Default();
        double Get(Gene gene) const;
        int GetInt(Gene gene) const;
        void Set(Gene gene, double value);
        std::string Describe() const;
    };

    struct Individual {
        Genome genome;
        int games = 0;
        double rewardSum = 0.0;
        double Fitness() const;
    };

    // Steady-state genetic algorithm: evaluate every individual a few times, then breed.
    class Population {
    public:
        static constexpr int Size = 10;
        static constexpr int EvaluationsPerIndividual = 2;
        static constexpr int Elites = 3;

        void Seed(std::mt19937& rng);
        int Select(std::mt19937& rng);  // Index of the individual to play next; evolves when due.
        void Report(int index, double reward);
        void Evolve(std::mt19937& rng);
        int Generation() const { return m_generation; }
        std::vector<Individual>& Individuals() { return m_individuals; }
        const std::vector<Individual>& Individuals() const { return m_individuals; }
        void SetGeneration(int generation) { m_generation = generation; }

        static Genome Mutate(const Genome& genome, std::mt19937& rng, double rate, double sigma);
        static Genome Crossover(const Genome& a, const Genome& b, std::mt19937& rng);

    private:
        int Tournament(std::mt19937& rng) const;
        std::vector<Individual> m_individuals;
        int m_generation = 0;
    };

    struct ArmStats {
        double visits = 0.0;
        double value = 0.5;
    };

    // Contextual bandit with Monte-Carlo credit assignment: each composition that was active
    // receives the final match reward weighted by the share of the match it was active for.
    class CompositionBandit {
    public:
        double Estimate(int context, Composition composition) const;
        double Visits(int context, Composition composition) const;
        double ContextVisits(int context) const;
        void Update(int context, Composition composition, double weight, double reward);
        std::map<std::pair<int, int>, ArmStats>& Arms() { return m_arms; }
        const std::map<std::pair<int, int>, ArmStats>& Arms() const { return m_arms; }

    private:
        std::map<std::pair<int, int>, ArmStats> m_arms;
    };

    struct Decision {
        Composition composition;
        bool switched;
        std::array<double, CompositionCount> scores;
    };

    class Learner {
    public:
        explicit Learner(unsigned seed = std::random_device{}());

        bool Load(const std::string& path);
        bool Save(const std::string& path) const;
        void Reset();

        // Called once at match start: returns the timing genome to play.
        const Genome& BeginGame(bool explore = true);
        const Genome& CurrentGenome() const;
        int CurrentIndividual() const { return m_current; }

        // ownedTech[c] is the fraction of composition c's tech buildings we already own (switch cost).
        Decision Choose(int context, const EnemyProfile& profile, const std::array<double, CompositionCount>& ownedTech,
                        bool hasCurrent, Composition current, double secondsSinceSwitch, bool opening);

        void RecordActive(int context, Composition composition, double seconds);
        void EndGame(double reward);

        Population& GetPopulation() { return m_population; }
        CompositionBandit& GetBandit() { return m_bandit; }
        std::mt19937& Rng() { return m_rng; }

    private:
        std::mt19937 m_rng;
        Population m_population;
        CompositionBandit m_bandit;
        int m_current = -1;
        bool m_explore = true;
        Genome m_fallback = Genome::Default();
        std::map<std::pair<int, int>, double> m_activeSeconds;
    };

    // Match reward in [0, 1]: mostly the result, shaped by trades and survival time.
    double Reward(bool won, double killScore, double lossScore, double minutes);
}
