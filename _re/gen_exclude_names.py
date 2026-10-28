import json
import re

DUMP = r"C:\Dumper-7\5.6.1-0+UE5-HT"
OUT = r"I:\Anomalymain\plugins\AnomalyKXExp2\exclude_names.inc"

NPC_BASES = {"AHTNPCCharacter", "AHTNPCCharacter_Sub"}
VEHICLE_BASES = {"AHTWheeledVehicleDrivable", "AHTWheeledVehicleBase", "AWheeledVehiclePawn",
                 "AHTVehicleBase", "AVehicle"}

info = {}
for entry in json.load(open(DUMP + r"\Dumpspace\ClassesInfo.json", encoding="utf-8"))["data"]:
    for name, value in entry.items():
        inherit = []
        for item in value:
            if "__InheritInfo" in item:
                inherit = item["__InheritInfo"]
        info[name] = inherit

excluded = set()
for name, inherit in info.items():
    bases = set(inherit)
    if bases & NPC_BASES or bases & VEHICLE_BASES:
        excluded.add(name[1:])

pattern = re.compile(r"BlueprintGeneratedClass [\w\.]+\.(\w+_C)\s*$")
with open(DUMP + r"\GObjects-Dump.txt", encoding="utf-8", errors="ignore") as handle:
    for line in handle:
        match = pattern.search(line)
        if not match:
            continue
        name = match.group(1)
        if re.search(r"(?i)(^|_)(seat|spoiler)(_|$)|^BP_SO_|^Vehicle\d", name):
            excluded.add(name)

excluded = sorted(name for name in excluded if re.fullmatch(r"[A-Za-z0-9_]+", name))
lines = [
    "// Classes treated as irrelevant targets: NPCs (AHTNPCCharacter descendants) and",
    "// vehicles (wheeled-vehicle descendants) plus the seat/spoiler prop classes.",
    "// Their FName ids are resolved once and skipped by the arm path and the scan.",
]
for name in excluded:
    lines.append('    "%s",' % name)
open(OUT, "w", encoding="ascii", newline="\n").write("\n".join(lines) + "\n")
print("excluded=%d" % len(excluded))
