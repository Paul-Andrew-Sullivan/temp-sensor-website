import { makeChart } from "./chart.js";
import { makeThermometer } from "./thermo3d.js";

const $ = (id) => document.getElementById(id);

let unit = "C";
try { unit = localStorage.getItem("unit") || "C"; } catch (e) { /* private mode */ }
let box = "off", plugged = [false, false], on = [true, true], cur = [null, null], lastT = 0;
let shown = 0; // which sensor the model shows
let demo = { on: false, scenario: null };

const chart = makeChart($("chart"), { unit });
const thermo = makeThermometer($("model"), "./models/thermometer.glb");

const f = (c) => (unit === "F" ? (c * 9) / 5 + 32 : c);
const fmt = (c) => f(c).toFixed(1) + " °" + unit;

function drawUnits() {
  $("units").innerHTML = unit === "C"
    ? '<b>°C</b> <a href="#" data-u="F">°F</a>'
    : '<a href="#" data-u="C">°C</a> <b>°F</b>';
}
$("units").addEventListener("click", (e) => {
  const u = e.target.getAttribute("data-u");
  if (!u) return;
  e.preventDefault();
  unit = u;
  try { localStorage.setItem("unit", u); } catch (err) { /* ignore */ }
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
    b.textContent = "Sensor " + (i + 1) + " display: " + (on[i] ? "on" : "off");
    b.className = on[i] ? "" : "off";
  }
  // the model
  if (box === "off" || !plugged[shown]) thermo.setMissing();
  else thermo.setTemp(cur[shown]);
  $("modelline").innerHTML = "The thermometer shows sensor " + (shown + 1) + ". "
    + '<a href="#" id="swap">Show sensor ' + (2 - shown) + " instead</a>.";
  $("swap").onclick = (e) => { e.preventDefault(); shown = 1 - shown; render(); };
  // demo line
  const d = $("demoline");
  if (demo.on) {
    d.hidden = false;
    d.innerHTML = "Showing demo data because no board has reported yet. "
      + '<a href="#" id="demooff">Turn demo off</a>. Scenario: <select id="scenario"></select>';
    const sel = $("scenario");
    for (const s of ["normal", "unplugged1", "unplugged2", "boxoff", "high", "low"]) {
      const o = document.createElement("option");
      o.value = s; o.textContent = s; o.selected = s === demo.scenario;
      sel.appendChild(o);
    }
    sel.onchange = () => postDemo({ scenario: sel.value });
    $("demooff").onclick = (e) => { e.preventDefault(); postDemo({ on: false }); };
  } else if (demo.on === false && box === "off") {
    d.hidden = false;
    d.innerHTML = 'No board is reporting. <a href="#" id="demoon">Turn demo data on</a> to see the page working.';
    $("demoon").onclick = (e) => { e.preventDefault(); postDemo({ on: true }); };
  } else {
    d.hidden = true;
  }
}

function postJSON(url, body, method = "POST") {
  return fetch(url, { method, headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }).then((r) => r.json());
}
function postDemo(body) { postJSON("/api/demo", body).then(() => state()); }
function press(i) { postJSON("/api/button", { sensor: i + 1, on: !on[i] }).then((j) => { on = [j.b1, j.b2]; render(); }); }
$("b1").onclick = () => press(0);
$("b2").onclick = () => press(1);

function history() {
  fetch("/api/history").then((r) => r.json()).then((j) => chart.setSeries(j.s1, j.s2));
}
function state() {
  fetch("/api/state").then((r) => r.json()).then((j) => {
    box = j.box; plugged = [j.s1.plugged, j.s2.plugged]; on = [j.s1.on, j.s2.on]; cur = [j.s1.temp, j.s2.temp];
    demo = { on: j.demo, scenario: j.scenario };
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
setInterval(alerts, 15000);
