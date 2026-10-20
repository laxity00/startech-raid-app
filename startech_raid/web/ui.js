"use strict";
const $ = id => document.getElementById(id);
const text = (id, value) => { $(id).textContent = value ?? "—"; };
const date = value => value ? new Date(value * 1000).toLocaleString() : "Not yet collected";
let page = "overview";
async function read(path) {
  const response = await fetch(path, {cache: "no-store", credentials: "same-origin"});
  if (!response.ok) throw new Error(`Page request failed (${response.status}). Open this app through Home Assistant.`);
  return response.json();
}
document.querySelectorAll("[data-page]").forEach(button => button.addEventListener("click", () => {
  page = button.dataset.page;
  document.querySelectorAll(".page").forEach(section => { section.hidden = section.id !== page; });
  document.querySelectorAll("[data-page]").forEach(tab => tab.classList.toggle("active", tab === button));
  refresh();
}));
$("show-token").addEventListener("click", () => {
  const show = $("api-token").type === "password";
  $("api-token").type = show ? "text" : "password";
  text("show-token", show ? "Hide" : "Show");
});
function pair(list, key, value) {
  const term = document.createElement("dt"), definition = document.createElement("dd");
  term.textContent = key; definition.textContent = value ?? "Unknown"; list.append(term, definition);
}
function drives(data) {
  const cards = $("drive-cards"); cards.replaceChildren();
  for (const port of [0, 1]) {
    const disk = data.ssds.find(d => d.port === port), known = data.known_ssds.find(d => d.port === port);
    const valid = data.query_ok && disk?.identity_verified, identity = valid ? disk : known;
    const card = document.createElement("article"), title = document.createElement("h2"), info = document.createElement("dl");
    title.textContent = `SSD port ${port}`; card.append(title);
    const temperature = document.createElement("div"); temperature.className = "temp";
    temperature.textContent = valid && disk.temperature_candidate_c !== null ? `${disk.temperature_candidate_c} °C` : "Unknown";
    card.append(temperature);
    const note = document.createElement("p"); note.textContent = "Temperature candidate · vendor encoding not independently verified"; card.append(note);
    pair(info, "Model", identity?.model); pair(info, "Serial", identity?.serial); pair(info, "Firmware", identity?.firmware);
    pair(info, "Capacity", identity?.capacity_bytes ? `${(identity.capacity_bytes / 1e9).toFixed(2)} GB · ${identity.capacity_bytes.toLocaleString()} bytes` : null);
    pair(info, "Current visibility", valid ? "Identified independently" : "Not verified in this sample");
    pair(info, "Pre-fail thresholds", valid ? disk.attribute_check : "Unknown");
    pair(info, "Attributes compared", valid ? disk.attributes_compared : null);
    pair(info, "SMART overall", "Unknown · RETURN STATUS unavailable");
    pair(info, "Port error", disk?.error || "None reported"); card.append(info);
    const details = document.createElement("details"), summary = document.createElement("summary");
    summary.textContent = "SMART attribute table"; details.append(summary);
    const table = document.createElement("table"), head = document.createElement("tr");
    for (const name of ["ID", "Current", "Worst", "Threshold", "Raw"]) { const cell = document.createElement("th"); cell.textContent = name; head.append(cell); }
    table.append(head);
    for (const a of valid && disk.smart_data_valid ? disk.attributes : []) {
      const row = document.createElement("tr");
      for (const v of [a.id, a.normalized, a.worst, a.threshold, a.raw]) { const cell = document.createElement("td"); cell.textContent = v ?? "—"; row.append(cell); }
      table.append(row);
    }
    details.append(table); card.append(details); cards.append(card);
  }
}
function chart(samples) {
  const svg = $("chart"), legend = $("chart-legend"), ns = "http://www.w3.org/2000/svg";
  svg.replaceChildren(); legend.replaceChildren();
  const serials = [...new Set(samples.flatMap(s => s.ssds.map(d => d.serial)).filter(Boolean))];
  const points = samples.flatMap(s => s.ssds.map(d => d.temperature)).filter(v => typeof v === "number");
  if (!points.length) { text("chart-note", "Temperature history will appear after successful SSD collection."); return; }
  const low = Math.floor(Math.min(...points) / 5) * 5 - 5, high = Math.ceil(Math.max(...points) / 5) * 5 + 5;
  const start = samples[0].time, end = Math.max(start + 1, samples.at(-1).time);
  for (let value = low; value <= high; value += 5) {
    const y = 205 - (value - low) / (high - low) * 180;
    const line = document.createElementNS(ns, "line");
    for (const [key, v] of Object.entries({x1: 55, x2: 880, y1: y, y2: y, stroke: "#30424f"})) line.setAttribute(key, v);
    const label = document.createElementNS(ns, "text"); label.setAttribute("x", "8"); label.setAttribute("y", y + 5); label.setAttribute("fill", "#a4b7c5"); label.textContent = `${value}°C`; svg.append(line, label);
  }
  serials.forEach((serial, index) => {
    const color = ["#7cd6c4", "#f3be78", "#a3a9ff"][index % 3], item = document.createElement("span"); item.style.color = color; item.textContent = `SSD …${serial.slice(-6)}`; legend.append(item);
    let segment = [];
    const flush = () => { if (!segment.length) return; const poly = document.createElementNS(ns, "polyline"); poly.setAttribute("points", segment.join(" ")); poly.setAttribute("stroke", color); poly.setAttribute("fill", "none"); poly.setAttribute("stroke-width", "3"); svg.append(poly); segment = []; };
    samples.forEach(s => { const value = s.ssds.find(d => d.serial === serial)?.temperature;
      if (typeof value !== "number") { flush(); return; }
      const x = 55 + (s.time - start) / (end - start) * 825, y = 205 - (value - low) / (high - low) * 180;
      segment.push(`${x},${y}`);
      const dot = document.createElementNS(ns, "circle"); dot.setAttribute("cx", x); dot.setAttribute("cy", y); dot.setAttribute("r", "3"); dot.setAttribute("fill", color); svg.append(dot);
    }); flush();
  });
  text("chart-note", `${date(start)} → ${date(end)}. Gaps represent samples without verified temperature data.`);
}
async function refresh() {
  try {
    const data = await read("api/status");
    $("error").hidden = true;
    text("connection", data.fresh ? "Collector connected" : "Waiting / stale"); text("raid", data.raid_display);
    text("last", date(data.sample_time_unix)); text("age", data.fresh ? "Fresh cached sample" : "No fresh sample · HA sensors will be unavailable");
    text("smart", data.service.smart_paused ? "Paused after error" : !data.service.smart_enabled ? "Disabled" : data.smart_complete ? "Complete" : "Incomplete");
    text("smart-note", "SSD health is separate from RAID state"); text("next", date(data.service.next_collection_unix));
    text("duration", data.service.duration_ms === null ? "No collection yet" : `${data.service.duration_ms} ms last collection`);
    text("sample-error", data.error ? `Current issue: ${data.error}` : "No collection errors reported.");
    if (page === "drives") { drives(data); chart((await read("api/history")).samples); }
    if (page === "configuration") {
      const config = await read("api/config"); $("settings").replaceChildren();
      pair($("settings"), "Collection interval", `${config.options.interval_seconds} seconds`);
      pair($("settings"), "SSD SMART reads", config.options.enable_smart ? "Enabled" : "Disabled");
      pair($("settings"), "Temperature warning", `${config.options.temperature_warning_c} °C · user threshold`);
      pair($("settings"), "Log level", config.options.log_level);
      text("api-host", config.host); $("api-token").value = config.api_token; text("hardware", config.hardware);
    }
    if (page === "logs") text("log-text", (await read("api/logs")).lines.join("\n") || "No entries yet.");
    if (page === "documentation") text("docs", (await read("api/docs")).text);
  } catch (error) { text("connection", "Disconnected"); text("error", error.message); $("error").hidden = false; text("raid", "Unknown - service unavailable"); }
}
refresh(); setInterval(refresh, 15000);
