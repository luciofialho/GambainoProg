"""Upper bound of the /getstatus sections and of each status line.

Every format literal of a section function is counted once (all if/else
branches together) and each conversion gets a generous width, so the result
is a pessimistic bound. Checked limits:
- temperature section: strnncat(..., 2048) in getTemperatureControlStatus()
- pressure section: PRESSURE_STATUS_SIZE (include/PressureControl.h)
- each pressure status line: tmp[] in PressureControl.cpp
- whole page: MAXSTATUSLEN (IOTK_ESPAsyncServer.h, 8192), with GLog <= 512 and peers <= 1000
Run: python tests/check_status_sizes.py
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
read = lambda p: (root / p).read_text(encoding="utf-8").replace("\r\n", "\n")
povoto, temp, press = read("src/Povoto.cpp"), read("src/TermperatureControl.cpp"), read("src/PressureControl.cpp")
press_h = read("include/PressureControl.h")

PRESSURE_LIMIT = int(re.search(r"PRESSURE_STATUS_SIZE\s*=\s*(\d+)", press_h).group(1)) - 1
TMP_LIMIT = int(re.search(r"^char tmp\[(\d+)\];", press, re.M).group(1)) - 1
TEMP_LIMIT = 2047
PAGE_LIMIT = 8192
GLOG_MAX, PEERS_MAX = 512, 1000  # GLogGetEspNowStatus buf[512]; peers: header + 12 rows


def body(src, signature):
    j = src.index("{", src.index(signature))
    depth = 0
    for k in range(j, len(src)):
        depth += {"{": 1, "}": -1}.get(src[k], 0)
        if depth == 0:
            return src[j:k + 1]
    raise ValueError(signature)


def calls(b):
    """snprintf/sprintf calls and strnncat(st, "literal") calls, with balanced parentheses."""
    for m in re.finditer(r"\b(snprintf|sprintf|strnncat)\s*\(", b):
        k, depth, in_str = m.end(), 1, False
        while depth:
            c = b[k]
            if in_str:
                if c == "\\":
                    k += 1
                elif c == '"':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c in "()":
                depth += 1 if c == "(" else -1
            k += 1
        text = b[m.start():k]
        if m.group(1) == "strnncat" and not re.match(r'strnncat\s*\(\s*st\s*,\s*"', text):
            continue  # copies a buffer already counted
        yield text


def width(text):
    total = 0
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', text):
        lit = lit.replace('\\"', '"')

        def conv(m):
            spec = m.group(0)
            if spec == "%%":
                return "%"
            return "x" * (32 if spec.endswith("s") else 12 if spec.endswith("f") else 11)

        total += len(re.sub(r"%%|%[-+ #0]*\d*(?:\.\*|\.\d+)?(?:hh|h|ll|l|z)?[diouxXfFeEgGsc]", conv, lit))
    return total


def section(src, signature):
    return [(width(t), t) for t in calls(body(src, signature))]


temperature = section(temp, "char *getTemperatureControlStatus(char *st)")
pressure = section(press, "char *getPressureControlStatus(char *st)")
stability = section(press, "static void appendStabilityStatus(")
own = section(povoto, "char * getPovotoStatus(char *st)") + section(povoto, "void handlePovotoStatus(")

t = sum(w for w, _ in temperature)
# appendStabilityStatus: one of its three branches, called twice.
p = sum(w for w, _ in pressure) + 2 * max(w for w, _ in stability)
o = sum(w for w, _ in own)
widest = max(pressure + stability, key=lambda x: x[0])
page = o + t + p + GLOG_MAX + PEERS_MAX

checks = [
    ("temperature section", t, TEMP_LIMIT),
    ("pressure section", p, PRESSURE_LIMIT),
    ("widest pressure line", widest[0], TMP_LIMIT),
    ("whole page", page, PAGE_LIMIT),
]
ok = True
for name, value, limit in checks:
    fail = value > limit
    ok = ok and not fail
    print(f"{name:21}: <= {value:5} (limit {limit})" + ("  <-- FAIL" if fail else ""))
print("widest line starts with:", re.sub(r"\s+", " ", widest[1])[:80])
print("RESULT:", "PASS" if ok else "FAIL")
raise SystemExit(0 if ok else 1)
