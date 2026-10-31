"""Validate the two-native-client faction probe; no cargo/radar or dedicated proof."""
import argparse
import json
from pathlib import Path
import re


REPORT = re.compile(
    r"PFSESSION slot=(p[12]) t=([0-9.]+) other=(p[12]) faction=(\S+) "
    r"name=(.*?) short=(.*?) diplomacy_list=(.*?) uirelation=(\S+) ships=(.*)$")
FAIL = re.compile(r"FEATURE player_factions DISABLED|PLAYER_FACTIONS STOPPED|"
                  r"PLAYER_FACTIONS REFUSED|BUBBLE STOPPED|PFSESSION ERROR|FATAL ERROR|"
                  r"Error in MD cue md\.X4LAN")


def resolved(value):
    return (value.strip().lower() not in {"", "false", "nil", "none", "null", "?", "??", "unknown faction"}
            and not value.startswith("LAN Player"))


def analyze_client(text, slot):
    """Require correlated client/identity/ownership and an ordered relation round trip."""
    other = "p2" if slot == "p1" else "p1"
    faction = "x4lan_player" + other[1:]
    errors = []
    lines = text.splitlines()
    failures = [line for line in lines if FAIL.search(line)]
    if failures:
        errors.append("native feature failure/refusal")
    if not any(f"PLAYER_FACTIONS READY slot={slot} " in line for line in lines):
        errors.append("missing matching ready slot")
    if not any(f"PLAYER_FACTIONS IDENTITY_APPLIED slot={other} faction={faction} " in line for line in lines):
        errors.append("missing remote identity application")
    if not any("relay=connected remote=receiving" in line for line in lines):
        errors.append("missing native relay/remote reception")

    relations = []
    for line in lines:
        match = re.search(r"PLAYER_FACTIONS RELATION_APPLIED 1\|2\|([-+0-9.eE]+)\s*$", line)
        if match:
            value = float(match[1])
            if not relations or value != relations[-1]:
                relations.append(value)
    if relations != [0.75, 0.0, -0.5, 0.75]:
        errors.append("relation sequence must be allied -> neutral -> hostile -> allied")
    fa, fb = ("player", faction) if slot == "p1" else (faction, "player")
    directional_pattern = re.compile(
        r"PLAYER_FACTIONS RELATION_APPLIED " + re.escape(fa + "->" + fb) + r"=([-+0-9.eE]+) "
        + re.escape(fb + "->" + fa) + r"=([-+0-9.eE]+)\s*$")
    directions = []
    for line in lines:
        match = directional_pattern.search(line)
        if match:
            pair = (float(match[1]), float(match[2]))
            if not directions or pair != directions[-1]:
                directions.append(pair)
    if directions != [(v, v) for v in (0.75, 0.0, -0.5, 0.75)]:
        errors.append("both native directions must complete the relation round trip")

    expected_actions = (("hostile", "applied"), ("allied", "applied")) if slot == "p1" else (("neutral", "requested"),)
    for value, result in expected_actions:
        marker = f"PFSESSION slot={slot} ACTION setRelation({other},{value}) -> true {result}"
        if not any(marker in line for line in lines):
            errors.append(f"missing successful {value} action")

    samples = []
    for line in lines:
        match = REPORT.search(line)
        if not match:
            continue
        found_slot, elapsed, found_other, found_faction, name, short, listed, relation, ships = match.groups()
        if found_slot != slot or found_other != other or found_faction != faction:
            errors.append("report client/faction mapping mismatch")
            continue
        if not (resolved(name) and resolved(short) and listed == "yes:" + name):
            continue  # Startup observations can precede identity assignment.
        try:
            ui_relation = float(relation)
        except ValueError:
            continue
        if not (-30 <= ui_relation <= 30):
            continue
        owners = {}
        for ship in ships.split(";"):
            item = re.fullmatch(r"([12]):([^/]+)/(.+)/pilot=(p[12]|0)", ship)
            if item:
                owners[item[1]] = (item[2], item[3], item[4])
        # Both actively piloted ships must be present in the SAME native sample.
        if set(owners) != {"1", "2"}:
            continue
        correct = True
        for index, (owner, owner_name, pilot) in owners.items():
            want = "player" if "p" + index == slot else "x4lan_player" + index
            if pilot != "p" + index or owner != want or not resolved(owner_name):
                correct = False
        if correct:
            samples.append((float(elapsed), ui_relation))
    if not samples:
        errors.append("missing correlated named diplomacy and both piloted ship owners")
    if not any(t >= 160 and relation >= 20 for t, relation in samples):
        errors.append("missing final allied native observation at/after 160 seconds")
    if not any(re.search(r"PFSESSION DONE slot=" + slot + r"\s*$", line) for line in lines):
        errors.append("missing matching completion")
    return {"slot": slot, "success": not errors, "errors": errors,
            "relation_sequence": relations, "directional_sequence": directions,
            "valid_samples": len(samples)}


def analyze(folder):
    folder = Path(folder)
    result = json.loads((folder / "result.json").read_text(encoding="utf-8"))
    clients = []
    errors = []
    if result.get("verdict") != "pass":
        errors.append("lane session did not pass")
    rows = result.get("clients", [])
    if len(rows) != 2 or {r.get("slot") for r in rows} != {"p1", "p2"}:
        errors.append("lane result must contain distinct p1 and p2 clients")
    for row in rows:
        if row.get("verdict") != "pass" or not row.get("x4", {}).get("pid"):
            errors.append("each client must be a successful launched native process")
        if row.get("lane_restored") is not True:
            errors.append("client lane was not restored")
    for index in (1, 2):
        slot = f"p{index}"
        path = folder / f"client{index}" / "x4lan-debug.txt"
        if not path.is_file():
            errors.append(f"missing {slot} native log")
            continue
        clients.append(analyze_client(path.read_text(encoding="utf-8", errors="replace"), slot))
    return {"success": not errors and len(clients) == 2 and all(c["success"] for c in clients),
            "errors": errors, "clients": clients,
            "scope": "two native co-op clients: identities, diplomacy, relation round trip, piloted owners; not dedicated/cargo/radar acceptance"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.folder)
    except (OSError, ValueError, TypeError) as exc:
        result = {"success": False, "errors": [str(exc)]}
    print(json.dumps(result, indent=2))
    return 0 if result["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
