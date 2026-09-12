"""Verify that every persisted field has direct read and write calls."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
header = (root / "include/PovotoData.h").read_text(encoding="utf-8")
source = (root / "src/PovotoData.cpp").read_text(encoding="utf-8")
for group in ["FMTData", "UserConfigurationData", "BatchData",
              "SetPointData", "CountersData"]:
    body = re.search(r"struct " + group + r"_t \{(.*?)\}", header, re.S)[1]
    members = set(re.findall(r"\w+\s+(\w+)(?:\[\d+\])?;", body))
    read = re.search(r"bool read" + group + r"FromEEPROM\(\) \{(.*?)\n\}", source, re.S)[1]
    write = re.search(r"bool write" + group + r"ToNIV\(\) \{(.*?)\n\}", source, re.S)[1]
    for member in members:
        assert group + "." + member in read, (group, member, "missing read")
        assert group + "." + member in write, (group, member, "missing write")
    get_keys = set(re.findall(r'store\.get\w+\("([^"]+)"', read))
    put_keys = re.findall(r'store\.put\w+\("([^"]+)"', write)
    assert get_keys == set(put_keys), (group, get_keys, put_keys)
    assert len(put_keys) == len(set(put_keys))
    assert all(len(key) <= 15 for key in put_keys)
assert 'store.putDouble("molsEjected"' in source
assert 'store.putDouble("co2Solution"' in source
assert "store.clear()" in source
assert "StoredField" not in source
assert "cleanupObsolete" not in source
print("Direct storage checks passed: read/write coverage, keys, doubles and schema storage.")
