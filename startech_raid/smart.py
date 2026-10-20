"""Conservative SMI2259XT/Y0507A0 candidate labels, not an Integral specification.

Paraphrased interoperability facts: Transcend MTS960T v1.4 tables 18/19,
https://docs.rs-online.com/3aeb/A700000012186708.pdf
Cross-check: https://github.com/smartmontools/smartmontools/blob/master/smartmontools/drivedb.h
No upstream implementation code or database is bundled.
"""
LABELS = {
    1: "Read error indicator", 5: "Reallocated sectors", 9: "Power-on hours",
    12: "Power cycles", 160: "Uncorrectable read/write errors",
    161: "Usable spare blocks", 163: "Initial bad blocks", 164: "Total block erasures",
    165: "Maximum erase count", 166: "Minimum erase count", 167: "Average erase count",
    168: "Rated erase-cycle ceiling", 169: "Estimated remaining life",
    175: "Worst-chip program failures", 176: "Worst-chip erase failures",
    177: "Wear-level activity", 178: "New bad blocks", 181: "Total program failures",
    182: "Total erase failures", 192: "Unexpected power events", 194: "Temperature",
    195: "ECC corrections", 196: "Reallocation events", 197: "Pending sectors",
    198: "Uncorrectable errors", 199: "Interface CRC errors", 232: "Reserve space indicator",
    241: "Host writes counter", 242: "Host reads counter", 245: "Flash/TLC writes counter",
}
MEDIA_IDS = frozenset((5,160,175,176,178,181,182,196,197,198))
IMPORTANT_IDS = MEDIA_IDS | {9,161,169,199}
SOURCE = "SMI2259XT family documentation; Integral meaning inferred"

def profile_matches(disk):
    return bool(disk and disk.get("identity_verified") and disk.get("smart_data_valid")
                and disk.get("smart_controller_signature") == "SMI2259XT"
                and disk.get("firmware") == "Y0507A0")

def metadata(disk, attribute_id):
    matched = profile_matches(disk)
    label = LABELS.get(attribute_id) if matched else None
    return {"label": label or "Vendor-specific attribute", "interpretation_verified": False,
            "profile_matched": matched, "source": SOURCE if label else "No matching interpretation profile",
            "candidate_unit": "%" if matched and attribute_id == 169 else
                "32 MiB" if matched and attribute_id in (241,242,245) else "raw counter",
            "warning_recommended": matched and attribute_id in IMPORTANT_IDS}

def indicators(disk):
    # Missing/invalid input is Unknown, never a reassuring zero.
    if not profile_matches(disk):
        return {"media_error_warning": None, "interface_error_warning": None, "life_warning": None}
    attrs = {a["id"]: a.get("raw") for a in disk.get("attributes", [])}
    def valid(i): return type(attrs.get(i)) is int and attrs[i] >= 0
    media = True if any(valid(i) and attrs[i] > 0 for i in MEDIA_IDS) else (
        False if all(valid(i) for i in MEDIA_IDS) else None)
    life = attrs.get(169)
    return {"media_error_warning": media,
            "interface_error_warning": attrs[199] > 0 if valid(199) else None,
            "life_warning": life <= 20 if valid(169) and life <= 100 else None}
