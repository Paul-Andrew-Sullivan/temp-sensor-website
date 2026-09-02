// Renders thermometer.glb and sets the visible height of its red column.
// The column is the model's own mercury mesh; a clipping plane hides the part
// above the current temperature. -10 °C sits at the top of the bulb, 63 °C at
// the top of the column, matching the lab's required design range.

import * as THREE from "./vendor/three.module.min.js";
import { GLTFLoader } from "./vendor/GLTFLoader.js";

// The model's printed scale runs -40 to 50 °C. These are where those two
// ticks sit along the mercury mesh, as fractions of its full extent (bulb
// bottom = 0, tube top = 1), measured against a render of the model. The
// column is placed so it agrees with the printed numbers; the tube has room
// above 50 up to about 75 °C, which covers the lab's -10 to 63 °C range.
const SCALE = { lowC: -40, lowFrac: 0.303, highC: 50, highFrac: 0.840 };
const TMIN = -40;
const TMAX = 75;

export function makeThermometer(container, modelUrl) {
  const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.localClippingEnabled = true;
  renderer.setClearColor(0x000000, 0);
  container.appendChild(renderer.domElement);

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(28, 1, 0.05, 50);
  scene.add(new THREE.HemisphereLight(0xffffff, 0x8a8478, 1.4));
  const key = new THREE.DirectionalLight(0xffffff, 1.6);
  key.position.set(2, 3, 4);
  scene.add(key);
  const fill = new THREE.DirectionalLight(0xffffff, 0.5);
  fill.position.set(-3, 1, -2);
  scene.add(fill);

  // Clipping plane in world space. Set per frame from `current`.
  const plane = new THREE.Plane(new THREE.Vector3(0, -1, 0), 0);
  let axis = "y";
  let lo = 0, hi = 1;         // mercury extent along the column axis
  let bottom = 0, top = 1;    // world levels for TMIN and TMAX
  let target = null, current = null, ready = false, missing = true;

  function resize() {
    const w = container.clientWidth || 320;
    const h = container.clientHeight || Math.round(w * 1.4);
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  window.addEventListener("resize", resize);
  resize();

  const loader = new GLTFLoader();
  loader.load(modelUrl, (gltf) => {
    const root = gltf.scene;
    root.updateMatrixWorld(true);
    // The file carries a second copy of the thermometer scaled to nothing
    // (a Sketchfab export artifact). The loader rewrites node names, so find
    // it by its world scale instead and drop it before measuring anything.
    const dead = [];
    const ws = new THREE.Vector3();
    root.traverse((o) => {
      if (o === root) return;
      o.getWorldScale(ws);
      if (Math.max(ws.x, ws.y, ws.z) < 1e-6) dead.push(o);
    });
    for (const o of dead) if (o.parent) o.parent.remove(o);

    let mercury = null;
    root.traverse((o) => {
      if (o.isMesh && o.material && /^Thermo_Mercury/.test(o.material.name) && !mercury) mercury = o;
      if (o.isMesh && o.material && o.material.name === "Window_Glass") {
        o.material.transparent = true;
        o.material.depthWrite = false;
        o.renderOrder = 2;
      }
    });

    // Normalize: one unit tall, centred on the origin, so camera and
    // clipping maths work in plain numbers whatever the export scale was.
    const raw = new THREE.Box3().setFromObject(root);
    const rawSize = new THREE.Vector3();
    raw.getSize(rawSize);
    const rawCenter = new THREE.Vector3();
    raw.getCenter(rawCenter);
    const k = 1 / Math.max(rawSize.x, rawSize.y, rawSize.z);
    const holder = new THREE.Group();
    holder.add(root);
    root.position.sub(rawCenter);
    holder.scale.setScalar(k);
    scene.add(holder);
    holder.updateMatrixWorld(true);

    if (mercury) {
      const mat = mercury.material.clone();
      mat.side = THREE.DoubleSide;
      mat.clippingPlanes = [plane];
      mat.clipShadows = true;
      mercury.material = mat;
      const box = new THREE.Box3().setFromObject(mercury);
      const size = new THREE.Vector3();
      box.getSize(size);
      // The column runs along the longest side of the mercury's bounding box.
      axis = size.x > size.y && size.x > size.z ? "x" : size.z > size.y ? "z" : "y";
      const n = new THREE.Vector3(0, 0, 0);
      n[axis] = -1;
      plane.normal.copy(n);
      lo = box.min[axis]; hi = box.max[axis];
      bottom = levelFor(TMIN);
      top = levelFor(TMAX);
    }

    // Frame the whole model.
    const all = new THREE.Box3().setFromObject(holder);
    const c = new THREE.Vector3();
    all.getCenter(c);
    const s = new THREE.Vector3();
    all.getSize(s);
    const h = Math.max(s.x, s.y, s.z);
    const dist = (h / 2) / Math.tan((camera.fov * Math.PI) / 360) * 1.12;
    camera.position.set(c.x + dist * 0.28, c.y + dist * 0.1, c.z + dist);
    camera.lookAt(c);
    ready = true;
    if (current === null) current = bottom;
    container.__thermo = { scene, camera, renderer, mercury, axis, bottom, top, all, plane, levelFor };
    render();
  }, undefined, (err) => {
    console.error("thermometer model failed to load", err);
    container.textContent = "The thermometer model did not load.";
  });

  function levelFor(tempC) {
    const t = Math.max(TMIN, Math.min(TMAX, tempC));
    const perDeg = (SCALE.highFrac - SCALE.lowFrac) / (SCALE.highC - SCALE.lowC);
    const frac = SCALE.lowFrac + (t - SCALE.lowC) * perDeg;
    return lo + (hi - lo) * frac;
  }

  let raf = null;
  function render() {
    if (!ready) return;
    const goal = missing || target === null ? bottom : levelFor(target);
    if (current === null) current = goal;
    const d = goal - current;
    current += Math.abs(d) < 0.0005 ? d : d * 0.12;
    plane.constant = current;
    renderer.render(scene, camera);
    if (Math.abs(goal - current) > 0.0002 && !document.hidden) raf = requestAnimationFrame(render);
    else raf = null;
  }
  function kick() { if (raf === null) raf = requestAnimationFrame(render); }
  document.addEventListener("visibilitychange", () => { if (!document.hidden) kick(); });

  return {
    setTemp(c) { missing = false; target = c; kick(); },
    setMissing() { missing = true; kick(); },
  };
}
