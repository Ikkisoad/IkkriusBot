"""Summarize match diagnostics. Usage: python tools/analyze_match.py [log.jsonl|directory]"""
import argparse
from collections import Counter
import json
from pathlib import Path


def summarize(path):
    records, malformed = [], 0
    with Path(path).open(encoding="utf-8", errors="replace") as stream:
        for line in stream:
            try:
                record = json.loads(line)
                if isinstance(record, dict):
                    records.append(record)
            except json.JSONDecodeError:
                malformed += 1  # A killed process may leave an incomplete final line.
    snapshots = [r for r in records if r.get("event") == "snapshot"]
    start = next((r for r in records if r.get("event") == "start"), {})
    end = next((r for r in reversed(records) if r.get("event") == "end"), {})
    losses = Counter(r.get("type", "unknown") for r in records
                     if r.get("event") == "destroy" and r.get("side") == "self")
    queenSpells = Counter(r.get("detail", "unknown").split(" ", 1)[0] for r in records if r.get("event") == "queen_spell")
    airMorphs = Counter(r.get("detail", "unknown") for r in records if r.get("event") == "air_morph")
    support = [r for r in records if r.get("event") == "queen_support"]
    failures = Counter((r.get("action"), r.get("type"), r.get("reason")) for r in records
                       if r.get("event") == "command" and not r.get("accepted"))
    blocked_frames = float_frames = 0
    for a, b in zip(snapshots, snapshots[1:]):
        interval = max(0, min(120, b.get("frame", 0) - a.get("frame", 0)))
        if a.get("supply_total", 0) < 400 and a.get("supply_used", 0) >= a.get("supply_total", 0):
            blocked_frames += interval
        if a.get("minerals", 0) >= 800:
            float_frames += interval
    race = snapshots[-1].get("race", start.get("race", "unknown")) if snapshots else start.get("race", "unknown")
    if race.lower() == "unknown":
        observed = Counter()
        for snapshot in snapshots:
            for name, count in snapshot.get("visible_enemies", {}).items():
                prefix = name.split("_", 1)[0]
                if prefix in ("Terran", "Protoss", "Zerg"):
                    observed[prefix] += count
        if observed:
            race = observed.most_common(1)[0][0] + " (inferred from visible units)"
    return {
        "file": str(path), "revision": start.get("revision", "unknown"), "build": start.get("build", "unknown"),
        "map": start.get("map", "unknown"), "race": race,
        "result": end.get("result", "incomplete"),
        "frames": end.get("frame", records[-1].get("frame", 0) if records else 0),
        "snapshots": len(snapshots), "malformed_lines": malformed,
        "peak_minerals": max((r.get("minerals", 0) for r in snapshots), default=0),
        "peak_gas": max((r.get("gas", 0) for r in snapshots), default=0),
        "peak_workers": max((r.get("workers", 0) for r in snapshots), default=0),
        "peak_army_supply": max((r.get("army_supply", 0) / 2 for r in snapshots), default=0),
        "estimated_supply_block_frames": blocked_frames,
        "estimated_high_mineral_frames": float_frames,
        "losses": dict(losses),
        "queen_spells_accepted": dict(queenSpells),
        "air_morphs_accepted": dict(airMorphs),
        "lurker_burrows": sum(r.get("event") == "lurker_burrow" for r in records),
        "lurker_unburrows": sum(r.get("event") == "lurker_unburrow" for r in records),
        "surplus_expansions": sum(r.get("event") == "surplus_expansion" for r in records),
        "pressure_waves": sum(r.get("event") == "pressure_start" for r in records),
        "last_queen_support": support[-1] if support else None,
        "sampled_failures": [{"action": a, "type": t, "reason": reason, "samples": n}
                             for (a, t, reason), n in failures.most_common(10)],
        "decisions": [{"frame": r.get("frame"), "detail": r.get("detail")}
                      for r in records if r.get("event") in ("decision", "attack", "regroup", "pressure_start", "pressure_end", "surplus_expansion")][-20:],
        "last_snapshot": snapshots[-1] if snapshots else None,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "bin" / "logs")
    args = parser.parse_args()
    files = sorted(args.path.glob("match-*.jsonl")) if args.path.is_dir() else [args.path]
    files = [path for path in files if path.is_file()]
    if not files:
        parser.exit(1, "No match logs found. Start a match with the instrumented bot first.\n")
    print(json.dumps([summarize(path) for path in files], indent=2))


if __name__ == "__main__":
    main()
