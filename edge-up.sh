#!/usr/bin/env bash
# Start Windows Edge with CDP on :9222 and a loopback->0.0.0.0 relay on :9223 (WSL NAT cannot reach Windows 127.0.0.1).
GW=$(ip route | awk '/default/{print $3}')
if ! curl -s -m 2 http://$GW:9223/json/version >/dev/null; then
  "/mnt/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" --remote-debugging-port=9222 \
    --user-data-dir='C:\Users\Duck\jev-profile' --no-first-run about:blank >/dev/null 2>&1 &
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\Users\Duck\jev-relay\relay.ps1' >/dev/null 2>&1 &
  sleep 5
fi
curl -s -m 3 http://$GW:9223/json/version | grep -E '"Browser"|webSocketDebuggerUrl'
