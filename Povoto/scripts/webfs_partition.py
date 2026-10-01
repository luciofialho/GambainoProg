"""Point PlatformIO's filesystem image at the web partition.

The ESP32 builder otherwise chooses the last filesystem partition (persist),
whereas ElegantOTA/Update targets the first spiffs partition (spiffs/web).
Run as a post script so this overrides the builder's discovered FS range.
"""

Import("env")

WEB_START = 0x670000
WEB_SIZE = 0x110000
PERSIST_START = 0x780000
PERSIST_SIZE = 0x70000

with open(env.subst("$PROJECT_DIR/partitions.csv"), encoding="utf-8") as table:
    entries = [
        [field.strip() for field in line.split(",")]
        for line in table
        if line.strip() and not line.lstrip().startswith("#")
    ]

filesystem = [entry for entry in entries if entry[1:3] == ["data", "spiffs"]]
expected = [
    ["spiffs", "data", "spiffs", hex(WEB_START), hex(WEB_SIZE)],
    ["persist", "data", "spiffs", hex(PERSIST_START), hex(PERSIST_SIZE)],
]
if [entry[:5] for entry in filesystem] != expected:
    raise RuntimeError("Unexpected LittleFS partition map; refusing filesystem upload")

env.Replace(FS_START=WEB_START, FS_SIZE=WEB_SIZE)
