#!/usr/bin/env bash
#
# Check (and optionally fix) the host firewall for LaserTag telemetry (UDP 4210).
# The host UDP listener needs inbound UDP 4210 to receive device heartbeats and
# EVT telemetry. A missing rule is one common cause of "REST fine, no heartbeats"
# (a lossy Wi-Fi link at low RSSI is the other); this script rules out the firewall.
#
#   tools/setup-firewall.sh            # check, and offer to fix (uses sudo)
#   tools/setup-firewall.sh --check    # diagnose only (exit 0 = ok, 1 = missing)
#   tools/setup-firewall.sh --remove   # undo
#
# Linux: uses ufw or firewalld (via sudo). macOS: the Application Firewall is
# app-based and usually does not block inbound UDP by port — verify + advise.

set -uo pipefail
PORT=4210
MODE="${1:-}"

linux_check() {  # 0 = allowed, 1 = not allowed, 2 = no known fw manager
  if command -v ufw >/dev/null 2>&1; then
    ufw status 2>/dev/null | grep -qiE "(^|[[:space:]])${PORT}/udp([[:space:]]|$).*ALLOW" && return 0 || return 1
  elif command -v firewall-cmd >/dev/null 2>&1; then
    firewall-cmd --query-port=${PORT}/udp >/dev/null 2>&1 && return 0 || return 1
  fi
  return 2
}

linux_apply() {
  if command -v ufw >/dev/null 2>&1; then
    sudo ufw allow ${PORT}/udp
  elif command -v firewall-cmd >/dev/null 2>&1; then
    sudo firewall-cmd --add-port=${PORT}/udp --permanent && sudo firewall-cmd --reload
  else
    echo "No ufw/firewalld found. If inbound UDP ${PORT} is blocked, allow it manually, e.g.:"
    echo "  sudo iptables -A INPUT -p udp --dport ${PORT} -j ACCEPT"
    return 1
  fi
}

linux_remove() {
  if command -v ufw >/dev/null 2>&1; then
    sudo ufw delete allow ${PORT}/udp || true
  elif command -v firewall-cmd >/dev/null 2>&1; then
    sudo firewall-cmd --remove-port=${PORT}/udp --permanent && sudo firewall-cmd --reload || true
  fi
}

os="$(uname -s)"
case "$os" in
  Linux)
    case "$MODE" in
      --check)
        if linux_check; then echo "[ok] UDP ${PORT} inbound allowed."; exit 0
        else echo "[missing] UDP ${PORT} inbound not allowed."; exit 1; fi ;;
      --remove) linux_remove; echo "[removed]" ;;
      --apply)  linux_apply  && echo "[applied]" ;;
      *)
        if linux_check; then echo "[ok] UDP ${PORT} already allowed. Nothing to do."; exit 0; fi
        rc=$?
        [ "$rc" = "2" ] && echo "(no ufw/firewalld detected)"
        echo "UDP ${PORT} inbound is not allowed."
        read -r -p "Add the rule now (uses sudo)? [y/N] " ans
        case "$ans" in
          y|Y|yes|YES) linux_apply && echo "[applied]" ;;
          *) echo "Skipped." ;;
        esac ;;
    esac ;;
  Darwin)
    echo "macOS: the Application Firewall is app-based and usually does NOT block inbound UDP by port."
    echo "If the listener still receives no heartbeats, allow the 'dotnet' binary under:"
    echo "  System Settings > Network > Firewall > Options…"
    echo "(Otherwise no change is needed for UDP ${PORT}.)"
    exit 0 ;;
  *)
    echo "Unsupported OS: ${os}. On Windows use tools/setup-firewall.ps1."
    exit 1 ;;
esac
