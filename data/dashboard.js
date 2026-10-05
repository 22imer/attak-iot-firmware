"use strict";

// Dashboard client for attak-iot-firmware.
//
// Single classic script (no bundler/framework). It owns display state, command
// correlations, freshness and the browser-only event log. The firmware is the
// single source of module state: this code never optimistically toggles a
// module and never invents mock data when the device is absent.

(function () {
  // ---- Spec constants (§6, §8, §9) ----
  const MODULE_IDS = ["cc1101", "nrf24", "pn532", "ir", "wifi"];
  const ACTION_STATES = new Set(["idle", "running", "succeeded", "timeout", "error"]);
  const ACTION_ERRORS = new Set([
    "", "scan_failed", "scan_timeout", "read_timeout", "capture_timeout", "capture_too_long", "hardware_error",
  ]);
  const STATUS_STALE_MS = 5000;
  const ACK_TIMEOUT_MS = 3000;
  const MAX_LOG_EVENTS = 200;
  const MAX_RECENT_COMMANDS = 200;
  const RECONNECT_DELAYS_MS = [1000, 2000, 4000, 8000];
  const MAX_COMMAND_BYTES = 512;
  const TICK_MS = 250;

  const MODULE_META = {
    cc1101: { label: "CC1101", sub: "Sub-GHz RF" },
    nrf24: { label: "NRF24", sub: "2.4GHz" },
    pn532: { label: "PN532", sub: "NFC" },
    ir: { label: "IR", sub: "IR RX" },
    wifi: { label: "WiFi", sub: "AP / scan" },
  };
  const MODULE_ACTIONS = {
    cc1101: [],
    nrf24: [],
    pn532: [{ action: "read_uid", label: "Đọc UID" }],
    ir: [{ action: "capture", label: "Capture" }],
    wifi: [{ action: "scan", label: "Quét WiFi" }],
  };
  const TRANSPORT_TEXT = {
    connecting: "Đang kết nối…", live: "Đã kết nối", reconnecting: "Đang kết nối lại…", offline: "Mất kết nối",
  };
  const ACTION_STATE_TEXT = {
    idle: "Rảnh", running: "Đang chạy", succeeded: "Thành công", timeout: "Hết thời hạn", error: "Lỗi",
  };
  const ACTION_ERROR_TEXT = {
    scan_failed: "Quét WiFi thất bại",
    scan_timeout: "Quét WiFi quá hạn 15 giây",
    read_timeout: "Không thấy thẻ trong 5 giây",
    capture_timeout: "Không có tín hiệu IR trong 10 giây",
    capture_too_long: "Thông điệp IR vượt 512 timing",
    hardware_error: "Lỗi phần cứng",
  };
  const COMMAND_ERROR_TEXT = {
    invalid_command: "Lệnh không hợp lệ",
    unsupported_action: "Hành động không được hỗ trợ",
    queue_full: "Hàng đợi đầy",
    module_off: "Module đang tắt",
    busy: "Đang bận hoặc đang giải phóng",
    hardware_error: "Lỗi phần cứng",
  };

  // ---- State ----
  const state = {
    transport: "connecting",
    selectedModule: MODULE_IDS[0],
    socket: null,
    socketToken: 0,
    reconnectAttempt: 0,
    reconnectTimer: 0,
    nextCommandId: 1,
    pending: new Map(),         // id -> { id, module, cmd, action, socketToken, sentAtMs }
    recentCommands: new Map(),  // bounded history; keeps unresolved (unknown) records for late acks
    log: [],                    // oldest-first, capped
    logFilter: "all",
    modules: {},
    dirty: true,
  };

  const dom = {};

  function makeModuleState() {
    return {
      status: null,
      receivedAtMs: null,       // performance.now() of the last valid status
      lastOutput: "",
      payload: null,            // decoded output object, or null
      resultReceivedAtIso: null,
      signature: "",
      stale: true,
    };
  }

  // ---- DOM helpers (device text never enters innerHTML) ----
  function el(tag, props, children) {
    const node = document.createElement(tag);
    if (props) {
      for (const [key, value] of Object.entries(props)) {
        if (value === null || value === undefined) continue;
        if (key === "class") node.className = value;
        else if (key === "text") node.textContent = value;
        else if (key === "disabled") node.disabled = Boolean(value);
        else if (key.startsWith("on") && typeof value === "function") node.addEventListener(key.slice(2), value);
        else node.setAttribute(key, String(value));
      }
    }
    for (const child of children || []) if (child) node.append(child);
    return node;
  }
  function clearNode(node) { while (node.firstChild) node.removeChild(node.firstChild); }
  function isUint32(value) { return Number.isInteger(value) && value >= 0 && value <= 0xFFFFFFFF; }

  // ---- Validation ----
  function validateStatus(raw) {
    if (!raw || typeof raw !== "object") return null;
    if (!MODULE_IDS.includes(raw.module)) return null;
    if (typeof raw.enabled !== "boolean" || typeof raw.connected !== "boolean") return null;
    if (typeof raw.detail !== "string" || typeof raw.output !== "string") return null;
    if (!isUint32(raw.lastUpdateMs) || !isUint32(raw.resultSequence) || !isUint32(raw.resultUpdateMs)) return null;
    if (typeof raw.actionState !== "string" || !ACTION_STATES.has(raw.actionState)) return null;
    if (typeof raw.actionError !== "string" || !ACTION_ERRORS.has(raw.actionError)) return null;
    if (typeof raw.cleanupPending !== "boolean") return null;
    if ((raw.actionState === "idle" || raw.actionState === "running" || raw.actionState === "succeeded") &&
        raw.actionError !== "") return null;
    if (!raw.enabled && (raw.connected || raw.actionState !== "idle")) return null;
    return {
      module: raw.module, enabled: raw.enabled, connected: raw.connected, detail: raw.detail, output: raw.output,
      lastUpdateMs: raw.lastUpdateMs, actionState: raw.actionState, actionError: raw.actionError,
      cleanupPending: raw.cleanupPending, resultSequence: raw.resultSequence, resultUpdateMs: raw.resultUpdateMs,
    };
  }

  function decodePayload(module, output) {
    let parsed;
    try { parsed = JSON.parse(output); } catch { return null; }
    if (!parsed || typeof parsed !== "object") return null;
    if (module === "wifi") return parsed.kind === "wifi_scan" && Array.isArray(parsed.networks) ? parsed : null;
    if (module === "pn532") return parsed.kind === "nfc_uid" && typeof parsed.uid === "string" ? parsed : null;
    if (module === "ir") return parsed.kind === "ir_capture" ? parsed : null;
    return null;
  }

  // ---- Event log ----
  function logEvent(module, type, content) {
    state.log.push({ receivedAtIso: new Date().toISOString(), module: module || null, type, content: String(content) });
    if (state.log.length > MAX_LOG_EVENTS) state.log.splice(0, state.log.length - MAX_LOG_EVENTS);
    state.dirty = true;
  }

  // ---- Transport ----
  function wsUrl() {
    const scheme = location.protocol === "https:" ? "wss" : "ws";
    return `${scheme}://${location.host}/ws`;
  }
  function setTransport(next) {
    state.transport = next;
    state.dirty = true;
    if (dom.connIndicator) render(); // reflect live/reconnecting without waiting for the tick
  }

  function connect() {
    clearTimeout(state.reconnectTimer);
    const token = ++state.socketToken;
    let socket;
    try { socket = new WebSocket(wsUrl()); } catch { scheduleReconnect(); return; }
    state.socket = socket;
    setTransport("connecting");

    socket.onopen = () => {
      if (token !== state.socketToken) return;
      state.reconnectAttempt = 0;
      setTransport("live");
      refreshStaleness();
      render();
      logEvent(null, "transport", "Đã kết nối tới thiết bị");
      render();
    };
    socket.onmessage = (event) => { if (token === state.socketToken) onMessage(event.data); };
    socket.onclose = () => { if (token === state.socketToken) handleClose(); };
    socket.onerror = () => { /* close follows */ };
  }

  function scheduleReconnect() {
    const delay = RECONNECT_DELAYS_MS[Math.min(state.reconnectAttempt, RECONNECT_DELAYS_MS.length - 1)];
    state.reconnectAttempt += 1;
    setTransport("reconnecting");
    state.reconnectTimer = setTimeout(connect, delay);
  }

  function handleClose() {
    state.socket = null; // keep the socket token so stale callbacks are rejected
    setTransport("reconnecting");
    logEvent(null, "transport", "Mất kết nối");
    for (const [id, record] of state.pending) {
      record.outcome = "unknown";
      state.pending.delete(id);
      state.recentCommands.set(id, record);
      logEvent(record.module, "command", `Chưa xác định kết quả lệnh #${id}`);
    }
    trimRecent();
    refreshStaleness();
    render();
    scheduleReconnect();
  }

  function refreshStaleness() {
    const now = performance.now();
    let changed = false;
    for (const id of MODULE_IDS) {
      const module = state.modules[id];
      const fresh = state.transport === "live" && module.receivedAtMs !== null &&
        (now - module.receivedAtMs) < STATUS_STALE_MS;
      const stale = !fresh;
      if (stale !== module.stale) { module.stale = stale; changed = true; }
    }
    return changed;
  }

  // ---- Inbound frames ----
  function onMessage(text) {
    let frame;
    try { frame = JSON.parse(text); } catch { logEvent(null, "protocol", "Frame không phải JSON hợp lệ"); return; }
    if (frame && typeof frame === "object" && frame.type === "command_result") { handleCommandResult(frame); return; }

    const status = validateStatus(frame);
    if (!status) {
      logEvent(frame && typeof frame === "object" ? frame.module : null, "protocol", "Status không hợp lệ, đã bỏ qua");
      render();
      return;
    }
    applyStatus(status);
    render();
  }

  function applyStatus(status) {
    const module = state.modules[status.module];
    const nowMs = performance.now();
    const nowIso = new Date().toISOString();
    const first = module.receivedAtMs === null;
    const sequenceChanged = status.resultSequence !== module.resultSequence;
    const outputChanged = status.output !== module.lastOutput;

    const signature = [
      status.enabled, status.connected, status.detail, status.actionState, status.actionError,
      status.cleanupPending, status.resultSequence, status.output,
    ].join("|");

    module.status = status;
    module.receivedAtMs = nowMs;
    module.lastOutput = status.output;
    module.stale = false; // a just-arrived status is fresh by definition

    if (status.output === "") {
      module.payload = null;
      module.resultReceivedAtIso = null;
    } else if (first || sequenceChanged || outputChanged) {
      module.payload = decodePayload(status.module, status.output);
      module.resultReceivedAtIso = nowIso; // browser receipt time, never claimed as capture time
    }

    if (signature !== module.signature) {
      module.signature = signature;
      logEvent(status.module, "state", describeStatus(status));
    }
    state.dirty = true;
  }

  function describeStatus(status) {
    const health = healthLevel(status);
    let text = `health=${health} action=${status.actionState}`;
    if (status.actionError) text += `/${status.actionError}`;
    if (status.cleanupPending) text += " cleanup=đang giải phóng";
    return text;
  }

  function trimRecent() {
    while (state.recentCommands.size > MAX_RECENT_COMMANDS) {
      const oldest = state.recentCommands.keys().next().value;
      state.recentCommands.delete(oldest);
    }
  }

  function handleCommandResult(frame) {
    if (!Number.isInteger(frame.id) || !MODULE_IDS.includes(frame.module)) {
      logEvent(null, "protocol", "command_result không hợp lệ");
      return;
    }
    const pending = state.pending.get(frame.id);
    if (pending && pending.socketToken === state.socketToken) {
      state.pending.delete(frame.id);
      pending.ok = frame.ok === true;
      pending.error = String(frame.error || "");
      state.recentCommands.set(frame.id, pending);
      if (pending.ok) logEvent(frame.module, "command", `Đã nhận lệnh #${frame.id}`);
      else logEvent(frame.module, "command", `Lệnh #${frame.id}: ${errorText(pending.error)}`);
    } else {
      const record = state.recentCommands.get(frame.id);
      if (record && record.socketToken === state.socketToken && record.outcome === "unknown") {
        record.ok = frame.ok === true;
        record.error = String(frame.error || "");
        logEvent(frame.module, "command", `Ack muộn cập nhật lệnh #${frame.id}`);
      } else {
        logEvent(frame.module, "protocol", `command_result không khớp lệnh chờ (#${frame.id})`);
      }
    }
    trimRecent();
    state.dirty = true;
    render();
  }

  function errorText(code) { return COMMAND_ERROR_TEXT[code] || code || "lỗi"; }

  // ---- Commands ----
  function isFresh(id) {
    const module = state.modules[id];
    return state.transport === "live" && module.receivedAtMs !== null &&
      (performance.now() - module.receivedAtMs) < STATUS_STALE_MS;
  }

  function sendCommand(module, cmd, action) {
    if (!isFresh(module)) { logEvent(module, "command", "Không gửi: trạng thái chưa mới"); return; }
    if (!state.socket || state.socket.readyState !== WebSocket.OPEN) {
      logEvent(module, "command", "Không gửi: socket chưa mở"); return;
    }
    if (state.nextCommandId > 0xFFFFFFFF) {
      state.nextCommandId = 1;
      logEvent(null, "protocol", "Hết dải id, tạo phiên socket mới");
      state.socket.close();
      return;
    }
    const id = state.nextCommandId++;
    const body = action ? { id, module, cmd, action } : { id, module, cmd };
    const text = JSON.stringify(body);
    if (new TextEncoder().encode(text).length > MAX_COMMAND_BYTES) {
      logEvent(module, "command", "Không gửi: lệnh vượt 512 byte"); return;
    }
    state.pending.set(id, { id, module, cmd, action: action || "", socketToken: state.socketToken, sentAtMs: performance.now() });
    state.socket.send(text);
    logEvent(module, "command", `Gửi ${cmd}${action ? "/" + action : ""} #${id}`);
    state.dirty = true;
    render();
  }

  function expirePending() {
    const now = performance.now();
    let changed = false;
    for (const [id, record] of state.pending) {
      if (now - record.sentAtMs >= ACK_TIMEOUT_MS) {
        record.outcome = "unknown";
        state.pending.delete(id);
        state.recentCommands.set(id, record);
        logEvent(record.module, "command", `Chưa xác định kết quả lệnh #${id}`);
        changed = true;
      }
    }
    if (changed) { trimRecent(); state.dirty = true; }
  }

  // ---- Rendering ----
  function healthLevel(status) {
    if (!status || !status.enabled) return "off";
    return status.connected ? "ready" : "error";
  }
  function dotColor(level) {
    return { off: "#5b5f68", ready: "#4ade80", error: "#f87171" }[level];
  }
  function levelClass(level) {
    if (level === "ready") return "ok";
    if (level === "error") return "err";
    return "";
  }

  function buildSidebar() {
    clearNode(dom.sidebar);
    for (const id of MODULE_IDS) {
      const module = state.modules[id];
      const level = module.stale && module.status ? "stale" : healthLevel(module.status);
      const badge = module.status && module.status.cleanupPending ? el("span", { class: "badge", text: "giải phóng" }) : null;
      const row = el("button", {
        class: "row" + (id === state.selectedModule ? " active" : ""),
        type: "button",
        onclick: () => { state.selectedModule = id; state.dirty = true; render(); },
      }, [
        el("span", { class: "dot", style: `background:${dotColor(level === "stale" ? "off" : level)}` }),
        el("span", { class: "label", text: MODULE_META[id].label }),
        badge,
      ]);
      row.setAttribute("aria-pressed", id === state.selectedModule ? "true" : "false");
      dom.sidebar.append(row);
    }
  }

  function renderOverview() {
    clearNode(dom.overviewGrid);
    const counts = { off: 0, ready: 0, error: 0, stale: 0 };
    for (const id of MODULE_IDS) {
      const module = state.modules[id];
      if (!module.status) { counts.stale += 1; continue; }
      if (module.stale) { counts.stale += 1; continue; }
      counts[healthLevel(module.status)] += 1;
    }
    for (const key of ["off", "ready", "error", "stale"]) {
      dom.overviewGrid.append(el("div", { class: "metric" }, [
        el("div", { class: "k", text: { off: "Tắt", ready: "Sẵn sàng", error: "Lỗi", stale: "Chưa mới / chưa có" }[key] }),
        el("div", { class: "v", text: String(counts[key]) }),
      ]));
    }
  }

  function renderDetail() {
    clearNode(dom.detail);
    const id = state.selectedModule;
    const module = state.modules[id];
    const status = module.status;
    dom.detail.append(el("div", { class: "sub", text: MODULE_META[id].sub }));
    dom.detail.append(el("div", { class: "name", text: MODULE_META[id].label }));

    if (!status) {
      dom.detail.append(el("div", { class: "detailtext", text: "Chưa nhận trạng thái từ thiết bị." }));
      return;
    }

    const level = healthLevel(status);
    const healthText = { off: "Đang tắt", ready: "Sẵn sàng", error: "Lỗi phần cứng" }[level] +
      (module.stale ? " (dữ liệu chưa mới)" : "");
    dom.detail.append(el("div", { class: "status" }, [
      el("span", { class: levelClass(level), text: healthText }),
    ]));
    dom.detail.append(el("div", { class: "detailtext", text: status.detail || "—" }));
    dom.detail.append(el("div", { class: "updated", text: `Cập nhật #${status.lastUpdateMs}` }));

    if (status.cleanupPending) {
      dom.detail.append(el("div", { class: "status" }, [
        el("span", { class: "pill warn", text: "Đang giải phóng tài nguyên" }),
      ]));
    }

    const actionText = `${ACTION_STATE_TEXT[status.actionState] || status.actionState}` +
      (status.actionError ? ` — ${ACTION_ERROR_TEXT[status.actionError] || status.actionError}` : "");
    dom.detail.append(el("div", { class: "status", text: `Tác vụ: ${actionText}` }));

    // Controls
    const controls = el("div", { class: "controls" });
    const frozen = module.stale;
    const enableBtn = el("button", {
      type: "button", text: "Bật", disabled: frozen || status.enabled,
      onclick: () => sendCommand(id, "enable"),
    });
    const disableBtn = el("button", {
      type: "button", text: "Dừng / Tắt", disabled: frozen || !status.enabled,
      onclick: () => sendCommand(id, "disable"),
    });
    controls.append(enableBtn);
    controls.append(disableBtn);
    for (const entry of MODULE_ACTIONS[id]) {
      const locked = frozen || !status.enabled || !status.connected || status.actionState === "running" ||
        status.cleanupPending;
      controls.append(el("button", {
        type: "button", text: entry.label, disabled: locked,
        onclick: () => sendCommand(id, "action", entry.action),
      }));
    }
    dom.detail.append(controls);
  }

  function renderResult() {
    clearNode(dom.result);
    const id = state.selectedModule;
    const module = state.modules[id];
    const status = module.status;
    dom.result.append(el("div", { class: "sub", text: "Kết quả" }));

    if (!status) { dom.result.append(el("div", { class: "detailtext", text: "Chưa có dữ liệu." })); return; }
    if (!module.payload) {
      dom.result.append(el("div", { class: "detailtext", text: "Chưa có kết quả thành công." }));
      return;
    }

    const previous = status.actionState === "running" || status.actionState === "error" || status.actionState === "timeout";
    const meta = [
      `sequence #${status.resultSequence}`,
      `uptime ${status.resultUpdateMs}`,
      previous ? "kết quả lần trước" : "kết quả mới nhất",
      module.resultReceivedAtIso ? `nhận lúc ${module.resultReceivedAtIso}` : "",
      module.stale ? "chưa mới" : "",
    ].filter(Boolean).join(" · ");
    dom.result.append(el("div", { class: "updated", text: meta }));

    if (previous) {
      dom.result.append(el("div", { class: "status" }, [el("span", { class: "pill warn", text: "Kết quả lần trước" })]));
    }

    const payload = module.payload;
    if (id === "wifi") {
      if (payload.truncated) {
        dom.result.append(el("div", { class: "status warn", text: "Chỉ hiển thị 32 AP mạnh nhất." }));
      }
      const table = el("table");
      table.append(el("thead", {}, [el("tr", {}, [
        el("th", { text: "SSID" }), el("th", { text: "BSSID" }), el("th", { text: "RSSI" }),
        el("th", { text: "Kênh" }), el("th", { text: "Bảo mật" }),
      ])]));
      const body = el("tbody");
      for (const network of payload.networks) {
        body.append(el("tr", {}, [
          el("td", { text: network.ssid === "" ? "Mạng ẩn" : String(network.ssid) }),
          el("td", { text: String(network.bssid) }),
          el("td", { text: String(network.rssi) }),
          el("td", { text: String(network.channel) }),
          el("td", { text: network.secure ? "Có" : "Mở" }),
        ]));
      }
      table.append(body);
      dom.result.append(el("div", { class: "scrollx" }, [table]));
    } else if (id === "pn532") {
      dom.result.append(el("pre", { text: `UID: ${payload.uid}` }));
    } else if (id === "ir") {
      dom.result.append(el("pre", {
        text: `protocol: ${payload.protocol}\nvalue: ${payload.value === null ? "null" : payload.value}\n` +
          `rawTimingsUs (${Array.isArray(payload.rawTimingsUs) ? payload.rawTimingsUs.length : 0}): ` +
          `${Array.isArray(payload.rawTimingsUs) ? payload.rawTimingsUs.join(", ") : ""}`,
      }));
    }
  }

  function renderLog() {
    clearNode(dom.logPane);
    const events = state.logFilter === "all" ? state.log : state.log.filter((event) => event.module === state.logFilter);
    for (let i = events.length - 1; i >= 0; i -= 1) {
      const event = events[i];
      dom.logPane.append(el("div", { class: "logline" }, [
        el("span", { class: "t", text: event.receivedAtIso.slice(11, 19) }),
        el("span", { class: "m", text: event.module ? MODULE_META[event.module].label : "—" }),
        el("span", { text: event.content }),
      ]));
    }
  }

  function updateToolbar() {
    const module = state.modules[state.selectedModule];
    dom.exportResult.disabled = !(module.status && module.payload);
    dom.connIndicator.className = state.transport;
    dom.connIndicator.textContent = TRANSPORT_TEXT[state.transport];
    if (dom.logFilter.value !== state.logFilter) dom.logFilter.value = state.logFilter;
  }

  function render() {
    state.dirty = false;
    buildSidebar();
    renderOverview();
    renderDetail();
    renderResult();
    renderLog();
    updateToolbar();
  }

  // ---- Export ----
  function buildLogExport() {
    return {
      exportedAtIso: new Date().toISOString(),
      events: state.log.map((event) => ({
        receivedAtIso: event.receivedAtIso,
        module: event.module,
        type: event.type,
        content: event.content,
      })),
    };
  }
  function buildResultExport() {
    const id = state.selectedModule;
    const module = state.modules[id];
    const status = module.status;
    if (!status || !module.payload) return null;
    return {
      module: id,
      enabled: status.enabled,
      connected: status.connected,
      actionState: status.actionState,
      actionError: status.actionError,
      cleanupPending: status.cleanupPending,
      resultSequence: status.resultSequence,
      resultUpdateMs: status.resultUpdateMs,
      receivedAtIso: module.resultReceivedAtIso,
      exportedAtIso: new Date().toISOString(),
      stale: module.stale,
      previousResult: status.actionState === "running" || status.actionState === "error" || status.actionState === "timeout",
      payload: module.payload,
    };
  }
  function downloadJson(filename, value) {
    const blob = new Blob([JSON.stringify(value, null, 2)], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const anchor = el("a", { href: url, download: filename });
    document.body.append(anchor);
    anchor.click();
    anchor.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }

  // ---- Wiring ----
  function cacheDom() {
    dom.sidebar = document.querySelector("#sidebar");
    dom.overviewGrid = document.querySelector("#overviewGrid");
    dom.detail = document.querySelector("#detail");
    dom.result = document.querySelector("#result");
    dom.logPane = document.querySelector("#logPane");
    dom.logFilter = document.querySelector("#logFilter");
    dom.clearLog = document.querySelector("#clearLog");
    dom.exportLog = document.querySelector("#exportLog");
    dom.exportResult = document.querySelector("#exportResult");
    dom.connIndicator = document.querySelector("#connIndicator");
  }

  function buildFilter() {
    dom.logFilter.append(el("option", { value: "all", text: "Tất cả" }));
    for (const id of MODULE_IDS) dom.logFilter.append(el("option", { value: id, text: MODULE_META[id].label }));
    dom.logFilter.addEventListener("change", () => { state.logFilter = dom.logFilter.value; state.dirty = true; render(); });
  }

  function wireToolbar() {
    dom.clearLog.addEventListener("click", () => {
      state.log = [];
      logEvent(null, "protocol", "Đã xóa log trình duyệt");
      render();
    });
    dom.exportLog.addEventListener("click", () => downloadJson("attak-iot-log.json", buildLogExport()));
    dom.exportResult.addEventListener("click", () => {
      const payload = buildResultExport();
      if (!payload) { logEvent(state.selectedModule, "protocol", "Không có kết quả để xuất"); render(); return; }
      downloadJson(`attak-iot-result-${state.selectedModule}.json`, payload);
    });
  }

  function tick() {
    const staleChanged = refreshStaleness();
    expirePending();
    if (staleChanged || state.dirty) render();
  }

  function init() {
    for (const id of MODULE_IDS) state.modules[id] = makeModuleState();
    cacheDom();
    buildFilter();
    wireToolbar();
    connect();
    render();
    setInterval(tick, TICK_MS);
  }

  init();
})();
