#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run repeatable, private bot matches and report observable strategy behaviour."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
WORLDS = {"classic", "desert", "jungle", "winter", "volcanic", "wasteland", "moon", "swamp"}
GOALS = {"flag", "conquest", "hill", "annihilation", "deathmatch", "warlord", "killconfirmed", "hardpoint"}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def map_goals(path):
    fields = {}
    for line in path.read_text().splitlines():
        if line == "tiles":
            break
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key] = value.strip()
    if fields.get("biome", "classic") not in WORLDS:
        return []
    authored = fields.get("mode")
    if authored in GOALS | {"classic"}:
        return ["flag" if authored == "classic" else authored]
    if fields.get("biome", "classic") == "classic":
        return ["flag"]
    raw = fields.get("modes", "").strip()
    goals = set(raw.replace(",", " ").split()) if raw else (
        {"annihilation"} if fields.get("arena") == "yes" else {"flag", "conquest", "deathmatch", "warlord"})
    if goals & {"conquest", "hardpoint"}:
        goals.update(("conquest", "hardpoint"))
    if goals & {"deathmatch", "killconfirmed"}:
        goals.update(("deathmatch", "killconfirmed"))
    return sorted(goals & GOALS)


def report(folder, records):
    good = [r for r in records if r["status"] == "ok"]
    failed = sum(r["status"] in {"error", "timeout"} for r in records)
    stopped = len(records) - len(good) - failed
    lines = ["# OpenFP bot strategy baseline", "",
             f"Completed: {len(good)} matches. Errors/timeouts: {failed}. Budget/cancelled: {stopped}.",
             f"Natural finishes: {sum(not r['result']['time_up'] for r in good)}. "
             f"Time limits reached: {sum(r['result']['time_up'] for r in good)}.", "",
             "Time-limit points wins are kept separate from objective/elimination finishes.",
             "Percentages use live bot time, excluding respawning and eliminated tanks. Base and AI-intent metrics overlap.",
             "Attack time counts raid, flag-seeking, hunting and control-point/hill intent; it is not a count of combat time.",
             "Low progress means less than 2 pixels on each axis over a full second while travelling; "
             "turning or combat can also cause it, so inspect the replay before calling it a routing fault.", "",
             "| Profile / difficulty / mode | Matches | Natural finishes | Base time | Attack time | Repair time | Flags picked up |",
             "|---|---:|---:|---:|---:|---:|---:|"]
    groups = {}
    for row in good:
        key = (row["profile"], row["level"], row["goal"])
        groups.setdefault(key, []).append(row)
    for key, rows in sorted(groups.items()):
        teams = [t for r in rows for t in r["result"]["teams"]]
        alive = sum(t["alive_ticks"] for t in teams)
        pct = lambda field: f"{100 * sum(t[field] for t in teams) / max(alive, 1):.1f}%"
        lines.append(f"| {' / '.join(key)} | {len(rows)} | "
                     f"{sum(not r['result']['time_up'] for r in rows)} | {pct('home_ticks')} | "
                     f"{pct('attack_ticks')} | {pct('repair_ticks')} | {sum(t['flag_pickups'] for t in teams)} |")
    lines += ["", "## Matches to inspect", "",
              "Ranked by low-progress seconds. High base time alone can be valid defence or repair.", ""]
    ranked = sorted(good, key=lambda r: sum(t["low_progress_seconds"] for t in r["result"]["teams"]), reverse=True)[:12]
    for row in ranked:
        low = sum(t["low_progress_seconds"] for t in row["result"]["teams"])
        replay = row.get("replay")
        lines.append(f"- {row['id']}: {row['map']}, {row['goal']}, {row['profile']}/{row['level']}, "
                     f"seed {row['seed']}, {low} low-progress bot-seconds. "
                     + (f"Replay: `{replay}`." if replay else "Reproduce with the command in results.jsonl."))
    if failed:
        lines += ["", "## Errors / timeouts", ""]
        lines += [f"- {r['id']}: {r['status']} — {r.get('error', '')[:240]}" for r in records if r["status"] in {"error", "timeout"}]
    lines += ["", "No AI training or parameter changes are applied by this test.",
              "Compare the same map hashes, seeds, profiles, team sizes and match lengths after an AI change.", ""]
    temporary = folder / "report.tmp"
    temporary.write_text("\n".join(lines))
    temporary.replace(folder / "report.md")
    with (folder / "matches.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["id", "map", "goal", "profile", "level", "seed", "status", "winner", "time_up", "seconds", "low_progress_seconds", "team", "home_pct", "attack_pct", "repair_pct", "flag_pickups", "shots", "tank_kills", "deaths", "objective_score"])
        for r in records:
            result = r.get("result", {})
            for team, stats in enumerate(result.get("teams", [{}])):
                alive = max(stats.get("alive_ticks", 0), 1)
                pct = lambda field: round(100 * stats.get(field, 0) / alive, 2)
                writer.writerow([r["id"], r["map"], r["goal"], r["profile"], r["level"], r["seed"], r["status"],
                                 result.get("winner", ""), result.get("time_up", ""),
                                 result.get("ticks", 0) / result.get("tick_rate", 50), stats.get("low_progress_seconds", ""),
                                 team, pct("home_ticks"), pct("attack_ticks"), pct("repair_ticks"),
                                 stats.get("flag_pickups", ""), stats.get("shots", ""), stats.get("tank_kills", ""),
                                 stats.get("deaths", ""), stats.get("objective_score", "")])


def baseline_plan(folder):
    """Read data from a previous run; never execute its recorded command."""
    folder = Path(folder).resolve()
    manifest = json.loads((folder / "manifest.json").read_text())
    maps = {}
    for item in manifest["maps"]:
        path = (folder / item["snapshot"]).resolve()
        if folder not in path.parents or not path.is_file() or digest(path) != item["sha256"]:
            raise ValueError("missing or changed baseline map: " + item["snapshot"])
        name = item.get("name", Path(item["source"]).name)
        if name in maps:
            raise ValueError("ambiguous baseline map name: " + name)
        maps[name] = path
    plan = []
    for line in (folder / "results.jsonl").read_text().splitlines():
        row = json.loads(line)
        if row.get("status") != "ok":
            continue
        command = row["command"]
        if not isinstance(command, list) or not all(isinstance(value, str) for value in command):
            raise ValueError("invalid baseline command settings")
        # These two bounded numeric settings are the only command fields reused.
        def setting(flag):
            if command.count(flag) != 1:
                raise ValueError("missing or duplicate baseline setting: " + flag)
            return int(command[command.index(flag) + 1])
        side, minutes = setting("--side"), setting("--minutes")
        if (row["map"] not in maps or row["goal"] not in GOALS or
            row["profile"] not in {"server", "local"} or row["level"] not in {"easy", "normal", "hard"} or
            not isinstance(row["seed"], int) or not 0 <= row["seed"] <= 0xffffffff or
            not 1 <= side <= 4 or not 1 <= minutes <= 30):
            raise ValueError("invalid baseline scenario: " + str(row.get("id")))
        plan.append((row, maps[row["map"]], side, minutes))
    plan.sort(key=lambda item: item[0]["id"])
    if not plan:
        raise ValueError("baseline contains no completed matches")
    return folder, plan


def comparison(folder, records, baseline):
    before = {row["id"]: row for row, *_ in baseline}
    pairs = [(before[row["baseline_id"]], row) for row in records
             if row.get("status") == "ok" and row.get("baseline_id") in before]
    def metrics(rows):
        teams = [team for row in rows for team in row["result"]["teams"]]
        alive = sum(team["alive_ticks"] for team in teams)
        seconds = sum(team["alive_ticks"] / row["result"]["tick_rate"]
                      for row in rows for team in row["result"]["teams"])
        pct = lambda key: round(100 * sum(team.get(key, 0) for team in teams) / max(alive, 1), 3)
        return {"matches": len(rows), "natural_finishes": sum(not row["result"]["time_up"] for row in rows),
                "flag_pickups": sum(team["flag_pickups"] for team in teams),
                "objective_score": sum(team["objective_score"] for team in teams),
                "base_pct": pct("home_ticks"), "attack_pct": pct("attack_ticks"),
                "low_progress_per_1000_bot_seconds": round(1000 * sum(team["low_progress_seconds"] for team in teams) / max(seconds, 1), 3)}
    groups = {"all": pairs}
    for old, new in pairs:
        groups.setdefault(new["goal"], []).append((old, new))
    summary = {key: {"before": metrics([old for old, _ in rows]),
                     "after": metrics([new for _, new in rows])} for key, rows in sorted(groups.items())}
    lines = ["# Paired bot comparison", "", f"Matched completed scenarios: {len(pairs)}.", "",
             "Each pair uses the same map bytes, seed, mode, difficulty, bot preset, team-size limit and match limit.",
             "Only completed pairs are compared. Low-progress time is normalized by live bot time; it can include legitimate turning or combat.", "",
             "| Mode | Matches | Natural finishes before / after | Flags before / after | Objective points before / after | Base % before / after | Attack % before / after | Low-progress seconds per 1,000 bot-seconds before / after |",
             "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for key, values in summary.items():
        old, new = values["before"], values["after"]
        pair = lambda field: f"{old[field]} / {new[field]}"
        lines.append(f"| {key} | {new['matches']} | {pair('natural_finishes')} | {pair('flag_pickups')} | "
                     f"{pair('objective_score')} | {pair('base_pct')} | {pair('attack_pct')} | {pair('low_progress_per_1000_bot_seconds')} |")
    lines += ["", "Differences in self-play are evidence to inspect, not proof that a strategy is better against human players.", ""]
    (folder / "comparison.md").write_text("\n".join(lines))
    (folder / "comparison.json").write_text(json.dumps(summary, indent=2) + "\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, help="repeat completed scenarios from a prior results directory and compare")
    parser.add_argument("--engine", type=Path, required=True, help="freshly built ofp_match")
    parser.add_argument("--hours", type=float, default=8)
    parser.add_argument("--matches", type=int, default=0, help="stop after this many; 0 uses the time budget, or all completed baseline scenarios")
    parser.add_argument("--jobs", type=int, default=2, choices=range(1, 9))
    parser.add_argument("--minutes", type=int, default=20, choices=range(1, 31), help="simulated match limit")
    parser.add_argument("--timeout", type=float, default=180, help="wall seconds allowed per match")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--map", type=Path, action="append", dest="maps", help="restrict to selected maps; repeatable")
    parser.add_argument("--levels", nargs="+", choices=["easy", "normal", "hard"], default=["normal", "hard"])
    parser.add_argument("--profiles", nargs="+", choices=["server", "local"], default=["server", "local"])
    parser.add_argument("--side", type=int, choices=range(1, 5), default=4, help="maximum per side; map capacity still applies")
    parser.add_argument("--replays", type=int, default=12, help="save the first N matches; 0 disables recording")
    args = parser.parse_args(argv)
    if not 0 < args.hours <= 168 or not 0 < args.timeout <= 3600 or args.matches < 0 or args.replays < 0:
        parser.error("invalid time, match or replay limit")
    if not args.engine.is_file() or not os.access(args.engine, os.X_OK):
        parser.error("--engine must be an executable ofp_match")
    baseline = None
    if args.baseline:
        try:
            baseline_folder, baseline = baseline_plan(args.baseline)
        except (OSError, ValueError, KeyError, TypeError, IndexError) as error:
            parser.error("invalid baseline: " + str(error))
        if args.maps:
            parser.error("--map cannot be combined with --baseline")
    if baseline:
        # A baseline's numeric settings and snapshotted maps override scenario defaults.
        if args.matches:
            baseline = baseline[:args.matches]
        scenarios = [(path, row["goal"], row["profile"], row["level"]) for row, path, _, _ in baseline]
        args.matches = len(scenarios)
    else:
        maps = args.maps or sorted([*(ROOT / "data/maps").glob("*.ofpmap"), *(ROOT / "data/maps/classic").glob("*.ofpmap")])
        scenarios = [(path.resolve(), goal, profile, level) for path in maps for goal in map_goals(path)
                     for level in args.levels for profile in args.profiles]
    if not scenarios:
        parser.error("no supported maps/modes selected")
    default_base = Path.home() / "Development/builds/OpenFirePower/AI"
    folder = (args.out or default_base / time.strftime("%Y%m%d-%H%M%S")).resolve()
    folder.mkdir(parents=True, exist_ok=False)
    (folder / "engine").mkdir(); (folder / "maps").mkdir(); (folder / "replays").mkdir()
    engine = folder / "engine/ofp_match"
    shutil.copy2(args.engine, engine)
    copies = {}
    for path, *_ in scenarios:
        if path not in copies:
            target = folder / "maps" / (digest(path)[:12] + "-" + path.name)
            shutil.copy2(path, target)
            copies[path] = target
    map_names = {path: row["map"] for row, path, _, _ in baseline} if baseline else {}
    manifest = {"schema": 1, "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "engine_sha256": digest(engine),
                "arguments": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items() if k != "maps"},
                "maps": [{"source": str(p), "name": map_names.get(p, p.name), "snapshot": str(c.relative_to(folder)), "sha256": digest(c)} for p, c in copies.items()],
                "scenarios_per_seed": len(scenarios)}
    if baseline:
        manifest["baseline"] = str(baseline_folder)
        manifest["scenario_settings"] = "from matched baseline records"
    try:
        manifest["commit"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        manifest["working_tree_changed"] = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT))
    except (OSError, subprocess.CalledProcessError):
        pass
    (folder / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    deadline = time.monotonic() + args.hours * 3600
    stop = threading.Event()
    active = set(); lock = threading.Lock(); records = []
    previous = {}
    for sig in (signal.SIGINT, signal.SIGTERM):
        previous[sig] = signal.signal(sig, lambda *_: stop.set())

    def play(index):
        path, goal, profile, level = scenarios[index % len(scenarios)]
        seed = 7919 * (index // len(scenarios) + 1)
        side, minutes = args.side, args.minutes
        if baseline:
            old, _, side, minutes = baseline[index]
            seed = old["seed"]
        name = f"match-{index + 1:06d}"
        cmd = [str(engine), "evaluate", "--map", str(copies[path]), "--goal", goal,
               "--profile", profile, "--level", level, "--seed", str(seed), "--side", str(side), "--minutes", str(minutes)]
        row = {"id": name, "map": path.name, "goal": goal, "profile": profile, "level": level, "seed": seed, "command": cmd}
        if baseline:
            row["baseline_id"] = old["id"]
            row["map"] = old["map"]
        row["map_sha256"] = digest(copies[path])
        if index < args.replays:
            row["replay"] = f"replays/{name}.ofprep"
            cmd.extend(["--out", str(folder / row["replay"])])
        process = None
        started = time.monotonic()
        try:
            if stop.is_set() or started >= deadline:
                return None
            process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            with lock:
                active.add(process)
            output, error = process.communicate(timeout=min(args.timeout, max(.01, deadline - started)))
            if process.returncode:
                status = "budget" if time.monotonic() >= deadline else "cancelled" if stop.is_set() else "error"
                row.update(status=status, error=error.strip() or f"exit {process.returncode}")
            else:
                result = json.loads(output)
                if result.get("schema") != 1 or len(result["teams"]) != 2:
                    raise ValueError("invalid evaluation output")
                row.update(status="ok", result=result)
        except subprocess.TimeoutExpired:
            row.update(status="budget" if time.monotonic() >= deadline else "timeout",
                       error="match exceeded its wall-clock or tournament time budget")
        except (OSError, ValueError, KeyError, TypeError) as error:
            row.update(status="error", error=str(error))
        finally:
            if process is not None:
                if process.poll() is None:
                    process.kill(); process.communicate()
                with lock:
                    active.discard(process)
        row["wall_seconds"] = round(time.monotonic() - started, 3)
        if row.get("status") != "ok" and row.get("replay"):
            (folder / row["replay"]).unlink(missing_ok=True)
            row.pop("replay")
        return row

    scenario_label = "paired scenarios" if baseline else "scenarios per seed"
    print(f"Bot tournament: {len(scenarios)} {scenario_label}, at most {args.hours:g} hours.\nResults: {folder}", flush=True)
    try:
        with (folder / "results.jsonl").open("a", buffering=1) as stream, concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            pending = set(); index = 0; next_report = time.monotonic()
            while pending or (not stop.is_set() and time.monotonic() < deadline and (not args.matches or index < args.matches)):
                if stop.is_set() or time.monotonic() >= deadline:
                    stop.set()
                    with lock:
                        for process in active:
                            process.terminate()
                while not stop.is_set() and time.monotonic() < deadline and len(pending) < args.jobs and (not args.matches or index < args.matches):
                    pending.add(pool.submit(play, index)); index += 1
                if not pending:
                    break
                done, pending = concurrent.futures.wait(pending, timeout=.5, return_when=concurrent.futures.FIRST_COMPLETED)
                for future in done:
                    row = future.result()
                    if row:
                        records.append(row); stream.write(json.dumps(row) + "\n"); stream.flush()
                        if time.monotonic() >= next_report:
                            report(folder, records); next_report = time.monotonic() + 30
                        print(f"{row['id']} {row['status']}: {row['map']} / {row['goal']} / {row['profile']} / {row['level']}", flush=True)
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)
        report(folder, records)
        if baseline:
            comparison(folder, records, baseline)
    print(f"Saved {folder / 'report.md'}", flush=True)
    return int(any(r["status"] in {"error", "timeout"} for r in records))


if __name__ == "__main__":
    raise SystemExit(main())
