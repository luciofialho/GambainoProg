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
