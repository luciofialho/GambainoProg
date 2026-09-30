# Historical graph data (model and persistence)

This phase implements storage and CSV validation, not chart rendering or
firmware upload.

- A sample is taken at most once per 15-minute local-NTP-epoch slot only while
  the mode is Fermenting. No synthetic points are inserted after
  a reboot, clock outage, or power outage.
- Each point holds local epoch, SG, temperature, temperature setpoint,
  pressure, pressure setpoint, gCO2/L/d, ABV, and rate-quality flags. Invalid
  values are `NAN`; the graph API should serialize them as JSON `null`.
  The rate follows Brewfather's `bpm` inclusion criterion exactly: positive,
  finite and outside a pressure/temperature transition. The flags distinguish
  held and transition values.
- PSRAM contains a chronological circular buffer of 5,952 points (62 days at
  96 points/day). A getter copies one point under a mutex. Expired points are
  discarded by epoch on sampling or reading.
- LittleFS stores fixed 48-byte records in `/graph-history.bin`. Each record
  includes a format magic, batch-number key, the point, and a checksum. One
  slot is overwritten for each new point once the file reaches 5,952 slots:
  maximum file size is 285,696 bytes. A partly written or corrupt record is
  skipped during restore; other records remain available. The file is scanned
  and sorted into PSRAM when booting.
- Starting a new batch or restoring factory defaults clears the graph file and
  PSRAM buffer. A batch-date/name correction does not clear history. The
  existing LittleFS mount no longer automatically formats on failure, to
  protect persisted history and UI assets.
- The boot log reports restored point count, reserved PSRAM, and LittleFS
  used/total bytes. The board's actual free LittleFS space should be checked
  before relying on the full 62-day capacity.
- `/getstatus` reports LittleFS used/total/free, PSRAM used/total/free/largest
  free block, and the single series' point count.
- `/graphs` has one CSV download at `/graphs/data.csv`. The Debug Params page
  alone can replace the current series with 1,345 synthetic points spanning
  exactly from 14 days ago through now, from smooth piecewise SG decline,
  temperature cycles and target steps, a spunding pressure rise, and a CO2-rate
  peak, with gaps in the rate during simulated setpoint transitions. The
  replacement is persisted in LittleFS and copied into the same
  PSRAM buffer. It overwrites the prior graph history after explicit browser
  confirmation; a temporary file and backup protect against an incomplete
  replacement. Subsequent Fermenting observations append to this same series.
  CSV uses semicolons between columns and commas for decimal values. It leaves
  invalid values blank and includes local epoch, local ISO date/time,
  and quality flags (including synthetic-origin points). These flags are
  metadata, not post-processing: 1 = rate held after reboot, 2 = transition
  (rate blank), 4 = synthetic. Each row is a snapshot, not a 15-minute average.
  CSV export copies the series once to a temporary PSRAM snapshot and streams
  full HTTP chunks from it; it does not lock the live history or call NTP for
  every exported row.
- The home page uses compact vector logos converted from the supplied Povoto
  and Brewtal SVGs and served from firmware; no LittleFS asset upload is needed.
- `platformio.ini` does not override the board partition scheme. Its
  `default_8MB.csv` assigns `0x180000` bytes (1,572,864 B = 1.5 MiB) to the
  `spiffs`-typed filesystem partition used by LittleFS. The old checked-in
  `partitions.bin` instead encodes `0x150000` (1,376,256 B); it is not the
  current board definition. The mounted size in `/getstatus` is definitive
  for the running device.

Future chart work will use uPlot, convert the local epoch consistently for the
browser, and serve bounded/chunked data rather than placing all points in the
initial HTML page.
