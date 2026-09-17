import { makeChart } from "./chart.js";

const $ = (id) => document.getElementById(id);

let theme = "light";
try { theme = localStorage.getItem("theme") === "dark" ? "dark" : "light"; } catch (e) { /* localStorage blocked */ }
function applyTheme() {
  document.documentElement.dataset.theme = theme;
  $("theme-toggle").textContent = theme === "dark" ? "Light mode" : "Dark mode";
  $("theme-toggle").setAttribute("aria-pressed", String(theme === "dark"));
}
applyTheme();
$("theme-toggle").addEventListener("click", () => {
  theme = theme === "dark" ? "light" : "dark";
  try { localStorage.setItem("theme", theme); } catch (e) { /* localStorage blocked */ }
  applyTheme();
  chart.setUnit(unit);
});

let unit = "C";
try { unit = localStorage.getItem("unit") || "C"; } catch (e) { /* localStorage blocked */ }
let box = "off", plugged = [false, false], on = [true, true], cur = [null, null], lastT = 0;

const chart = makeChart($("chart"), { unit });

const f = (c) => (unit === "F" ? (c * 9) / 5 + 32 : c);
const fmt = (c) => f(c).toFixed(1) + " °" + unit;

function drawUnits() {
  $("units").innerHTML = ["C", "F"].map((u) =>
    `<button type="button" data-u="${u}" aria-label="${u === "C" ? "Celsius" : "Fahrenheit"}" aria-pressed="${unit === u}">${u}</button>`
  ).join("");
}
$("units").addEventListener("click", (e) => {
  const u = e.target.getAttribute("data-u");
  if (u !== "C" && u !== "F") return;
  e.preventDefault();
  unit = u;
  try { localStorage.setItem("unit", u); } catch (err) { /* localStorage blocked */ }
  drawUnits(); render(); chart.setUnit(unit);
});

function render() {
  const bl = $("boxline");
  if (box === "off") {
    bl.textContent = "No data available. The box is switched off or not reachable.";
    bl.className = "status off";
  } else {
    bl.textContent = "The box is on and reporting once a second.";
    bl.className = "status";
  }
  for (let i = 0; i < 2; i++) {
    const el = $("t" + (i + 1));
    if (box === "off") { el.textContent = "no data available"; el.className = "val msg"; }
    else if (!plugged[i]) { el.textContent = "unplugged sensor"; el.className = "val msg bad"; }
    else { el.textContent = fmt(cur[i]); el.className = "val"; }
    const b = $("b" + (i + 1));
    b.textContent = "Display " + (on[i] ? "on" : "off");
    b.setAttribute("aria-checked", String(on[i]));
  }
}

function postJSON(url, body, method = "POST") {
  return fetch(url, { method, headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }).then((r) => r.json());
}
function press(i) { postJSON("/api/button", { sensor: i + 1, on: !on[i] }).then((j) => { on = [j.b1, j.b2]; render(); }); }
$("b1").onclick = () => press(0);
$("b2").onclick = () => press(1);

function history() {
  fetch("/api/history").then((r) => r.json()).then((j) => chart.setSeries(j.s1, j.s2));
}
function state() {
  fetch("/api/state").then((r) => r.json()).then((j) => {
    box = j.box; plugged = [j.s1.plugged, j.s2.plugged]; on = [j.s1.on, j.s2.on]; cur = [j.s1.temp, j.s2.temp];
    const gap = lastT && j.t - lastT > 2500;
    lastT = j.t;
    chart.push(cur[0], cur[1]);
    render();
    if (gap) history();
  }).catch(() => { box = "off"; chart.push(null, null); render(); });
}

function alerts() {
  fetch("/api/alerts").then((r) => r.json()).then((j) => {
    $("email").value = j.email; $("max").value = j.max; $("min").value = j.min;
    $("maxMessage").value = j.maxMessage; $("minMessage").value = j.minMessage;
    $("lastsent").textContent = j.lastSent
      ? "Last message sent " + new Date(j.lastSent.t).toLocaleString() + " for sensor " + j.lastSent.sensor + (j.lastSent.ok ? "." : ", but delivery failed.")
      : "No message has been sent yet.";
  });
}
$("save").onclick = (e) => {
  e.preventDefault();
  const b = { email: $("email").value, max: $("max").value, min: $("min").value, maxMessage: $("maxMessage").value, minMessage: $("minMessage").value };
  postJSON("/api/alerts", b, "PUT").then((j) => {
    $("saved").textContent = j.error ? "Not saved: " + j.error : "Saved.";
    setTimeout(() => { $("saved").textContent = ""; }, 4000);
  });
};

drawUnits(); render(); history(); state(); alerts();
setInterval(state, 1000);
setInterval(history, 30000);
