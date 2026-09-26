"""Compile and run the BWAPI-free learning core used by the Adaptive build order.

Run from a VS 2022 developer prompt (or any shell with g++): python tests/learning_regression.py
Covers genome decoding, genetic evolution, composition learning, switching hysteresis and persistence.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LEARNING = ROOT / 'src/starterbot/learning'

checks = r'''
#include "Learning.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
using namespace Learning;

EnemyProfile Profile(std::initializer_list<std::pair<Feature, double>> values) {
    EnemyProfile profile;
    for (auto [feature, value] : values) profile[feature] = value;
    return profile;
}

Composition BestPrior(const EnemyProfile& profile) {
    int best = 0;
    for (int i = 1; i < CompositionCount; ++i)
        if (CounterScore(Composition(i), profile) > CounterScore(Composition(best), profile)) best = i;
    return Composition(best);
}

int main() {
    // Every requested build is available by name and by 1-9 shortcut.
    for (int i = 0; i < CompositionCount; ++i) {
        Composition parsed;
        assert(ParseComposition(CompositionName(Composition(i)), parsed) && parsed == Composition(i));
        assert(ParseComposition(std::to_string(i + 1), parsed) && parsed == Composition(i));
        double total = 0;
        for (double share : Spec(Composition(i)).share) total += share;
        assert(std::abs(total - 1.0) < 1e-9);
    }

    // Genes decode to their documented defaults and stay inside bounds.
    const auto genome = Genome::Default();
    for (int i = 0; i < GeneCount; ++i) {
        const auto& info = Info(Gene(i));
        assert(std::abs(genome.Get(Gene(i)) - info.defaultValue) < 1e-6);
    }
    Genome extreme; extreme.raw.fill(5.0);
    assert(extreme.GetInt(Gene::PoolDrones) == 14);

    // Counter prior: sensible picks for clear-cut enemy tech.
    assert(BestPrior(Profile({{Feature::Air, 0.8}, {Feature::CapitalAir, 0.7}})) == Composition::MassMutaDevourer);
    assert(BestPrior(Profile({{Feature::StaticDefense, 1.0}, {Feature::Heavy, 0.6}})) == Composition::GuardianRush ||
           BestPrior(Profile({{Feature::StaticDefense, 1.0}, {Feature::Heavy, 0.6}})) == Composition::LingMutaGuardian);
    assert(BestPrior(Profile({{Feature::Small, 0.9}})) == Composition::LurkerQueenMuta);
    assert(BestPrior(Profile({{Feature::Early, 1.0}})) == Composition::ZerglingQueenRush);
    assert(ContextKey("Terran", Profile({})) != ContextKey("Protoss", Profile({})));
    assert(ContextKey("Zerg", Profile({{Feature::Air, 0.5}})) != ContextKey("Zerg", Profile({})));

    // Genetic algorithm: every individual is evaluated before breeding, and fitness drives selection.
    std::mt19937 rng(7);
    Population population;
    population.Seed(rng);
    assert(int(population.Individuals().size()) == Population::Size);
    const auto reward = [](const Genome& g) {
        // Synthetic landscape: the "best" timings are a 13-drone pool and a 30-supply attack.
        return std::exp(-std::pow(g.Get(Gene::PoolDrones) - 13, 2) / 4.0 - std::pow(g.Get(Gene::AttackSupply) - 30, 2) / 200.0);
    };
    double firstBest = 0;
    for (const auto& individual : population.Individuals()) firstBest = std::max(firstBest, reward(individual.genome));
    for (int game = 0; game < 600; ++game) {
        const int index = population.Select(rng);
        assert(population.Individuals()[index].games < Population::EvaluationsPerIndividual);
        population.Report(index, reward(population.Individuals()[index].genome));
    }
    assert(population.Generation() >= 20);
    double bestFitness = 0; Genome best;
    for (const auto& individual : population.Individuals()) {
        for (double gene : individual.genome.raw) assert(gene >= 0.0 && gene <= 1.0);
        if (individual.games > 0 && individual.rewardSum / individual.games > bestFitness) {
            bestFitness = individual.rewardSum / individual.games; best = individual.genome;
        }
    }
    std::cout << "GA best reward " << firstBest << " -> " << bestFitness << " (pool " << best.Get(Gene::PoolDrones)
              << ", attack " << best.Get(Gene::AttackSupply) << ")\n";
    assert(bestFitness > 0.9 && bestFitness >= firstBest - 1e-9);

    // Composition bandit: repeated wins with one composition in a context outweigh the prior.
    Learner learner(11);
    learner.BeginGame();
    std::array<double, CompositionCount> noTech{};
    const auto opening = Profile({{Feature::Early, 1.0}});
    const int context = ContextKey("Protoss", opening);
    for (int game = 0; game < 40; ++game) {
        learner.BeginGame();
        const auto decision = learner.Choose(context, opening, noTech, false, Composition::LingMutaQueen, 0, true);
        learner.RecordActive(context, decision.composition, 600);
        learner.EndGame(decision.composition == Composition::HydraQueenRush ? 1.0 : 0.0);
    }
    int hydra = 0;
    for (int trial = 0; trial < 50; ++trial)
        hydra += learner.Choose(context, opening, noTech, false, Composition::LingMutaQueen, 0, true).composition == Composition::HydraQueenRush;
    std::cout << "bandit picked HydraQueenRush " << hydra << "/50 after learning\n";
    assert(hydra >= 40);
    // Unseen contexts of the same race borrow what the race has taught.
    const auto similar = Profile({{Feature::Early, 1.0}, {Feature::Heavy, 0.5}});
    assert(learner.GetBandit().Estimate(ContextKey("Protoss", similar), Composition::HydraQueenRush) >
           learner.GetBandit().Estimate(ContextKey("Protoss", similar), Composition::ZerglingQueenRush));

    // Mid-match switching: blocked by the cooldown, allowed once it expires, skipped when not clearly better.
    Learner switcher(3);
    switcher.BeginGame(false);
    const auto airHeavy = Profile({{Feature::Air, 0.9}, {Feature::CapitalAir, 0.8}});
    const int airContext = ContextKey("Terran", airHeavy);
    auto early = switcher.Choose(airContext, airHeavy, noTech, true, Composition::ZerglingQueenRush, 10, false);
    assert(!early.switched && early.composition == Composition::ZerglingQueenRush);
    auto late = switcher.Choose(airContext, airHeavy, noTech, true, Composition::ZerglingQueenRush, 400, false);
    assert(late.switched && late.composition == Composition::MassMutaDevourer);
    auto stay = switcher.Choose(airContext, airHeavy, noTech, true, Composition::MassMutaDevourer, 400, false);
    assert(!stay.switched);
    // Tech already owned makes the cheaper transition preferable.
    std::array<double, CompositionCount> ownsLurkerTech{};
    ownsLurkerTech[int(Composition::LurkerQueenMuta)] = 1.0;
    const auto mixed = Profile({{Feature::Small, 0.4}, {Feature::Air, 0.2}});
    const auto withTech = switcher.Choose(ContextKey("Terran", mixed), mixed, ownsLurkerTech, true, Composition::LingMutaQueen, 400, false);
    const auto withoutTech = switcher.Choose(ContextKey("Terran", mixed), mixed, noTech, true, Composition::LingMutaQueen, 400, false);
    assert(withTech.scores[int(Composition::LurkerQueenMuta)] > withoutTech.scores[int(Composition::LurkerQueenMuta)]);

    // Credit is shared by the time each composition was active.
    Learner credit(5);
    credit.BeginGame();
    credit.RecordActive(1, Composition::MutaQueenRush, 300);
    credit.RecordActive(1, Composition::LingMutaGuardian, 900);
    credit.EndGame(1.0);
    assert(std::abs(credit.GetBandit().Visits(1, Composition::MutaQueenRush) - 0.25) < 1e-9);
    assert(std::abs(credit.GetBandit().Visits(1, Composition::LingMutaGuardian) - 0.75) < 1e-9);

    // Persistence round trip, including tolerance for unknown genes.
    const std::string path = "learning_state.txt";
    assert(learner.Save(path));
    Learner restored(99);
    assert(restored.Load(path));
    assert(restored.GetPopulation().Generation() == learner.GetPopulation().Generation());
    assert(restored.GetPopulation().Individuals().size() == learner.GetPopulation().Individuals().size());
    for (size_t i = 0; i < learner.GetPopulation().Individuals().size(); ++i) {
        const auto& a = learner.GetPopulation().Individuals()[i];
        const auto& b = restored.GetPopulation().Individuals()[i];
        assert(a.games == b.games);
        for (int g = 0; g < GeneCount; ++g) assert(std::abs(a.genome.raw[g] - b.genome.raw[g]) < 1e-5);
    }
    assert(std::abs(restored.GetBandit().Estimate(context, Composition::HydraQueenRush) -
                    learner.GetBandit().Estimate(context, Composition::HydraQueenRush)) < 1e-5);
    {
        FILE* file = std::fopen("old.txt", "w");
        std::fputs("IKKRIUS_LEARNING 1\ngeneration 4\ngenes 2 PoolDrones RetiredGene\nindividual 1 0.5 1.0 0.3\n", file);
        std::fclose(file);
    }
    Learner old(1);
    assert(old.Load("old.txt"));
    assert(old.GetPopulation().Generation() == 4);
    assert(old.GetPopulation().Individuals()[0].genome.GetInt(Gene::PoolDrones) == 14);
    assert(std::abs(old.GetPopulation().Individuals()[0].genome.Get(Gene::AttackSupply) - 40) < 1e-6);
    assert(!old.Load("missing.txt"));

    // Rewards stay in [0, 1] and always rank a win above a loss.
    assert(Reward(true, 0, 5000, 3) > Reward(false, 5000, 0, 60));
    for (double kills : {0.0, 100.0, 1e6}) for (double losses : {0.0, 100.0, 1e6}) {
        assert(Reward(true, kills, losses, 10) <= 1.0 && Reward(false, kills, losses, 90) >= 0.0);
    }
    std::cout << "learning regression passed\n";
}
'''

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    (directory / 'learning.cpp').write_text(checks)
    sources = ['learning.cpp', str(LEARNING / 'Learning.cpp')]
    if shutil.which('cl'):
        subprocess.run(['cl', '/nologo', '/EHsc', '/std:c++20', '/I' + str(LEARNING), *sources, '/Fe:learning.exe'],
                       cwd=directory, check=True)
        executable = directory / 'learning.exe'
    else:
        subprocess.run(['g++', '-std=c++20', '-O1', '-I' + str(LEARNING), *sources, '-o', 'learning'], cwd=directory, check=True)
        executable = directory / 'learning'
    subprocess.run([str(executable)], cwd=directory, check=True)
