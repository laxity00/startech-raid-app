"""Validate serialized mocked hardware samples; never accesses a real device."""
import json
import subprocess
import sys

result = subprocess.run(sys.argv[1:], capture_output=True, text=True, check=True)
samples = [json.loads(line) for line in result.stdout.splitlines()]
assert len(samples) == 32
for sample in samples:
    assert sample["schema"] == 1 and sample["tool_version"] == "0.3.3"
    assert not sample["array_synchronization_verified"]
    assert len(sample["ssds"]) == 2 and len(sample["transfers"]) <= 9
    if not sample["query_ok"]:
        assert sample["raid_status"] == "Unknown" and sample["raid_raw"] is None
    for transfer in sample["transfers"]:
        assert transfer["direction"] == "from_device"
        assert transfer["timeout_ms"] == 3000
        assert len(bytes.fromhex(transfer["response_hex"])) == transfer["bytes"]
good = samples[0]
assert good["raid_display"] == "1 - Healthy" and good["smart_complete"]
for port, disk in enumerate(good["ssds"]):
    assert disk["port"] == port and disk["serial"] == f"TEST-SERIAL-{port}"
    assert disk["model"] == "Synthetic SSD" and disk["capacity_bytes"] == 35651584
    assert disk["attribute_check"] == "Passed" and disk["attributes_compared"] == 1
    assert disk["temperature_candidate_c"] == 35
    assert disk["smart_overall_health"] == "Unknown" and not disk["ata_return_status_queried"]
assert not samples[3]["ssds"][0]["identity_verified"] and samples[3]["ssds"][1]["smart_data_valid"]
assert not samples[4]["ssds"][0]["smart_data_valid"]
assert samples[6]["duplicate_ssd_identity"]
assert all(not d["identity_verified"] and d["attribute_check"] == "Unknown"
           and d["temperature_candidate_c"] is None for d in samples[6]["ssds"])
for index, label in [(8, "255 - Unknown"), (9, "2 - Degraded"), (21, "3 - Failed"), (22, "4 - Rebuilding"), (23, "0 - Unknown")]:
    assert samples[index]["raid_display"] == label
assert samples[13]["ssds"][0]["attribute_check"] == "Failed"
assert samples[13]["ssds"][0]["failed_prefail_attributes"] == 1
assert not samples[30]["query_ok"]
assert not samples[31]["smart_requested"] and len(samples[31]["transfers"]) == 3
print(json.dumps({"mocked_samples": len(samples), "read_only_transport_and_failure_cases": True, "hardware_access": False}))

# An EPERM open denial must be actionable and must not emit any request.
assert 'errno=1' in samples[12]['error']
assert 'Protection mode' in samples[12]['error']
assert samples[12]['transfers'] == []
