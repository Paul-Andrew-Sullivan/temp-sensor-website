// Chart recorder: 300 seconds, fixed 10–50 °C axis, newest sample on the right.
// Missing samples are hatched. Off-scale samples are pinned to the edge in red.
// The same drawing code is inlined in esp32-site/index.html; keep them in step.

export const N = 300;
export const YMIN = 10;
export const YMAX = 50;

export function makeChart(canvas, opts = {}) {
  const cx = canvas.getContext("2d");
  const colors = Object.assign({ ink: "#1a1a1a", faint: "#6b675f", rule: "#cfcac0", hatch: "#e2ded6", red: "#b3261e" }, opts.colors || {});
  let unit = opts.unit || "C";
  let series = [new Array(N).fill(null), new Array(N).fill(null)];

  const f = (c) => (unit === "F" ? (c * 9) / 5 + 32 : c);

  function hatch(x, y, w, h) {
    cx.save();
    cx.beginPath();
    cx.rect(x, y, w, h);
    cx.clip();
    cx.strokeStyle = colors.hatch;
    cx.lineWidth = 2;
    for (let d = -h; d < w; d += 12) {
      cx.beginPath();
      cx.moveTo(x + d, y + h);
      cx.lineTo(x + d + h, y);
      cx.stroke();
    }
    cx.restore();
  }

  function offscale(x, w, y) {
    cx.save();
    cx.fillStyle = colors.red;
    cx.fillRect(x - w / 2, y - 3, w + 1, 6);
    cx.restore();
  }

  function draw() {
    const W = canvas.width, H = canvas.height;
    const L = 110, R = 30, T = 30, B = 70;
    const pw = W - L - R, ph = H - T - B;
    cx.clearRect(0, 0, W, H);
    cx.font = "24px Literata, Georgia, serif";
    cx.fillStyle = colors.faint;
    cx.strokeStyle = colors.rule;
    cx.lineWidth = 2;

    const lo = f(YMIN), hi = f(YMAX);
    for (let k = 0; k <= 4; k++) {
      const y = T + (ph * k) / 4;
      cx.beginPath(); cx.moveTo(L, y); cx.lineTo(W - R, y); cx.stroke();
      const v = hi - ((hi - lo) * k) / 4;
      cx.textAlign = "right";
      cx.fillText(v.toFixed(0) + "°" + unit, L - 14, y + 8);
    }
    for (let sec = 300; sec >= 0; sec -= 60) {
      const x = L + pw * (1 - sec / 300);
      cx.beginPath(); cx.moveTo(x, T); cx.lineTo(x, T + ph); cx.stroke();
      cx.textAlign = "center";
      cx.fillText(sec, x, T + ph + 34);
    }
    cx.fillText("seconds ago", L + pw / 2, H - 10);

    const colw = pw / N;
    // Hatch runs where both sensors are missing (box off, or both unplugged).
    let m = null;
    for (let i = 0; i < N; i++) {
      const miss = series[0][i] == null && series[1][i] == null;
      if (miss && m === null) m = i;
      if ((!miss || i === N - 1) && m !== null) {
        const a = L + colw * m, b = L + colw * (miss ? i + 1 : i);
        hatch(a, T, b - a, ph);
        m = null;
      }
    }

    const lineColors = [colors.ink, colors.faint];
    for (let si = 0; si < 2; si++) {
      cx.strokeStyle = lineColors[si];
      cx.lineWidth = si ? 2.5 : 3.5;
      cx.setLineDash(si ? [8, 6] : []);
      cx.beginPath();
      let pen = false;
      for (let j = 0; j < N; j++) {
        const v = series[si][j];
        if (v == null) { pen = false; continue; }
        const x = L + colw * (j + 0.5);
        if (v > YMAX || v < YMIN) { pen = false; offscale(x, colw, v > YMAX ? T : T + ph); continue; }
        const y = T + ph * (1 - (v - YMIN) / (YMAX - YMIN));
        if (pen) cx.lineTo(x, y); else cx.moveTo(x, y);
        pen = true;
      }
      cx.stroke();
      cx.setLineDash([]);
    }
    cx.fillStyle = colors.ink; cx.textAlign = "left"; cx.fillText("— sensor 1", L, T - 8);
    cx.fillStyle = colors.faint; cx.fillText("- - sensor 2", L + 150, T - 8);
  }

  return {
    draw,
    setUnit(u) { unit = u; draw(); },
    setSeries(s1, s2) { series = [s1.slice(-N), s2.slice(-N)]; draw(); },
    push(v1, v2) {
      series[0].push(v1); series[1].push(v2);
      if (series[0].length > N) series[0].shift();
      if (series[1].length > N) series[1].shift();
      draw();
    },
  };
}
