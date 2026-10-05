// The synthetic fermentation profile of graphSyntheticPoint()
// (Povoto/src/GraphHistory.cpp). Keep both in step.
export const SYNTHETIC_OG = 1.054;
export const SYNTHETIC_FLAG = 4;
export const TRANSITION_FLAG = 2;
export const PROFILE_DAYS = 14;

const PI2 = 6.28318530718;

function smoothStep(value) {
  if (value <= 0) return 0;
  if (value >= 1) return 1;
  return value * value * (3 - 2 * value);
}

export function syntheticPoint(day) {
  const dailyPhase = PI2 * day;
  const sg = SYNTHETIC_OG - 0.004 * smoothStep((day - 0.3) / 1.6)
    - 0.032 * smoothStep((day - 1.3) / 8.8)
    - 0.007 * smoothStep((day - 7.0) / 7.0);
  const abv = 100 * (105 * (SYNTHETIC_OG - sg) / (100 - sg) * (sg / 0.79));
  const temperatureSetpoint = day < 8 ? 19 : (day < 12.5 ? 20 : 21);
  const temperature = 19 + smoothStep((day - 8) / 0.6) + smoothStep((day - 12.5) / 0.6)
    + 0.14 * Math.sin(dailyPhase - 0.5) + 0.06 * Math.sin(PI2 * day * 5);
  const pressureSetpoint = 0.20 + 0.90 * smoothStep((day - 0.7) / 7.8)
    + 0.55 * smoothStep((day - 13.25) / 0.35);
  const pressure = pressureSetpoint - 0.08 * Math.exp(-day / 1.0) + 0.035 * Math.sin(PI2 * day * 1.7);
  const mainPeak = (day - 4.5) / 2.8;
  const tailPeak = (day - 9.0) / 1.6;
  let co2Rate = 0.35 + 12 * Math.exp(-0.5 * mainPeak * mainPeak) + 1.2 * Math.exp(-0.5 * tailPeak * tailPeak);
  let flags = SYNTHETIC_FLAG;
  if ((day >= 8 && day < 8.7) || (day >= 12.5 && day < 13.2) || (day >= 13.25 && day < 13.7)) {
    co2Rate = null;
    flags |= TRANSITION_FLAG;
  }
  return { sg, abv, temperature, temperatureSetpoint, pressure, pressureSetpoint, co2Rate, flags };
}

// Profile day of a record (CloudLog.cpp: day 0 = synthetic start, repeating).
export function syntheticDay(epoch, start) {
  return epoch > start ? ((epoch - start) / 86400) % PROFILE_DAYS : 0;
}
