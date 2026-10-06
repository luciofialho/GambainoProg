'use strict';

// Web copy of the 480 x 320 TFT main screen (Povoto_UI.cpp screenData).
// Everything is drawn in TFT pixel coordinates; the canvas is scaled as a
// whole to fit the window, up to 960 x 640. Graphs open the /graphs page on
// the matching tab.
(() => {
  const W = 480;
  const H = 320;
  const MAX_SCALE = 2; // 960 x 640 on large screens
  const STATUS_MS = 3000;
  const canvas = document.getElementById('screen');
  const page = document.getElementById('page');
  const back = document.getElementById('back');
  const updated = document.getElementById('updated');
  const ctx = canvas.getContext('2d');

  const rgb = (r, g, b) => `rgb(${r},${g},${b})`;
  const WHITE = '#fff';
  const BLACK = '#000';
  const YELLOW = '#ff0';
  const LIGHTGREY = rgb(214, 210, 214);
  const DARKGREY = rgb(123, 125, 123);
  const GREEN = '#0f0';

  // ---------------------------------------------------------------- fonts
  // Swiss 911 GFX fonts (include/Swiss_911_Extra_Compressed_Regular*pt7b.h):
  // height of '0', xAdvance of '0' and the font's max ascent, in TFT pixels.
  const GFX = {
    7: [10, 5, 10], 10: [15, 7, 15], 12: [17, 9, 17],
    16: [25, 12, 25], 18: [27, 13, 27], 72: [106, 54, 112],
  };
  let gfxFamily = '"Arial Narrow", Arial, sans-serif';
  let digitHeightPerPx = 0.72;
  let digitAdvancePerPx = 0.45;
  const LEFT = 0;
  const CENTER = 1;
  const RIGHT = 2;
  // TFT_eSPI built-in font 2 (16 px): WiFi status and calibration panel.
  const FONT2 = '13px Arial, Helvetica, sans-serif';

  function calibrateGfx() {
    ctx.save();
    ctx.font = `100px ${gfxFamily}`;
    const m = ctx.measureText('0');
    const height = m.actualBoundingBoxAscent + m.actualBoundingBoxDescent;
    if (height > 0) digitHeightPerPx = height / 100;
    if (m.width > 0) digitAdvancePerPx = m.width / 100;
    ctx.restore();
  }

  // Sets the GFX font and returns its horizontal stretch and max ascent.
  function useGfx(pt) {
    const [digitHeight, digitAdvance, ascent] = GFX[pt];
    const px = digitHeight / digitHeightPerPx;
    ctx.font = `${px}px ${gfxFamily}`;
    return { stretch: digitAdvance / (digitAdvancePerPx * px), ascent };
  }

  function drawGfx(text, pt, x, baseline, align, color) {
    ctx.save();
    const { stretch } = useGfx(pt);
    ctx.textAlign = 'left';
    ctx.textBaseline = 'alphabetic';
    ctx.fillStyle = color;
    const width = ctx.measureText(text).width * stretch;
    const left = align === CENTER ? x - width / 2 : align === RIGHT ? x - width : x;
    ctx.translate(left, baseline);
    ctx.scale(stretch, 1);
    ctx.fillText(text, 0, 0);
    ctx.restore();
  }

  // Povoto_UI.cpp textOut(): y is the middle of the string's real height.
  function textOut(align, pt, x, y, text, color) {
    ctx.save();
    const { ascent } = useGfx(pt);
    const m = ctx.measureText(text);
    const realHeight = Math.round(m.actualBoundingBoxAscent + m.actualBoundingBoxDescent);
    ctx.restore();
    const top = y - Math.trunc(realHeight / 2);
    drawGfx(text, pt, x, top + ascent, align, color);
  }

  // TFT_eSPI drawString with a GFX font and TL datum.
  function gfxString(text, pt, x, y, color) {
    drawGfx(text, pt, x, y + GFX[pt][2], LEFT, color);
  }

  // Built-in fonts: datum is two letters as in TFT_eSPI (TL, MR, MC ...).
  function glcd(text, font, x, y, datum, color) {
    ctx.save();
    ctx.font = font;
    ctx.fillStyle = color;
    ctx.textBaseline = datum[0] === 'T' ? 'top' : 'middle';
    ctx.textAlign = datum[1] === 'L' ? 'left' : datum[1] === 'R' ? 'right' : 'center';
    ctx.fillText(text, x, datum[0] === 'T' ? y + 2 : y);
    ctx.restore();
  }

  // ------------------------------------------------------------ primitives
  function fillRect(x, y, w, h, color) {
    ctx.fillStyle = color;
    ctx.fillRect(x, y, w, h);
  }

  // ------------------------------------------------------------- state
  // Nothing but "Loading..." until the background and the first status have
  // arrived (or failed), so the numbers never show without the screen.
  let backgroundSettled = false;
  let firstPollDone = false;
  const background = new Image();
  background.onload = background.onerror = () => {
    backgroundSettled = true;
    render();
  };
  background.src = 'LCars.bmp';

  let status = null;
  let online = false;

  // ----------------------------------------------------------- main screen
  function drawWifi() {
    // The cloud shows the data age below the screen instead.
    if (status && typeof status.updated === 'string') return;
    const text = !online ? 'No connection to Povoto' : status ? status.wifiText : '';
    if (text) {
      fillRect(W - 180, H - 32, 180, 32, BLACK);
      glcd(text, FONT2, W - 5, H - 32 + 9, 'TR', WHITE);
      return;
    }
    const bars = status ? status.wifiBars : 0;
    for (let i = 0; i < 4; ++i) {
      const barHeight = 3 + i * 3;
      fillRect(W - 8 - (3 - i) * 5, H - 6 - barHeight, 3, barHeight,
               i < bars ? GREEN : DARKGREY);
    }
  }

  function drawCalibration() {
    const lines = status && status.calibration;
    if (!lines || !lines[0]) return;
    fillRect(10, 2, 460, 66, BLACK);
    for (let i = 0; i < 3; ++i) glcd(lines[i] || '', FONT2, 16, 5 + i * 20 - 2, 'TL', WHITE);
  }

  function drawMain() {
    fillRect(0, 0, W, H, BLACK);
    if (background.complete && background.naturalWidth) ctx.drawImage(background, 0, 0);
    const s = status;
    if (s) {
      textOut(CENTER, 12, 86, 21, s.batchNumber, WHITE);
      textOut(CENTER, 10, 168, 16, s.batchDate, LIGHTGREY);
      textOut(CENTER, 72, 63, 150, s.povotoNum, LIGHTGREY);
      textOut(CENTER, 16, 343, 27, s.batchName, rgb(238, 214, 157));
      textOut(LEFT, 12, 320, 176, s.ogLabel, BLACK);

      textOut(CENTER, 18, 170, 105, s.temperature, WHITE);
      gfxString('o', 7, 162, 128, WHITE);
      gfxString('C', 12, 170, 128, WHITE);
      textOut(LEFT, 12, 320, 104, 'Target', BLACK);
      textOut(RIGHT, 12, 304, 104, s.tempTarget, YELLOW);
      textOut(LEFT, 12, 320, 132, 'Slow target', BLACK);
      textOut(RIGHT, 12, 304, 132, s.slowTarget, YELLOW);

      textOut(CENTER, 18, 170, 259, s.pressure, WHITE);
      textOut(CENTER, 12, 170, 284, 'bar', WHITE);
      textOut(LEFT, 12, 320, 245, 'Target', BLACK);
      textOut(RIGHT, 12, 304, 245, s.pressureTarget, YELLOW);
      textOut(LEFT, 12, 320, 273, s.co2Label, BLACK);
      textOut(RIGHT, 12, 304, 273, s.co2Mass, YELLOW);

      textOut(CENTER, 18, 170, 179, s.volume, WHITE);
      textOut(CENTER, 12, 170, 204, 'liters', WHITE);
      textOut(RIGHT, 12, 304, 176, s.sg, YELLOW);
      textOut(LEFT, 12, 320, 204, '% ABV', BLACK);
      textOut(RIGHT, 12, 304, 204, s.abv, YELLOW);
    }
    drawWifi();
    drawCalibration();
  }

  // Touch zones in displayUtils.cpp processTouch(). The TFT graph, keyboard,
  // task and batch screens are the existing web pages here. Paths are
  // relative so the page also works under the cloud's /p/<id>/dashboard/;
  // a read-only status (cloud) keeps only the graphs.
  function mainAction(x, y) {
    const editable = !(status && status.readOnly);
    if (editable && x >= 45 && x <= 160 && y <= 50) return () => go('../batch');
    if (editable && x < 80) return () => go('../tasks');
    if (x >= 125 && x <= 220 && y >= 88 && y <= 148) return () => go('../graphs/#temperature');
    if (x >= 235 && x <= 470 && y <= 55) return () => go('../graphs/#evolution');
    if (x >= 125 && x <= 220 && y >= 162 && y <= 219) return () => go('../graphs/#attenuation');
    if (x >= 125 && x <= 220 && y >= 235 && y <= 301) return () => go('../graphs/#pressure');
    if (editable && x >= 260 && x <= 430 && ((y >= 85 && y <= 165) || (y >= 230 && y <= 262)))
      return () => go('../setpoint');
    return null;
  }

  function go(path) {
    window.location.href = path;
  }

  // ------------------------------------------------------------ plumbing
  function drawLoading() {
    fillRect(0, 0, W, H, BLACK);
    glcd('Loading...', '16px Arial, Helvetica, sans-serif', W / 2, H / 2, 'MC', LIGHTGREY);
  }

  function render() {
    if (backgroundSettled && firstPollDone) drawMain();
    else drawLoading();
  }

  // Cloud only: the data age below the screen and the link back to the list.
  function applyPageTexts() {
    const text = status && typeof status.updated === 'string' ? status.updated : '';
    const lineChanged = !text !== !updated.textContent;
    updated.textContent = text;
    if (status && status.back && typeof status.back.href === 'string') {
      back.href = status.back.href;
      back.textContent = `← ${status.back.label}`;
    }
    if (lineChanged) resize(); // the line below the screen appeared or vanished
  }

  // The screen takes what the link, frame and signature leave of the window.
  function resize() {
    const pageBox = page.getBoundingClientRect();
    const canvasBox = canvas.getBoundingClientRect();
    const extraWidth = pageBox.width - canvasBox.width;
    const extraHeight = pageBox.height - canvasBox.height;
    const scale = Math.max(0.1, Math.min((window.innerWidth - extraWidth) / W,
                                         (window.innerHeight - extraHeight) / H, MAX_SCALE));
    const ratio = window.devicePixelRatio || 1;
    canvas.style.width = `${W * scale}px`;
    canvas.style.height = `${H * scale}px`;
    canvas.width = Math.round(W * scale * ratio);
    canvas.height = Math.round(H * scale * ratio);
    ctx.setTransform(canvas.width / W, 0, 0, canvas.height / H, 0, 0);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    render();
  }

  function screenPoint(event) {
    const rect = canvas.getBoundingClientRect();
    return [Math.floor(((event.clientX - rect.left) * W) / rect.width),
            Math.floor(((event.clientY - rect.top) * H) / rect.height)];
  }

  canvas.addEventListener('click', (event) => {
    const action = mainAction(...screenPoint(event));
    if (action) action();
  });
  canvas.addEventListener('mousemove', (event) => {
    canvas.style.cursor = mainAction(...screenPoint(event)) ? 'pointer' : 'default';
  });

  let pollTimer = 0;
  async function poll() {
    clearTimeout(pollTimer);
    try {
      const response = await fetch('status.json', { cache: 'no-store' });
      if (!response.ok) throw new Error(response.statusText);
      status = await response.json();
      applyPageTexts();
      online = true;
    } catch (error) {
      online = false;
    }
    firstPollDone = true;
    render();
    if (!document.hidden) pollTimer = setTimeout(poll, STATUS_MS);
  }

  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) poll();
  });
  window.addEventListener('resize', resize);

  async function start() {
    try {
      const face = new FontFace('Swiss911', 'url(lcars.ttf)');
      document.fonts.add(await face.load());
      gfxFamily = 'Swiss911';
    } catch (error) {
      // Keep the condensed fallback family.
    }
    calibrateGfx();
    resize();
    poll();
  }

  start();
})();
