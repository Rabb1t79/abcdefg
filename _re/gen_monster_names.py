import json
import re

DUMP = r"C:\Dumper-7\5.6.1-0+UE5-HT"
OUT = r"I:\Anomalymain\plugins\AnomalyKXExp2\monster_names.inc"

info = {}
for entry in json.load(open(DUMP + r"\Dumpspace\ClassesInfo.json", encoding="utf-8"))["data"]:
    for name, value in entry.items():
        inherit = []
        for item in value:
            if "__InheritInfo" in item:
                inherit = item["__InheritInfo"]
        info[name] = inherit

# Classes the plugin's ASC check would accept: they derive from AHTAbilityCharacter
# (the class that owns HTAbilitySystemComponent at +0x940).
asc_bearing = [name[1:] for name, inherit in info.items()
               if any(base == "AHTAbilityCharacter" for base in inherit)]

names = set(asc_bearing)
pattern = re.compile(r"BlueprintGeneratedClass [\w\.]+\.(\w+_C)\s*$")
with open(DUMP + r"\GObjects-Dump.txt", encoding="utf-8", errors="ignore") as handle:
    for line in handle:
        match = pattern.search(line)
        if match and re.search(r"mon_|boss_|Boss_|Abyss|Monster|Mon_", match.group(1)):
            names.add(match.group(1))

names.update("""Abyss_NPC_Sub_fm_Simple_sp_Interview_C
Abyss_NPC_Sub_mm_Simple_tanshou_loop_C
Boss_06_BP_Abyss_C
Boss_33_act_Laser1_C
Boss_33_act_Laser2_C
boss_33_ChallengeLv4_BP_C
boss_33_ChallengeLv5_BP_C
boss_33_ChallengeLv6_BP_C
boss_33_ControllerBP_lv3_C
boss_33_ControllerBP_lv5_C
boss_33_music_BP_C
BP_Monster_Leaves_Combat_mon_18_C
BP_Only_Monster_Leaves_Combat_mon_18_C
mon_018ControllerBP_LeaveBattleDistanceMax_C
mon_14_BP_Abyss_C
mon_18_BP_World_Passive_New_Black_C
mon_39_BP_World_Street_C
mon_39_ModsControllerBP_World_Street_C
mon_41_ControllerBP_summon_C
mon_41_Summon01_BP_C
mon_41_Summon02_BP_C""".split())

names = sorted(name for name in names if re.fullmatch(r"[A-Za-z0-9_]+", name))
lines = [
    "// Classes the plugin ASC check accepts (AHTAbilityCharacter descendants) plus every",
    "// mon_/boss_/Abyss class name from the dump and the runtime names seen in the log.",
    "// Resolved to FName ids once on the render thread; the scan then compares integers.",
]
for name in names:
    lines.append('    "%s",' % name)
open(OUT, "w", encoding="ascii", newline="\n").write("\n".join(lines) + "\n")
print("names=%d asc=%d" % (len(names), len(asc_bearing)))
