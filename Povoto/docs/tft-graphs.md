# TFT graph screen

The measured temperature on the main screen opens Temperature; tapping the
volume in liters opens Attenuation, the pressure in bar opens Pressure, and
the batch name opens Evolution. The 480 x 320 LCARS graph screen's top bar
shows the batch number/name and four view
selectors in Evolution, Temperature, Pressure, Attenuation order. Tapping the
currently selected selector restores the full time range. Temperature,
Pressure and Attenuation are rendered. Evolution displays SG, Temperature,
Pressure and gCO2/L/d simultaneously, each mapped by its own Y scale into the
same plot. Only one Y scale is labeled at a time, initially SG. Tapping the
left axis cycles through those four scales; its labels and line adopt the
selected series color. Only the narrow Y-axis strip is redrawn on that touch,
without refreshing the traces or header.
The header is 26 pixels high. Each selector has its own pastel color and its
label uses the same compact font. The batch number and name both use the
Swiss 911 12pt font; the name is light gray. Selector spacing follows
`TAB_STEP - TAB_W` in the screen layout.

There is no outer graph rectangle. The chart sprite fills the display from
just below the top bar to the bottom edge, with margins only for axis labels.
The selected graph contains measured and
target temperature on a Celsius axis, pressure and target pressure on a bar
axis, or SG and dotted OG on the left axis with ABV (%) on the right. OG comes
from the current batch metadata; other series come from GraphHistory. The
GraphWidget and TraceWidget classes from TFT_eWidget
draw scaled and clipped lines into a TFT_eSPI sprite in PSRAM. The graph takes
a stable snapshot of the existing GraphHistory PSRAM series; it does not read
CSV or LittleFS. Multiple observations in one display column retain their
minimum and maximum, so downsampling does not hide short temperature peaks.
Missing values are skipped and the line bridges them visually, without
changing the saved history.

The four vertical chart regions are the zoom controls. Tapping a region
selects that quarter of the currently visible time range and fills the chart
with its observations. Fewer than two observations cannot be zoomed further.
Tapping the batch number/name in the top-left header, including above the
visible header, returns to the main
screen and releases the snapshot. Reopening the graph restores the full range.
There is no bottom button bar.
Entering the screen saver dismisses the graph without rendering the main
screen while the display is asleep; waking shows the main screen. The graph
redraws after a new history observation; the control calculations are unchanged.
Graph rendering is limited to
the current screen and is not performed continuously while it is closed.
