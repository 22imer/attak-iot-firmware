"use strict";

// Dashboard client for attak-iot-firmware.
//
// Single classic script (no bundler/framework). It owns display state, command
// correlations, freshness and the browser-only event log. The firmware is the
// single source of module state: this code never optimistically toggles a
// module and never invents mock data when the device is absent.
//
// Device-supplied text (SSID, detail strings, UID, protocol names) is only ever
// written via textContent / the `text` prop — never innerHTML — so a hostile
// scan result cannot inject markup into the dashboard.

(function () {
  // ---- Spec constants (§6, §8, §9) ----
  const MODULE_IDS = ["cc1101", "nrf24", "pn532", "ir", "wifi"];
  const ACTION_STATES = new Set(["idle", "running", "succeeded", "timeout", "error"]);
  const ACTION_ERRORS = new Set([
    "", "scan_failed", "scan_timeout", "read_timeout", "capture_timeout", "capture_too_long", "hardware_error",
    "nfc_tag_error",
  ]);
  const STATUS_STALE_MS = 5000;
  const ACK_TIMEOUT_MS = 3000;
  const MAX_LOG_EVENTS = 200;
  const MAX_RECENT_COMMANDS = 200;
  const RECONNECT_DELAYS_MS = [1000, 2000, 4000, 8000];
  const MAX_COMMAND_BYTES = 512;
  const TICK_MS = 250;

  // ---- UI constants ----
  const MAX_TOASTS = 4;
  const TOAST_TTL_MS = 4500;
  const THEME_KEY = "attak-theme";
  const RSSI_MIN = -100; // dBm mapped to 0% bar
  const RSSI_MAX = -30;  // dBm mapped to 100% bar

  const MODULE_META = {
    cc1101: { label: "CC1101", sub: "Sub-GHz RF" },
    nrf24: { label: "NRF24", sub: "2.4GHz" },
    pn532: { label: "PN532", sub: "NFC" },
    ir: { label: "IR", sub: "IR RX / TX" },
    wifi: { label: "WiFi", sub: "AP / quan sát" },
  };
  const LEGAL_TIER_TEXT = {
    observe: "Quan sát", active_own: "Thiết bị của mình", disruptive: "Gây nhiễu",
  };
  // Tier groups shown in the action picker, in display order. Disruptive is last
  // and visually fenced off with a warning.
  const TIER_GROUPS = [
    { tier: "observe", label: "Quan sát", note: "" },
    { tier: "active_own", label: "Thiết bị của mình", note: "" },
    { tier: "disruptive", label: "Gây nhiễu / can thiệp",
      note: "Chỉ dùng trên thiết bị/mạng của bạn hoặc khi được cho phép." },
  ];
  const ACTION_KIND_TEXT = {
    oneshot: "một lần", continuous: "liên tục", record: "ghi", replay: "phát lại",
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
    nfc_tag_error: "Thẻ không hỗ trợ hoặc thao tác NFC thất bại",
  };
  const COMMAND_ERROR_TEXT = {
    invalid_command: "Lệnh không hợp lệ",
    unsupported_action: "Hành động không được hỗ trợ",
    queue_full: "Hàng đợi đầy",
    module_off: "Module đang tắt",
    busy: "Đang bận hoặc đang giải phóng",
    hardware_error: "Lỗi phần cứng",
    invalid_params: "Tham số không hợp lệ",
    buffer_empty: "Chưa có bản ghi trong RAM",
  };
  const LOG_TYPE_TEXT = {
    all: "Mọi loại", command: "Lệnh", state: "Trạng thái", protocol: "Giao thức", transport: "Kết nối",
    capture: "Thu thập",
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
    logFilter: "all",           // module filter
    logType: "all",             // event-type filter
    logSearch: "",              // free-text filter
    catalog: {},                // module id -> [action descriptor] from the device
    selectedAction: Object.create(null), // module id -> chosen action id in the picker
    modules: {},
    paramDrafts: new Map(),      // module/action -> values retained across status renders
    radio: null,
    clientTransport: "ap",       // "usb" | "ap", from the device's per-client transport_info
    clientAddress: "",           // local address the WebSocket arrived on
    sig: {},                    // per-section render signatures (skip unchanged DOM work)
    logView: { key: "", len: -1 }, // last rendered log filter + length, for incremental log
    toasts: [],                 // { id, node, timer }
    nextToastId: 1,
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
      resultSequence: 0,
      signature: "",
      stale: true,
      prevActionState: "idle",  // for transition-based toasts
      stream: null,
      streamSequence: 0,
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
        else if (key === "style") node.setAttribute("style", String(value));
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
    if (typeof raw.hasBuffer !== "boolean" || typeof raw.activeAction !== "string" ||
        !isUint32(raw.actionTicket)) return null;
    if ((raw.actionState === "idle" || raw.actionState === "running" || raw.actionState === "succeeded") &&
        raw.actionError !== "") return null;
    if (!raw.enabled && (raw.connected || raw.actionState !== "idle")) return null;
    return {
      module: raw.module, enabled: raw.enabled, connected: raw.connected, detail: raw.detail, output: raw.output,
      lastUpdateMs: raw.lastUpdateMs, actionState: raw.actionState, actionError: raw.actionError,
      cleanupPending: raw.cleanupPending, resultSequence: raw.resultSequence, resultUpdateMs: raw.resultUpdateMs,
      hasBuffer: raw.hasBuffer, activeAction: raw.activeAction, actionTicket: raw.actionTicket,
    };
  }

  function decodePayload(module, output) {
    let parsed;
    try { parsed = JSON.parse(output); } catch { return null; }
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed) ||
        typeof parsed.kind !== "string" || parsed.kind.length === 0) return null;
    if (module === "wifi" && parsed.kind === "wifi_scan" && !Array.isArray(parsed.networks)) return null;
    if (module === "pn532" && parsed.kind === "nfc_uid" && typeof parsed.uid !== "string") return null;
    return parsed;
  }

  // ---- Event log ----
  function logEvent(module, type, content) {
    state.log.push({ receivedAtIso: new Date().toISOString(), module: module || null, type, content: String(content) });
    if (state.log.length > MAX_LOG_EVENTS) state.log.splice(0, state.log.length - MAX_LOG_EVENTS);
    state.dirty = true;
  }

  // A captured evil-portal form POST is the operator's result, so it goes into
  // the event log (and only there — the stream pane keeps showing the raw
  // frame). The device already decoded the form fields, so the log shows the
  // submitted username/password; the raw body is kept for fields this portal
  // does not recognise.
  function logPortalCapture(payload) {
    // Both the evil-portal payload and the evil-twin scenario stream captured
    // form POSTs with the same shape (capture/captures/user/pass).
    if (!payload || typeof payload !== "object" || (payload.kind !== "evil_portal" && payload.kind !== "evil_twin"))
      return;
    if (!isUint32(payload.capture) || payload.capture === 0) return;
    const index = isUint32(payload.captures) ? ` #${payload.captures}` : "";
    const user = typeof payload.user === "string" ? payload.user : null;
    const pass = typeof payload.pass === "string" ? payload.pass : null;
    if (user === null && pass === null) {
      logEvent("wifi", "capture", `Portal: lần${index} không có trường đăng nhập nhận dạng được`);
      return;
    }
    logEvent("wifi", "capture",
      `Portal${index}: ${user === null ? "(không có)" : user} / ${pass === null ? "(không có)" : pass}`);
  }

  // ---- Toasts ----
  function dismissToast(entry) {
    clearTimeout(entry.timer);
    if (entry.node.parentNode) entry.node.remove();
    const idx = state.toasts.indexOf(entry);
    if (idx !== -1) state.toasts.splice(idx, 1);
  }
  function notify(level, title, body) {
    if (!dom.toasts) return;
    const entry = { id: state.nextToastId++ };
    const close = el("button", { class: "tx", type: "button", "aria-label": "Đóng", text: "×", onclick: () => dismissToast(entry) });
    entry.node = el("div", { class: "toast " + (level || ""), role: "status" }, [
      el("div", { class: "tc" }, [
        el("div", { class: "th", text: title }),
        body ? el("div", { class: "tb", text: body }) : null,
      ]),
      close,
    ]);
    dom.toasts.append(entry.node);
    state.toasts.push(entry);
    entry.timer = setTimeout(() => dismissToast(entry), TOAST_TTL_MS);
    while (state.toasts.length > MAX_TOASTS) dismissToast(state.toasts[0]);
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
      notify("ok", "Đã kết nối", "Dashboard đã nối tới thiết bị.");
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
    const wasLive = state.transport === "live";
    state.socket = null; // keep the socket token so stale callbacks are rejected
    state.clientTransport = "ap"; // unknown until the next connect's transport_info
    setTransport("reconnecting");
    logEvent(null, "transport", "Mất kết nối");
    if (wasLive) notify("warn", "Mất kết nối", "Đang thử kết nối lại…");
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
    if (frame && typeof frame === "object" && frame.type === "catalog") { handleCatalog(frame); return; }
    if (frame && frame.type === "action_output") { handleActionOutput(frame); return; }
    if (frame && frame.type === "transport_info") { handleTransportInfo(frame); return; }
    if (frame && frame.type === "radio_state") { handleRadioState(frame); return; }

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
    const prevActionState = module.prevActionState;

    const signature = [
      status.enabled, status.connected, status.detail, status.actionState, status.actionError,
      status.cleanupPending, status.resultSequence, status.output, status.hasBuffer,
      status.activeAction, status.actionTicket,
    ].join("|");

    if (!module.status || status.actionTicket !== module.status.actionTicket ||
        status.actionState !== "running" || !status.enabled) {
      module.stream = null;
      module.streamSequence = 0;
    }
    module.status = status;
    module.receivedAtMs = nowMs;
    module.lastOutput = status.output;
    module.resultSequence = status.resultSequence;
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

    // Action completion feedback (only on a real transition, so heartbeats stay quiet).
    if (!first && status.actionState !== prevActionState) {
      const label = MODULE_META[status.module].label;
      if (status.actionState === "succeeded") notify("ok", `${label}: thành công`, "Có kết quả mới.");
      else if (status.actionState === "timeout") notify("warn", `${label}: hết thời hạn`, ACTION_ERROR_TEXT[status.actionError] || "");
      else if (status.actionState === "error") notify("err", `${label}: lỗi`, ACTION_ERROR_TEXT[status.actionError] || "Lỗi phần cứng");
    }
    module.prevActionState = status.actionState;
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
    const label = MODULE_META[frame.module].label;
    const pending = state.pending.get(frame.id);
    if (pending && pending.socketToken === state.socketToken) {
      state.pending.delete(frame.id);
      pending.ok = frame.ok === true;
      pending.error = String(frame.error || "");
      state.recentCommands.set(frame.id, pending);
      if (pending.ok) {
        logEvent(frame.module, "command", `Đã nhận lệnh #${frame.id}`);
      } else {
        logEvent(frame.module, "command", `Lệnh #${frame.id}: ${errorText(pending.error)}`);
        notify("err", `${label}: lệnh bị từ chối`, errorText(pending.error));
      }
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

  function handleActionOutput(frame) {
    if (!MODULE_IDS.includes(frame.module) || typeof frame.action !== "string" ||
        !isUint32(frame.ticket) || frame.ticket === 0 || !isUint32(frame.sequence) ||
        frame.sequence === 0 || !isUint32(frame.uptimeMs) ||
        !Object.prototype.hasOwnProperty.call(frame, "payload") ||
        new TextEncoder().encode(JSON.stringify(frame.payload)).length > 1024) {
      logEvent(null, "protocol", "action_output không hợp lệ");
      return;
    }
    const module = state.modules[frame.module];
    const status = module.status;
    if (!status || !isFresh(frame.module) || !status.enabled || status.actionState !== "running" ||
        frame.ticket !== status.actionTicket || frame.action !== status.activeAction ||
        (module.streamSequence !== 0 && ((frame.sequence - module.streamSequence) >>> 0) >= 0x80000000) ||
        frame.sequence === module.streamSequence) return;
    if (frame.module === "wifi") logPortalCapture(frame.payload);
    module.stream = frame;
    module.streamSequence = frame.sequence;
    state.dirty = true;
    render();
  }

  function handleTransportInfo(frame) {
    if (!["usb", "ap"].includes(frame.transport) || typeof frame.address !== "string" ||
        frame.address.length > 45) {
      logEvent(null, "protocol", "transport_info không hợp lệ");
      return;
    }
    const changed = state.clientTransport !== frame.transport;
    state.clientTransport = frame.transport;
    state.clientAddress = frame.address;
    if (changed) {
      logEvent(null, "transport", frame.transport === "usb"
        ? `Điều khiển qua USB (${frame.address}) — dashboard không bị AP chiếm`
        : `Điều khiển qua AP WiFi (${frame.address})`);
      // A transport switch changes whether the current radio window is safe to
      // warn about, so re-announce it with the new transport's wording.
      if (state.radio) announceRadio();
      state.dirty = true;
      render();
    }
  }

  function radioText(radio, usb) {
    if (radio.phase === "await_suspend") {
      return usb
        ? "AP sẽ tạm ngắt nhưng kênh USB vẫn điều khiển được; có thể Dừng ngay từ dashboard."
        : "AP sẽ tạm ngắt. Không thể Dừng từ dashboard khi AP vắng; firmware tự dừng sau tối đa 30 giây và khôi phục AP.";
    }
    if (radio.phase === "restoring") return "Đang khôi phục AP; dashboard tự kết nối lại.";
    if (radio.phase === "idle") {
      return radio.apRequested ? "Kênh AP điều khiển sẵn sàng." : "AP đang tắt; dùng Serial 'ap on' nếu cần AP.";
    }
    return usb ? "Radio đang chạy trong thời hạn cứng; Dừng vẫn khả dụng qua USB." : "Radio đang chạy trong thời hạn cứng.";
  }

  function announceRadio() {
    const radio = state.radio;
    if (!radio) return;
    const usb = state.clientTransport === "usb";
    const text = radioText(radio, usb);
    logEvent(radio.owner || null, "transport", text);
    if ((radio.phase === "await_suspend" || radio.phase === "restoring") && !usb) {
      notify("warn", "Radio độc quyền", text);
    }
  }

  function handleRadioState(frame) {
    if (!["idle", "await_suspend", "active", "restoring"].includes(frame.phase) ||
        !(frame.owner === "" || MODULE_IDS.includes(frame.owner)) ||
        !isUint32(frame.remainingMs) || frame.remainingMs > 30000 ||
        frame.maxDurationMs !== 30000 || frame.graceMs !== 250 ||
        typeof frame.apRunning !== "boolean" ||
        typeof frame.apRequested !== "boolean") {
      logEvent(null, "protocol", "radio_state không hợp lệ");
      return;
    }
    const changed = !state.radio || frame.phase !== state.radio.phase || frame.owner !== state.radio.owner;
    state.radio = frame;
    if (!changed) return;
    announceRadio();
    render();
  }

  function validateParamSpecs(specs) {
    if (!Array.isArray(specs) || specs.length > 8) return null;
    const names = new Set();
    for (const p of specs) {
      if (!p || typeof p.name !== "string" || !p.name || names.has(p.name) ||
          typeof p.label !== "string" || typeof p.required !== "boolean" ||
          !["integer", "number", "boolean", "string"].includes(p.type)) return null;
      names.add(p.name);
      if ((p.type === "integer" || p.type === "number") &&
          (!Number.isFinite(p.min) || !Number.isFinite(p.max) || p.min > p.max)) return null;
      if (p.type === "string" && (!Number.isInteger(p.maxLength) || p.maxLength < 0 || p.maxLength > 64)) return null;
    }
    return specs.map((p) => ({ ...p }));
  }

  function paramDraft(module, entry) {
    const key = `${module}/${entry.id}`;
    if (!state.paramDrafts.has(key)) state.paramDrafts.set(key, Object.create(null));
    return state.paramDrafts.get(key);
  }

  function renderActionParams(module, entry, locked) {
    const drafts = paramDraft(module, entry);
    const group = el("fieldset", { disabled: locked, "aria-label": `Tham số ${entry.label}` },
      [el("legend", { text: entry.label })]);
    for (const p of entry.params) {
      const input = p.type === "boolean"
        ? el("select", { "aria-label": p.label }, [
          el("option", { value: "", text: "Chưa chọn" }),
          el("option", { value: "true", text: "Có" }),
          el("option", { value: "false", text: "Không" }),
        ])
        : el("input", { type: p.type === "string" ? "text" : "number", "aria-label": p.label,
          min: p.min, max: p.max, step: p.type === "integer" ? "1" : "any",
          maxlength: p.type === "string" ? p.maxLength : null });
      input.required = p.required;
      input.value = drafts[p.name] || "";
      input.addEventListener("input", () => { drafts[p.name] = input.value; });
      input.addEventListener("change", () => { drafts[p.name] = input.value; });
      group.append(el("label", {}, [el("span", { text: p.label + (p.required ? " *" : "") }), input]));
    }
    return group;
  }

  function readActionParams(module, entry) {
    const drafts = paramDraft(module, entry);
    const params = Object.create(null);
    for (const p of entry.params) {
      const text = drafts[p.name] || "";
      if (text === "" && !p.required &&
          (p.type !== "string" || !Object.prototype.hasOwnProperty.call(drafts, p.name))) continue;
      let value = text;
      let valid = text !== "";
      if (p.type === "boolean") { valid = text === "true" || text === "false"; value = text === "true"; }
      else if (p.type === "integer" || p.type === "number") {
        value = Number(text);
        valid = text.trim() !== "" && Number.isFinite(value) && value >= p.min && value <= p.max &&
          (p.type !== "integer" || Number.isSafeInteger(value));
      } else {
        valid = !text.includes("\0") && new TextEncoder().encode(text).length <= p.maxLength;
      }
      if (!valid) { notify("warn", "Tham số không hợp lệ", p.label); return null; }
      params[p.name] = value;
    }
    return params;
  }

  // ---- Catalog ----
  function handleCatalog(frame) {
    if (!Array.isArray(frame.modules)) { logEvent(null, "protocol", "catalog không hợp lệ"); return; }
    const next = {};
    for (const entry of frame.modules) {
      if (!entry || !MODULE_IDS.includes(entry.module) || !Array.isArray(entry.actions)) continue;
      const actions = [];
      for (const a of entry.actions) {
        if (!a || typeof a.id !== "string" || !a.id || typeof a.label !== "string" ||
            !["oneshot", "continuous", "record", "replay"].includes(a.kind) ||
            !["observe", "active_own", "disruptive"].includes(a.tier)) continue;
        const params = validateParamSpecs(a.params);
        if (params === null) continue;
        actions.push({
          id: a.id,
          label: a.label,
          kind: typeof a.kind === "string" ? a.kind : "oneshot",
          tier: typeof a.tier === "string" ? a.tier : "observe",
          radioExclusive: a.radioExclusive === true,
          needsBuffer: a.needsBuffer === true,
          params,
        });
      }
      next[entry.module] = actions;
    }
    state.catalog = next;
    logEvent(null, "protocol", "Đã nhận catalog action từ thiết bị");
    state.dirty = true;
    render();
  }

  function actionsFor(id) {
    return state.catalog[id] || [];
  }

  // ---- Commands ----
  function isFresh(id) {
    const module = state.modules[id];
    return state.transport === "live" && module.receivedAtMs !== null &&
      (performance.now() - module.receivedAtMs) < STATUS_STALE_MS;
  }

  function sendCommand(module, cmd, action, params) {
    if (!isFresh(module)) { logEvent(module, "command", "Không gửi: trạng thái chưa mới"); notify("warn", "Chưa gửi được", "Trạng thái module chưa mới."); return; }
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
    if (action) body.params = params || {};
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

  function runAction(moduleId, entry) {
    // Disruptive actions (jammer, beacon spam, deauth, evil portal) require
    // explicit confirmation before they ever reach the device. WiFi ones use the
    // device's own AP, so AP clients may be shadowed while they run — control and
    // Stop then stay available over USB or the Serial console.
    if (entry.tier === "disruptive") {
      const shadow = state.clientTransport === "usb"
        ? "Dashboard vẫn điều khiển được qua USB nên có thể Dừng ngay tại đây."
        : "Với payload WiFi, dashboard có thể bị chiếm — dùng Serial console để Dừng.";
      const ok = window.confirm(
        `"${entry.label}" là tác vụ tầng GÂY NHIỄU/CAN THIỆP. Chỉ dùng trên thiết bị/mạng của bạn hoặc khi được cho phép. ` +
        `${shadow} Tiếp tục?`);
      if (!ok) { logEvent(moduleId, "command", `Đã hủy ${entry.label} (chưa xác nhận)`); return; }
    }
    const params = readActionParams(moduleId, entry);
    if (params === null) return;
    sendCommand(moduleId, "action", entry.id, params);
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
        notify("warn", `${MODULE_META[record.module].label}: chưa có phản hồi`, `Lệnh #${id} chưa được xác nhận.`);
        changed = true;
      }
    }
    if (changed) { trimRecent(); state.dirty = true; }
  }

  // ---- Rendering helpers ----
  function healthLevel(status) {
    if (!status || !status.enabled) return "off";
    return status.connected ? "ready" : "error";
  }
  function dotColor(level) {
    return { off: "var(--off)", ready: "var(--ok)", error: "var(--err)", stale: "var(--warn)" }[level] || "var(--off)";
  }

  function buildSidebar() {
    const sig = state.selectedModule + "|" + MODULE_IDS.map((id) => {
      const m = state.modules[id];
      const level = m.stale && m.status ? "stale" : healthLevel(m.status);
      return id + ":" + level + ":" + (m.status && m.status.cleanupPending ? 1 : 0);
    }).join(",");
    if (sig === state.sig.sidebar) return;
    state.sig.sidebar = sig;

    clearNode(dom.modList);
    for (const id of MODULE_IDS) {
      const module = state.modules[id];
      const level = module.stale && module.status ? "stale" : healthLevel(module.status);
      const badge = module.status && module.status.cleanupPending ? el("span", { class: "badge", text: "giải phóng" }) : null;
      const row = el("button", {
        class: "mod-row" + (id === state.selectedModule ? " active" : ""),
        type: "button",
        onclick: () => { state.selectedModule = id; state.dirty = true; render(); },
      }, [
        el("span", { class: "dot", style: `color:${dotColor(level)};background:${dotColor(level)}` }),
        el("span", { class: "meta" }, [
          el("span", { class: "label", text: MODULE_META[id].label }),
          el("span", { class: "sub", text: MODULE_META[id].sub }),
        ]),
        badge,
      ]);
      row.setAttribute("aria-pressed", id === state.selectedModule ? "true" : "false");
      dom.modList.append(row);
    }
  }

  function renderOverview() {
    const counts = { ready: 0, error: 0, off: 0, stale: 0 };
    for (const id of MODULE_IDS) {
      const module = state.modules[id];
      if (!module.status || module.stale) { counts.stale += 1; continue; }
      counts[healthLevel(module.status)] += 1;
    }
    const sig = `${counts.ready}|${counts.error}|${counts.off}|${counts.stale}`;
    if (sig === state.sig.overview) return;
    state.sig.overview = sig;

    clearNode(dom.overviewGrid);
    const cards = [
      { key: "ready", label: "Sẵn sàng" },
      { key: "error", label: "Lỗi" },
      { key: "off", label: "Tắt" },
      { key: "stale", label: "Chưa mới / chưa có" },
    ];
    for (const card of cards) {
      dom.overviewGrid.append(el("div", { class: "stat " + card.key }, [
        el("div", { class: "v", text: String(counts[card.key]) }),
        el("div", { class: "k", text: card.label }),
      ]));
    }
  }

  function renderDetail() {
    const id = state.selectedModule;
    const module = state.modules[id];
    const status = module.status;

    const catSig = JSON.stringify(actionsFor(id));
    const sig = [id, module.signature, module.stale, status ? 1 : 0, catSig,
      pickerSelection(id, actionsFor(id)) || ""].join("|");
    if (sig === state.sig.detail) return;
    state.sig.detail = sig;

    clearNode(dom.detail);
    const level = healthLevel(status);
    const badgeText = status
      ? ({ off: "Đang tắt", ready: "Sẵn sàng", error: "Lỗi phần cứng" }[level]) + (module.stale ? " · chưa mới" : "")
      : "Chưa có dữ liệu";
    const head = el("div", { class: "detail-head" }, [
      el("div", {}, [
        el("div", { class: "sub", text: MODULE_META[id].sub }),
        el("h2", { class: "name", text: MODULE_META[id].label }),
      ]),
      el("div", { class: "health-badge " + (status ? level : "off") }, [
        el("span", { class: "dot", style: `background:${dotColor(module.stale && status ? "stale" : level)}` }),
        el("span", { text: badgeText }),
      ]),
    ]);
    dom.detail.append(head);

    if (!status) {
      dom.detail.append(el("div", { class: "empty", text: "Chưa nhận trạng thái từ thiết bị." }));
      return;
    }

    const kv = el("dl", { class: "kv" });
    kv.append(el("dt", { text: "Chi tiết" }), el("dd", { text: status.detail || "—" }));
    kv.append(el("dt", { text: "Cập nhật" }), el("dd", {}, [el("span", { class: "mono", text: `uptime ${status.lastUpdateMs} ms` })]));
    dom.detail.append(kv);

    // Status chips
    const chips = el("div", { class: "chips" });
    const actionLabel = ACTION_STATE_TEXT[status.actionState] || status.actionState;
    if (status.actionState === "running") {
      chips.append(el("span", { class: "chip run" }, [el("span", { class: "spin" }), el("span", { text: actionLabel })]));
    } else if (status.actionState !== "idle") {
      const cls = status.actionState === "succeeded" ? "ok" : (status.actionState === "timeout" ? "warn" : "err");
      const txt = actionLabel + (status.actionError ? ` · ${ACTION_ERROR_TEXT[status.actionError] || status.actionError}` : "");
      chips.append(el("span", { class: "chip " + cls, text: txt }));
    } else {
      chips.append(el("span", { class: "chip", text: "Tác vụ: rảnh" }));
    }
    if (status.cleanupPending) chips.append(el("span", { class: "chip warn", text: "Đang giải phóng tài nguyên" }));
    dom.detail.append(chips);

    // Module-level power controls.
    const frozen = module.stale;
    dom.detail.append(el("div", { class: "controls" }, [
      el("button", { class: "btn primary", type: "button", text: "Bật", disabled: frozen || status.enabled,
        onclick: () => sendCommand(id, "enable") }),
      el("button", { class: "btn danger", type: "button", text: "Dừng / Tắt", disabled: frozen || !status.enabled,
        onclick: () => sendCommand(id, "disable") }),
    ]));

    // Action picker: choose one task, grouped by tier, then configure + run it.
    const actions = actionsFor(id);
    if (actions.length) dom.detail.append(renderActionPicker(id, status, frozen, actions));
  }

  // The current action chosen in the picker, defaulting to the first non-disruptive
  // action so a destructive task is never pre-selected. Returns null only when a
  // module exposes disruptive actions exclusively (then the operator must pick one).
  function pickerSelection(id, actions) {
    const current = state.selectedAction[id];
    if (current && actions.some((a) => a.id === current)) return current;
    const safe = actions.find((a) => a.tier !== "disruptive");
    return safe ? safe.id : null;
  }

  // Why an action cannot run right now, or "" when it can. Shown next to the Run
  // button so the operator sees the blocker instead of a silently greyed control.
  function actionLockReason(status, frozen, entry) {
    if (frozen) return "Trạng thái chưa mới";
    if (!status.enabled) return "Bật module trước";
    if (!status.connected) return "Chưa kết nối phần cứng";
    if (status.actionState === "running") return "Đang chạy — Dừng trước";
    if (status.cleanupPending) return "Đang giải phóng tài nguyên";
    if (entry.needsBuffer && !status.hasBuffer) return "Cần bản ghi trong RAM (ghi trước)";
    return "";
  }

  function actionMetaTags(entry) {
    const tags = [el("span", { class: "tag", text: ACTION_KIND_TEXT[entry.kind] || entry.kind })];
    if (entry.radioExclusive) tags.push(el("span", { class: "tag warn", text: "độc quyền radio" }));
    if (entry.needsBuffer) tags.push(el("span", { class: "tag", text: "cần buffer" }));
    if (entry.params.length) tags.push(el("span", { class: "tag", text: `${entry.params.length} tham số` }));
    return tags;
  }

  function renderActionPicker(id, status, frozen, actions) {
    const selectedId = pickerSelection(id, actions);
    const wrap = el("div", { class: "action-picker" }, [el("div", { class: "picker-head", text: "Chọn tác vụ" })]);
    const groups = el("div", { class: "picker-groups", role: "group", "aria-label": "Chọn tác vụ" });
    for (const g of TIER_GROUPS) {
      const inTier = actions.filter((a) => a.tier === g.tier);
      if (!inTier.length) continue;
      const box = el("div", { class: "action-group " + g.tier }, [
        el("div", { class: "group-head" }, [el("span", { class: "group-title", text: g.label })]),
      ]);
      if (g.note) box.append(el("div", { class: "group-note", text: g.note }));
      for (const entry of inTier) {
        const chosen = entry.id === selectedId;
        box.append(el("button", {
          class: "action-opt" + (chosen ? " selected" : ""),
          type: "button", "aria-pressed": chosen ? "true" : "false",
          onclick: () => { state.selectedAction[id] = entry.id; state.dirty = true; render(); },
        }, [
          el("span", { class: "opt-radio", "aria-hidden": "true" }),
          el("span", { class: "opt-body" }, [
            el("span", { class: "opt-label", text: entry.label }),
            el("span", { class: "opt-meta" }, actionMetaTags(entry)),
          ]),
        ]));
      }
      groups.append(box);
    }
    wrap.append(groups);
    wrap.append(renderActionConfig(id, status, frozen, actions.find((a) => a.id === selectedId) || null));
    return wrap;
  }

  function renderActionConfig(id, status, frozen, entry) {
    const box = el("div", { class: "action-config" });
    if (!entry) {
      box.append(el("div", { class: "empty", text: "Chọn một tác vụ ở trên để cấu hình và chạy." }));
      return box;
    }
    // Params stay editable even while the module is off, so a task (e.g. a deauth
    // target) can be prepared before enabling; only the Run button is gated.
    if (entry.params.length) box.append(renderActionParams(id, entry, false));
    const reason = actionLockReason(status, frozen, entry);
    const disruptive = entry.tier === "disruptive";
    const runRow = el("div", { class: "run-row" }, [
      el("button", {
        class: "btn " + (disruptive ? "danger" : "primary"),
        type: "button", text: (disruptive ? "⚠ Chạy " : "▶ Chạy ") + entry.label, disabled: reason !== "",
        title: LEGAL_TIER_TEXT[entry.tier] ? `Tầng: ${LEGAL_TIER_TEXT[entry.tier]}` : null,
        onclick: () => runAction(id, entry),
      }),
    ]);
    if (reason) runRow.append(el("span", { class: "run-hint", text: reason }));
    box.append(runRow);
    return box;
  }

  function rssiPercent(rssi) {
    const clamped = Math.max(RSSI_MIN, Math.min(RSSI_MAX, Number(rssi)));
    return Math.round(((clamped - RSSI_MIN) / (RSSI_MAX - RSSI_MIN)) * 100);
  }
  function rssiColor(rssi) {
    if (rssi >= -60) return "var(--ok)";
    if (rssi >= -75) return "var(--warn)";
    return "var(--err)";
  }

  function renderWifiResult(payload) {
    const wrap = el("div", {});
    if (payload.truncated) wrap.append(el("div", { class: "chip warn", style: "margin-bottom:12px", text: "Chỉ hiển thị 32 AP mạnh nhất" }));

    // RSSI bar chart
    const list = el("div", { class: "rssi-list" });
    for (const network of payload.networks) {
      const rssi = Number(network.rssi);
      const row = el("div", { class: "rssi-row" }, [
        el("div", { class: "ssid" }, [
          el("span", { text: network.ssid === "" ? "〈mạng ẩn〉" : String(network.ssid) }),
          network.secure ? el("span", { class: "lock", text: " 🔒" }) : null,
        ]),
        el("div", { class: "rssi-track" }, [
          el("div", { class: "rssi-fill", style: `width:${rssiPercent(rssi)}%;background:${rssiColor(rssi)}` }),
        ]),
        el("div", { class: "dbm", text: `${Number.isFinite(rssi) ? rssi : "?"} dBm` }),
      ]);
      list.append(row);
    }
    wrap.append(list);

    // Full detail table
    const table = el("table");
    table.append(el("thead", {}, [el("tr", {}, [
      el("th", { text: "SSID" }), el("th", { text: "BSSID" }), el("th", { text: "RSSI" }),
      el("th", { text: "Kênh" }), el("th", { text: "Bảo mật" }),
    ])]));
    const body = el("tbody");
    for (const network of payload.networks) {
      body.append(el("tr", {}, [
        el("td", { text: network.ssid === "" ? "〈mạng ẩn〉" : String(network.ssid) }),
        el("td", {}, [el("span", { class: "mono", text: String(network.bssid) })]),
        el("td", { text: String(network.rssi) }),
        el("td", { text: String(network.channel) }),
        el("td", { text: network.security ? String(network.security) : (network.secure ? "Có" : "Mở") }),
      ]));
    }
    table.append(body);
    wrap.append(el("div", { class: "scrollx", style: "margin-top:14px" }, [table]));
    return wrap;
  }

  function renderResult() {
    const id = state.selectedModule;
    const module = state.modules[id];
    const status = module.status;

    const sig = [
      id, status ? status.resultSequence : -1, status ? status.actionState : "",
      status ? status.actionError : "", module.payload ? 1 : 0, module.stale,
      module.resultReceivedAtIso || "",
      module.streamSequence, status ? status.actionTicket : 0,
    ].join("|");
    if (sig === state.sig.result) return;
    state.sig.result = sig;

    clearNode(dom.result);
    const head = el("div", { class: "result-head" }, [
      el("h2", { class: "card-title", style: "margin:0", text: "Kết quả" }),
    ]);
    const copyBtn = el("button", {
      class: "btn", type: "button", text: "Copy JSON",
      disabled: !(status && module.payload),
      onclick: () => copyResult(),
    });
    const exportBtn = el("button", {
      class: "btn", type: "button", text: "Xuất kết quả",
      disabled: !(status && module.payload),
      onclick: () => {
        const payload = buildResultExport();
        if (!payload) { notify("warn", "Không có kết quả", "Chưa có kết quả để xuất."); return; }
        downloadJson(`attak-iot-result-${id}.json`, payload);
      },
    });
    head.append(el("div", { class: "controls", style: "margin:0" }, [copyBtn, exportBtn]));
    dom.result.append(head);

    if (!status) { dom.result.append(el("div", { class: "empty", text: "Chưa có dữ liệu." })); return; }
    if (module.stream) {
      dom.result.append(el("div", { class: "result-meta", text:
        `Stream ${module.stream.action} · ticket ${module.stream.ticket} · #${module.stream.sequence}` }));
      dom.result.append(el("pre", { text: JSON.stringify(module.stream.payload, null, 2) }));
    }
    if (!module.payload) { dom.result.append(el("div", { class: "empty", text: "Chưa có kết quả thành công." })); return; }

    const previous = status.actionState === "running" || status.actionState === "error" || status.actionState === "timeout";
    const meta = el("div", { class: "result-meta" });
    for (const part of [
      `seq #${status.resultSequence}`,
      `uptime ${status.resultUpdateMs}`,
      previous ? "kết quả lần trước" : "kết quả mới nhất",
      module.resultReceivedAtIso ? `nhận ${module.resultReceivedAtIso.slice(11, 19)}` : "",
      module.stale ? "chưa mới" : "",
    ]) if (part) meta.append(el("span", { text: part }));
    dom.result.append(meta);

    if (previous) dom.result.append(el("div", { class: "chips" }, [el("span", { class: "chip warn", text: "Kết quả lần trước" })]));

    const payload = module.payload;
    if (id === "wifi" && payload.kind === "wifi_scan") {
      dom.result.append(renderWifiResult(payload));
    } else if (id === "pn532" && payload.kind === "nfc_uid") {
      dom.result.append(el("pre", { text: `UID: ${payload.uid}` }));
    } else if (id === "ir" && payload.kind === "ir_capture") {
      const timings = Array.isArray(payload.rawTimingsUs) ? payload.rawTimingsUs : [];
      dom.result.append(el("pre", {
        text: `protocol: ${payload.protocol}\nvalue: ${payload.value === null ? "null" : payload.value}\n` +
          `rawTimingsUs (${timings.length}): ${timings.join(", ")}`,
      }));
    } else {
      dom.result.append(el("div", { class: "result-meta", text: payload.kind }));
      dom.result.append(el("pre", { text: JSON.stringify(payload, null, 2) }));
    }
  }

  function logLine(event) {
    return el("div", { class: "logline" }, [
      el("span", { class: "t", text: event.receivedAtIso.slice(11, 19) }),
      el("span", { class: "m", text: event.module ? MODULE_META[event.module].label : "—" }),
      el("span", { class: "ty " + event.type, text: (LOG_TYPE_TEXT[event.type] || event.type) }),
      el("span", { class: "c", text: event.content }),
    ]);
  }

  function renderLog() {
    const q = state.logSearch.trim().toLowerCase();
    const key = state.logFilter + "|" + state.logType + "|" + q;
    // Only touch the DOM when the filter or the log length actually changed — not
    // on every render tick. This stops the per-tick flicker and keeps the pane's
    // scroll position while the user is reading.
    if (key === state.logView.key && state.log.length === state.logView.len) return;
    const keyChanged = key !== state.logView.key;

    const pane = dom.logPane;
    const prevTop = pane.scrollTop;
    const prevHeight = pane.scrollHeight;

    const events = state.log.filter((event) => {
      if (state.logFilter !== "all" && event.module !== state.logFilter) return false;
      if (state.logType !== "all" && event.type !== state.logType) return false;
      if (q && !event.content.toLowerCase().includes(q)) return false;
      return true;
    });

    clearNode(pane);
    if (events.length === 0) {
      pane.append(el("div", { class: "empty", text: "Không có sự kiện khớp bộ lọc." }));
    } else {
      for (let i = events.length - 1; i >= 0; i -= 1) pane.append(logLine(events[i]));
    }

    // Newest entries render at the top. On a filter change jump to the top;
    // otherwise keep the same entries in view by offsetting for the added height.
    if (keyChanged) {
      pane.scrollTop = 0;
    } else {
      const delta = pane.scrollHeight - prevHeight;
      pane.scrollTop = prevTop > 0 ? Math.max(0, prevTop + delta) : 0;
    }
    state.logView = { key, len: state.log.length };
  }

  function updateToolbar() {
    dom.connIndicator.className = state.transport;
    dom.connText.textContent = TRANSPORT_TEXT[state.transport];
    if (dom.logFilter.value !== state.logFilter) dom.logFilter.value = state.logFilter;
    if (dom.logType.value !== state.logType) dom.logType.value = state.logType;
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

  // ---- Export / copy ----
  function buildLogExport() {
    return {
      exportedAtIso: new Date().toISOString(),
      events: state.log.map((event) => ({
        receivedAtIso: event.receivedAtIso, module: event.module, type: event.type, content: event.content,
      })),
    };
  }
  function buildResultExport() {
    const id = state.selectedModule;
    const module = state.modules[id];
    const status = module.status;
    if (!status || !module.payload) return null;
    return {
      module: id, enabled: status.enabled, connected: status.connected,
      actionState: status.actionState, actionError: status.actionError, cleanupPending: status.cleanupPending,
      resultSequence: status.resultSequence, resultUpdateMs: status.resultUpdateMs,
      receivedAtIso: module.resultReceivedAtIso, exportedAtIso: new Date().toISOString(), stale: module.stale,
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
  function copyResult() {
    const payload = buildResultExport();
    if (!payload) { notify("warn", "Không có kết quả", "Chưa có kết quả để copy."); return; }
    const text = JSON.stringify(payload.payload, null, 2);
    const ok = () => notify("ok", "Đã copy", "Kết quả đã vào clipboard.");
    const fail = () => notify("err", "Copy thất bại", "Trình duyệt chặn clipboard.");
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(ok, () => fallbackCopy(text) ? ok() : fail());
    } else {
      if (fallbackCopy(text)) ok();
      else fail();
    }
  }
  function fallbackCopy(text) {
    try {
      const ta = el("textarea", { style: "position:fixed;opacity:0;top:0;left:0" });
      ta.value = text;
      document.body.append(ta);
      ta.select();
      const ok = document.execCommand("copy");
      ta.remove();
      return ok;
    } catch { return false; }
  }

  // ---- Theme ----
  function applyTheme(theme) {
    document.documentElement.dataset.theme = theme;
    try { localStorage.setItem(THEME_KEY, theme); } catch { /* storage may be blocked */ }
  }
  function initTheme() {
    let theme = "dark";
    try { const saved = localStorage.getItem(THEME_KEY); if (saved === "light" || saved === "dark") theme = saved; } catch { /* ignore */ }
    document.documentElement.dataset.theme = theme;
    dom.themeToggle.addEventListener("click", () => {
      applyTheme(document.documentElement.dataset.theme === "light" ? "dark" : "light");
    });
  }

  // ---- Wiring ----
  function cacheDom() {
    dom.modList = document.querySelector("#modList");
    dom.overviewGrid = document.querySelector("#overviewGrid");
    dom.detail = document.querySelector("#detail");
    dom.result = document.querySelector("#result");
    dom.logPane = document.querySelector("#logPane");
    dom.logFilter = document.querySelector("#logFilter");
    dom.logType = document.querySelector("#logType");
    dom.logSearch = document.querySelector("#logSearch");
    dom.clearLog = document.querySelector("#clearLog");
    dom.exportLog = document.querySelector("#exportLog");
    dom.connIndicator = document.querySelector("#connIndicator");
    dom.connText = document.querySelector("#connText");
    dom.themeToggle = document.querySelector("#themeToggle");
    dom.toasts = document.querySelector("#toasts");
  }

  function buildFilters() {
    dom.logFilter.append(el("option", { value: "all", text: "Mọi module" }));
    for (const id of MODULE_IDS) dom.logFilter.append(el("option", { value: id, text: MODULE_META[id].label }));
    dom.logFilter.addEventListener("change", () => { state.logFilter = dom.logFilter.value; state.dirty = true; render(); });

    for (const type of ["all", "command", "state", "protocol", "transport", "capture"]) {
      dom.logType.append(el("option", { value: type, text: LOG_TYPE_TEXT[type] }));
    }
    dom.logType.addEventListener("change", () => { state.logType = dom.logType.value; state.dirty = true; render(); });

    dom.logSearch.addEventListener("input", () => { state.logSearch = dom.logSearch.value; state.dirty = true; render(); });
  }

  function wireToolbar() {
    dom.clearLog.addEventListener("click", () => {
      state.log = [];
      logEvent(null, "protocol", "Đã xóa log trình duyệt");
      render();
    });
    dom.exportLog.addEventListener("click", () => downloadJson("attak-iot-log.json", buildLogExport()));
  }

  function tick() {
    const staleChanged = refreshStaleness();
    expirePending();
    if (staleChanged || state.dirty) render();
  }

  function init() {
    for (const id of MODULE_IDS) state.modules[id] = makeModuleState();
    cacheDom();
    initTheme();
    buildFilters();
    wireToolbar();
    connect();
    render();
    setInterval(tick, TICK_MS);
  }

  init();
})();
