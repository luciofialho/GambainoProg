# Historical graph data (model and persistence)

This phase implements storage, CSV validation, and the first browser chart.
Firmware and LittleFS uploads remain separate operations.

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
- `/getstatus` reports web and data LittleFS used/total/free separately (or
  marks them shared on the legacy layout), PSRAM usage, and point count.
- `/graphs` serves `data/www/graphs.html` from LittleFS. `data/www/graphs.js`
  reads the existing `/graphs/data.csv`, removes duplicate timestamps left by
  old exports, and draws Evolution (SG, temperature, pressure, gCO2/L/d, four
  compact independent axes), Temperature (measured and target temperature,
  one °C axis), Pressure (measured and target pressure, one bar axis), or
  Attenuation (SG and horizontal dotted OG on one axis, ABV on the other) using
  a locally bundled uPlot 1.6.32. OG comes from `BatchData.batchOG` through
  the small `/graphs/meta.json` endpoint rather than being inferred from SG;
  if metadata is unavailable, the SG/ABV lines remain visible and OG is marked
  unavailable. The synthetic demo uses its own fixed initial SG of 1.054, so
  its starting SG may differ slightly from the batch's configured OG line.
  Units appear in the legend, not in the
  compact axis labels; the date legend caption, chart subtitles, and successful
  observation count are omitted. Export CSV sits at the bottom of the graph
  selector column. Null values and missing
  times are bridged visually, without writing interpolated observations to
  the stored history. In Evolution, points with flag 2 (gCO2/L/d transition)
  are shaded in grey with a "transition" label, so the rate line across them
  reads as a bridge and not as a measurement; each band runs to the next point,
  or one sample interval (15 min) past the last flagged point when the next is
  missing or more than two intervals away. Drag-selection zooms; the range selector and bright
  time slider provide zoom and horizontal navigation. The HTML, JS, CSS, and uPlot assets in
  `data/www` must be uploaded to LittleFS for the page to load.
  During development the same `data/www/graphs.html` can be opened directly
  from the PC filesystem. Its JS/CSS paths are relative; in `file://` mode the
  page fetches CSV from the test device at `192.168.13.180`. The Export CSV
  link also points directly to this device in `file://` mode. There is no local
  file picker. The CSV endpoint alone sends
  `Access-Control-Allow-Origin: *` to permit this read-only preview; any website
  able to reach the device can read that CSV. On the device, `/graphs` redirects
  to `/graphs/` and the page fetches `/graphs/data.csv` automatically.
- `/graphs` retains one CSV download at `/graphs/data.csv`. The Debug Params page
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
- `partitions.csv` retains both OTA firmware slots and the coredump at their
  original offsets. The former `0x180000`-byte filesystem region is split into
  `spiffs` (web assets, `0x670000` + `0x110000` = 1,114,112 B) and `persist`
  (graph history and CO2 buffers, `0x780000` + `0x70000` = 458,752 B).
  Firmware detects whether `persist` exists in the *device's* partition table:
  old devices continue using their single shared LittleFS; migrated devices
  use separate mounts. No failed mount formats existing data. A fully erased
  new `persist` partition is formatted once on first boot.
- ElegantOTA filesystem upload selects the first `spiffs` partition, which is
  intentionally the web partition. PlatformIO normally selects the last one;
  `scripts/webfs_partition.py` redirects its `buildfs`/`uploadfs` image size
  and USB write offset to the web partition. The `data/` directory must contain
  only deployable assets, never a live copy of a persistence file.
- Migration is **not** a normal firmware OTA. Back up both live files first,
  then install the new partition table over USB, erase only the future persist
  region (`0x780000` through `0x7effff`), upload the web filesystem image,
  and restore the backed-up files to the new data partition. Do not erase the
  whole flash. Merely updating firmware on an old device does not change its
  partition table and remains compatible, but a filesystem-image upload on an
  unmigrated device still overwrites its shared filesystem and live data.
  The first migrated boot can start
  with an empty persist partition, but old history/buffers will then be absent
  unless they were restored. The CO2 buffer restoration also requires its
  saved timestamp to be at most 10 minutes old; preserving its bytes does not
  override that freshness rule.

The Evolution chart loads its observations through the existing chunked CSV
endpoint rather than embedding history in the HTML page.
