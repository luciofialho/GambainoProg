"""Worst-case sizes of the GLog rows (Cold and Relief): header and data.

Field formats follow IOTK_GLog.cpp: strings are quoted, floats use "%.<p>f"
(non-finite -> ""), integers are plain. The receiver (GambainoCommon) rejects
packets of 2048 bytes or more, so a row, including "]}", must stay <= 2047.
Also checks that each header has as many columns as its data row.
Run: python tests/check_log_sizes.py
"""
import re
from pathlib import Path

LIMIT = 2047
root = Path(__file__).resolve().parents[1]
src = (root / "src/datalog.cpp").read_text(encoding="utf-8").replace("\r\n", "\n")

# Longest possible text of the string-valued arguments (checked in the sources).
STRING_MAX = {
    "getTemperatureModeLabel()": 14,              # "CHILL (paused)"
    "taskWindowTypeToText(taskWindowType)": 15,   # "Dynamic Hopping"
    "getTempStateLabel()": 15,                    # "CHANGING_DIRECT"
    "tempStableSinceText": 19,                    # ISO local time
    "getPressStateLabel()": 15,
    "pressStableSinceText": 19,
    "co2.mode": 9,                                # "half-life" / "immediate"
    "co2.criteriaState": 12,
    "co2.withReliefsState": 12,
    "co2.withoutReliefsState": 12,
    "dailyHs.state": 5,                           # "valid" / "hold" / "ema"
    "getCO2EvolutionSource()": 10,                # "calculated"
    'data.gasFlowModelActive ? "yes" : "no"': 3,
    "data.gasHeadspaceUpdateStatus": 32,          # longest status is 31
    'data.dailyState ? data.dailyState : ""': 5,
}
UNSIGNED_HINTS = ("Millis", "millis()", "dump.startMillis", "dump.endMillis", "reliefNumber",
                  "totalReliefCount", "previousReliefNumber", "valveOpened",
                  "polytropicSourceReliefNumber")
INT_WIDTH = 11          # int / long with sign
ULONG_WIDTH = 10        # unsigned long
FLOAT_INT_WIDTH = 6     # sign + 5 integer digits, generous for every logged float
TIMESTAMP_WIDTH = 21    # "YYYY-MM-DDTHH:MM:SS" with quotes


def field_width(arg):
    arg = arg.strip()
    m = re.fullmatch(r'"([^"]*)"', arg)
    if m:
        return len(m.group(1)) + 2
    m = re.fullmatch(r"(.+),\s*(\d+)", arg)
    if m:  # float with precision
        return FLOAT_INT_WIDTH + 1 + int(m.group(2))
    if arg in STRING_MAX:
        return STRING_MAX[arg] + 2
    if any(h in arg for h in UNSIGNED_HINTS):
        return ULONG_WIDTH
    return INT_WIDTH


def row_size(sheet, args):
    prefix = '{"folderName":"GambainoDebug","spreadsheetName":"65535","sheetName":"%s","values":[' % sheet
    fields = [TIMESTAMP_WIDTH] + [field_width(a) for a in args]
    return len(prefix) + sum(fields) + (len(fields) - 1) + len("]}")


def args_in(block):
    return re.findall(r"GLogAddData\((.*?)\);", block)


def check(sheet, header_block, data_block):
    header = args_in(header_block)
    # Every "" in these data rows is the alternative branch of a column already
    # counted (the empty dump-column loop, the else of ResidualFromReliefNumber),
    # so the widest branch is kept and "" is dropped.
    data = [a for a in args_in(data_block) if a != '""']
    data_columns = len(data)
    header_size = row_size(sheet, header)
    data_size = row_size(sheet, data)
    ok = header_size <= LIMIT and data_size <= LIMIT and len(header) == data_columns
    print(f"{sheet:6}: {len(header)} header columns, {data_columns} data columns; "
          f"header {header_size} bytes, worst data row {data_size} bytes (limit {LIMIT})"
          + ("" if ok else "  <-- FAIL"))
    return ok


cold = src.index("void doDataLog()")
cold_header = src[src.index("if (!headerWritten) {", cold):src.index("headerWritten = true;", cold)]
cold_data = src[src.index("else {", src.index("headerWritten = true;", cold)):src.index("void doReliefDataLog(")]

relief = src.index("void doReliefDataLog(")
relief_header = src[src.index("if (!headerWritten) {", relief):src.index("headerWritten = true;", relief)]
relief_data = src[src.index("// Send the data row separately", relief):src.index("void doRecoveryDataLog(")]

ok = check("Cold", cold_header, cold_data)
ok = check("Relief", relief_header, relief_data) and ok
print("RESULT:", "PASS" if ok else "FAIL")
raise SystemExit(0 if ok else 1)
