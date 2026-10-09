# Web dashboard (/dashboard)

`/dashboard` reproduces the 480 x 320 TFT main screen (`Povoto_UI.cpp`
`screenData`) in the browser. The layout is fixed in TFT pixel coordinates
and drawn on one canvas; the canvas is scaled as a whole to fit the window,
up to 960 x 640 (2x), so it works the same on a PC and on a phone in
landscape. It is also linked
from the main menu.

Files (web LittleFS):
- `data/www/dashboard.html`, `data/www/dashboard.js`.
- `data/www/lcars.ttf.gz`: `Visual/LCARS.ttf` (Swiss 911 Ultra Compressed)
  gzipped; served as `/dashboard/lcars.ttf`. Glyphs are stretched
  horizontally and sized so the digits match the Swiss 911 Extra Compressed
  GFX fonts on the TFT (metrics table `GFX` in `dashboard.js`).
- Background: the same `/LCars.bmp` the TFT uses, served as
  `/dashboard/LCars.bmp` (browsers render BMP natively; not duplicated).

`/dashboard/status.json` returns the main-screen texts already formatted with
the same `snprintf` formats as `screenData()`, plus the WiFi indicator and the
calibration panel lines. The page polls it every 3 s (paused while the tab is
hidden).

Touch zones are the TFT ones. Temperature, batch name, volume and pressure
open the `/graphs` page on the Temperature, Evolution, Attenuation and
Pressure tab (`/graphs/#temperature` etc.; the graphs page keeps the hash in
sync with the selected tab). The batch number opens `/batch`, the left strip
`/tasks` and the target fields `/setpoint` (the web pages replacing the TFT
batch info, task and keyboard screens).

The label next to g CO2 shows gCO2/L/d (`gCO2 [8.3/L/d]`;
`gCO2 [---]` without a value; `g CO2` outside Fermenting), with the
graph's rule (Fermenting only, positive, outside a transition), the same
text as the TFT (`getCO2RateLabel()`). Differences from the TFT: the screen
sits in a rounded frame like the chart of `/graphs`, with a line above it
and, below it, the Povoto logo on the left and the Brewtal logo on the right. The line above has, on the left, `← Back`
in the cloud only (to the Povoto list, from `back` in `status.json`); on the
right, the statistics icon (when `status.json` has `stats`) and, on the
device only, the menu icon (`/config`, the former home page). The device's
root `/` opens the dashboard (or `/wifi` while the setup access point is
on), so pages whose Back/Cancel lead to `/` now return to the dashboard.

The statistics icon toggles a view (the page always opens without it) that replaces
everything to the right of the value blocks (icons, targets, labels and the
orange dividers; the purple bar on the left is redrawn unbroken) with one
panel of batch counters (`stats` in `status.json`, texts as shown), same
font and spacing on every row, numbers aligned right, units in the labels:

- Temperature mode (`ChillHeatMode`: Chill / Heat / Idle), Total chilling
  time and Total heating time (hh:mm:ss, hours unlimited);
- Headspace volume and Dumped volume (L);
- Total reliefs; CO2 headspace, CO2 solution and CO2 vented in two columns,
  mol and g (44.01 g/mol), under a units line.

The set point touch zones are off in this view. The cloud fills `stats` from
the batch state line (every 5 min); it has no temperature mode nor
headspace volume, shown as "-".

Both logos (`/assets/povoto.svg`, cropped to its drawing by CSS, and `/assets/brewtal.svg`; files in `data/www/assets`, in LittleFS like the pages) are inverted to light on the
dark page. In the cloud only, the data age (`updated` in
`status.json`, e.g. "Updated 3 min ago") appears inside the frame, under the
screen and aligned right, instead of the WiFi indicator. `/graphs` has the same signature, and
its `← Back` link returns to the dashboard.

The canvas shows only "Loading..." until both the background image and the
first `status.json` have arrived (or failed), so values never appear on an
empty screen. `/graphs` marks the tab of the URL hash (e.g. `#temperature`)
as soon as the page loads, before the CSV arrives.

Links between the pages are relative (`../graphs/#pressure`, `../batch`), so
the same files also work in the cloud under `/p/<id>/dashboard/`. Two fields
exist only in the cloud's responses: `readOnly` in `status.json` keeps only
the graph touch zones; in `/graphs/meta.json`, `readOnly` hides the menu link
and Export CSV (public links), and `batches`/`batch` show a batch selector.
The data URLs carry the page's query string (`data.csv?batch=160`), which
the device ignores. See docs/cloud-plan.md.

The HTML, JS and CSS of `/dashboard` and `/graphs` are sent with
`Cache-Control: no-cache`, so the browser revalidates them and a new web
filesystem image takes effect immediately.

The web files take ~1.03 MB (mostly `LCars.bmp` and `SplashScreen.bmp`). The
old web partition (1.06 MB, 258 of 272 blocks used) was nearly full; the
October 2026 layout (16 MB flash) gives it 4 MB (docs/graph-history.md).
