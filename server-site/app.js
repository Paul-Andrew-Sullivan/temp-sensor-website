import { makeChart } from "./chart.js";
import { makeThermometer } from "./thermo3d.js";

const $ = (id) => document.getElementById(id);

let unit = "C";
try { unit = localStorage.getItem("unit") || "C"; } catch (e) { /* localStorage blocked */ }
let box = "off", plugged = [false, false], on = [true, true], cur = [null, null], lastT = 0;
let shown = 0; // which sensor the model shows
let dragged = false; // once the slider is touched, it drives the model until "follow live" is clicked
const slider = $("slider");

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
  try { localStorage.setItem("unit", u); } catch (err) { /* localStorage blocked */ }
  drawUnits(); render(); chart.setUnit(unit);
});

function link(text, fn) {
  const a = document.createElement("a");
  a.href = "#";
  a.textContent = text;
  a.onclick = (e) => { e.preventDefault(); fn(); };
  return a;
}

// The poll runs once a second, so the swap link is built here and only has
// its text updated in render(), rather than being replaced on every tick.
const modelText = document.createTextNode("");
const swapLink = link("", () => { shown = 1 - shown; render(); });
$("modelline").append(modelText, swapLink, ".");

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
  // the model: the slider once dragged, otherwise the selected live sensor
  if (dragged) {
    thermo.setTemp(parseFloat(slider.value));
  } else if (box === "off" || !plugged[shown]) {
    thermo.setMissing();
  } else {
    thermo.setTemp(cur[shown]);
    slider.value = cur[shown];
  }
  modelText.nodeValue = "The thermometer shows sensor " + (shown + 1) + ". ";
  swapLink.textContent = "Show sensor " + (2 - shown) + " instead";
}

slider.addEventListener("input", () => {
  dragged = true;
  const c = parseFloat(slider.value);
  $("sliderline").innerHTML = "Set by the slider: " + c.toFixed(1) + " °C, " + ((c * 9) / 5 + 32).toFixed(1) + " °F. "
    + '<a href="#" id="live">Follow the live reading again</a>.';
  $("live").onclick = (e) => { e.preventDefault(); dragged = false; $("sliderline").textContent = "The thermometer is following the live reading."; render(); };
  render();
});

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
