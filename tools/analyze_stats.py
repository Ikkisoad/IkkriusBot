"""Summarize the win-rate CSV per strategy and enemy race.
Usage: python tools/analyze_stats.py [strats.csv] [--strategy Adaptive] [--by-opponent]"""
import argparse
from collections import defaultdict
import csv
from pathlib import Path


def race_of(opponent, race):
    """Use the recorded race; for older "Unknown" rows fall back to the built-in AI faction names."""
    if race not in ("Unknown", "Random", "None", ""):
        return race
    if opponent.endswith("Brood"):
        return "Zerg (inferred from name)"
    if opponent.endswith("Tribe"):
        return "Protoss (inferred from name)"
    if opponent in ("Mar Sara", "Antiga", "Kel-Morian Combine", "Elite Guard", "Cronus Wing", "Atlas Wing",
                    "Delta Squadron", "Epsilon Squadron", "Alpha Squadron", "Omega Squadron"):
        return "Terran (inferred from name)"
    return "Unknown"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "src/starterbot/stats/data/strats-v1.csv")
    parser.add_argument("--strategy", help="only show this strategy")
    parser.add_argument("--by-opponent", action="store_true", help="group by opponent name instead of race")
    args = parser.parse_args()
    totals = defaultdict(lambda: [0, 0])
    with args.path.open(encoding="utf-8") as stream:
        for row in csv.reader(stream):
            if len(row) < 6 or (args.strategy and row[3] != args.strategy):
                continue
            opponent, race, _, strategy, games, wins = row[:6]
            group = opponent if args.by_opponent else race_of(opponent, race)
            totals[(strategy, group)][0] += int(games)
            totals[(strategy, group)][1] += int(wins)
    for (strategy, group), (games, wins) in sorted(totals.items()):
        print(f"{strategy:12s} {group:32s} {wins:5d}/{games:<5d} {100 * wins / games:5.1f}%")


if __name__ == "__main__":
    main()
