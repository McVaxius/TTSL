#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import threading
import time
from copy import deepcopy
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def utc_iso(value: datetime) -> str:
    return value.isoformat().replace("+00:00", "Z")


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>TTSL Remote HUD</title>
<style>
:root{--bg:#08111a;--panel:#122131;--panel2:#182b40;--text:#edf6ff;--muted:#9fb4c7;--ok:#79e58d;--warn:#ffb86c;--bad:#ff7f7f;--accent:#8be9fd}
*{box-sizing:border-box}body{margin:0;font-family:"Segoe UI",Tahoma,sans-serif;color:var(--text);background:radial-gradient(circle at top left,rgba(139,233,253,.15),transparent 28%),linear-gradient(180deg,#08111a,#0f1c2b 45%,#122131)}
header{position:sticky;top:0;padding:18px 20px 12px;border-bottom:1px solid rgba(255,255,255,.08);backdrop-filter:blur(10px);background:rgba(8,17,26,.82);z-index:2}
h1{margin:0 0 8px;font-size:27px}.toolbar{display:flex;flex-wrap:wrap;gap:10px 18px;color:var(--muted);font-size:14px}.toolbar label{display:inline-flex;align-items:center;gap:7px}
main{padding:18px 20px 24px;display:grid;gap:16px}.group{background:rgba(18,33,49,.93);border:1px solid rgba(255,255,255,.08);border-radius:18px;overflow:hidden}
.grouphead{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;padding:14px 16px;background:linear-gradient(90deg,rgba(139,233,253,.12),rgba(255,184,108,.09));border-bottom:1px solid rgba(255,255,255,.08)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(340px,1fr));gap:14px;padding:14px}.card{display:grid;gap:11px;padding:14px;border-radius:15px;background:linear-gradient(180deg,rgba(24,43,64,.95),rgba(14,27,42,.98));border:1px solid rgba(255,255,255,.08)}
.head{display:flex;justify-content:space-between;gap:8px}.title{font-weight:700;font-size:17px}.sub,.muted{color:var(--muted);font-size:13px}.badges{display:flex;flex-wrap:wrap;gap:6px;justify-content:flex-end}
.badge{padding:4px 8px;border-radius:999px;font-size:12px;font-weight:700;background:rgba(255,255,255,.08)}.ok{color:var(--ok);background:rgba(121,229,141,.14)}.warn{color:var(--warn);background:rgba(255,184,108,.14)}.bad{color:var(--bad);background:rgba(255,127,127,.14)}
.metrics{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.metric{padding:9px 10px;border-radius:11px;background:rgba(255,255,255,.045)}.label{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.08em;margin-bottom:4px}.value{font-size:15px;font-weight:600}
.bar{margin-top:6px;height:10px;border-radius:999px;overflow:hidden;background:rgba(255,255,255,.08)}.bar>span{display:block;height:100%;background:linear-gradient(90deg,var(--accent),#ffd479)}
.list{display:grid;gap:7px}.row{display:flex;justify-content:space-between;gap:10px;padding:7px 9px;border-radius:10px;background:rgba(255,255,255,.04);font-size:13px}
canvas{width:100%;max-width:220px;aspect-ratio:1/1;justify-self:center;background:rgba(7,12,20,.82);border:1px solid rgba(255,255,255,.08);border-radius:18px}.empty{padding:26px;text-align:center;color:var(--muted);background:rgba(18,33,49,.82);border:1px dashed rgba(255,255,255,.16);border-radius:18px}
@media (max-width:720px){header,main{padding-left:14px;padding-right:14px}.grid{grid-template-columns:1fr;padding:12px}}
</style></head><body>
<header><h1>TTSL Remote HUD</h1><div class="toolbar"><span id="summary">Waiting for clients...</span><span id="stamp">No updates yet.</span><label><input id="krangle" type="checkbox"> Krangle names/account IDs</label><label><input id="showStale" type="checkbox" checked> Show stale/disconnected</label></div></header>
<main id="app"><div class="empty">No clients connected yet. Start the server, point TTSL at it, then enable remote publishing.</div></main>
<script>
const app=document.getElementById("app"),summary=document.getElementById("summary"),stamp=document.getElementById("stamp"),krangle=document.getElementById("krangle"),showStale=document.getElementById("showStale");
const hash=s=>{let h=2166136261;for(let i=0;i<s.length;i++){h^=s.charCodeAt(i);h=Math.imul(h,16777619)}return h>>>0};
const kName=s=>krangle.checked?`Krangle-${hash(s).toString(36).toUpperCase().padStart(6,"0").slice(0,6)}`:s;
const kAcct=s=>krangle.checked?`ACC-${hash(s).toString(16).toUpperCase().padStart(8,"0").slice(0,8)}`:s;
const badge=(text,kind="")=>{const el=document.createElement("span");el.className=`badge ${kind}`.trim();el.textContent=text;return el};
const pct=(cur,max)=>!max||max<=0?0:Math.max(0,Math.min(100,(cur/max)*100));
const posText=p=>!p?"Unavailable":`X ${p.x.toFixed(1)} | Y ${p.y.toFixed(1)} | Z ${p.z.toFixed(1)}`;
function drawRadar(canvas,client){const ctx=canvas.getContext("2d"),w=canvas.width,h=canvas.height,cx=w/2,cy=h/2,r=w/2-18;ctx.clearRect(0,0,w,h);ctx.fillStyle="#08111a";ctx.fillRect(0,0,w,h);ctx.strokeStyle="rgba(255,255,255,.14)";ctx.strokeRect(10,10,w-20,h-20);ctx.beginPath();ctx.moveTo(cx,14);ctx.lineTo(cx,h-14);ctx.moveTo(14,cy);ctx.lineTo(w-14,cy);ctx.stroke();ctx.fillStyle="#79e58d";ctx.beginPath();ctx.arc(cx,cy,5,0,Math.PI*2);ctx.fill();if(!client.position||!Array.isArray(client.party)||client.party.length===0){ctx.fillStyle="#9fb4c7";ctx.font="12px Segoe UI";ctx.fillText("No party positions",48,cy+4);return}for(const m of client.party){if(!m.position)continue;const dx=m.position.x-client.position.x,dz=m.position.z-client.position.z,px=cx+Math.max(-1,Math.min(1,dx/35))*r,py=cy+Math.max(-1,Math.min(1,dz/35))*r;ctx.fillStyle="#ffd479";ctx.beginPath();ctx.arc(px,py,4,0,Math.PI*2);ctx.fill();ctx.fillStyle="#edf6ff";ctx.font="11px Segoe UI";ctx.fillText(String(m.slot),px+7,py+3)}}
function renderClient(c){const card=document.createElement("section");card.className="card";const head=document.createElement("div");head.className="head";const left=document.createElement("div");left.innerHTML=`<div class="title">${kName(c.characterName)} @ ${c.worldName}</div><div class="sub">${c.territoryName||"Unknown zone"} (${c.territoryId??0})</div>`;const badges=document.createElement("div");badges.className="badges";badges.appendChild(badge(c.isDisconnected?"Disconnected":c.stale?"Stale":"Live",c.isDisconnected?"bad":c.stale?"warn":"ok"));badges.appendChild(badge(`${c.ageSeconds.toFixed(1)}s ago`));head.append(left,badges);
const hpCur=c.player?.currentHp??0,hpMax=c.player?.maxHp??0,mpCur=c.player?.currentMp??0,mpMax=c.player?.maxMp??0;
const metrics=document.createElement("div");metrics.className="metrics";metrics.innerHTML=`<div class="metric"><div class="label">Position</div><div class="value">${posText(c.position)}</div></div><div class="metric"><div class="label">Repair</div><div class="value">${c.repair?`${c.repair.minCondition}% min / ${c.repair.averageCondition}% avg`:"Unavailable"}</div></div><div class="metric"><div class="label">HP</div><div class="value">${hpCur.toLocaleString()} / ${hpMax.toLocaleString()}</div><div class="bar"><span style="width:${pct(hpCur,hpMax)}%"></span></div></div><div class="metric"><div class="label">MP</div><div class="value">${mpCur.toLocaleString()} / ${mpMax.toLocaleString()}</div><div class="bar"><span style="width:${pct(mpCur,mpMax)}%"></span></div></div>`;
const conds=document.createElement("div");conds.className="list";for(const [label,active] of [["In combat",c.conditions?.inCombat],["Bound by duty",c.conditions?.boundByDuty],["In queue",c.conditions?.waitingForDuty],["Mounted",c.conditions?.mounted],["Casting",c.conditions?.casting],["Dead",c.conditions?.dead]]){const row=document.createElement("div");row.className="row";row.innerHTML=`<span>${label}</span><span class="${active?"":"muted"}">${active?"active":"inactive"}</span>`;conds.appendChild(row)}
const party=document.createElement("div");party.className="list";if(Array.isArray(c.party)&&c.party.length>0){for(const m of c.party){const d=typeof m.distance==="number"?`${m.distance.toFixed(1)}y`:"off-table";const row=document.createElement("div");row.className="row";row.innerHTML=`<span>[${m.slot}] ${kName(m.name)} <span class="muted">${m.job}</span></span><span>${d}</span>`;party.appendChild(row)}}else{const row=document.createElement("div");row.className="row";row.innerHTML=`<span>No party data captured yet.</span><span class="muted">Waiting</span>`;party.appendChild(row)}
const radar=document.createElement("canvas");radar.width=220;radar.height=220;const foot=document.createElement("div");foot.className="muted";foot.textContent=`Last update: ${c.lastSeenUtc} | Source: ${c.updateKind}`;card.append(head,metrics,conds,party,radar,foot);requestAnimationFrame(()=>drawRadar(radar,c));return card}
function renderGroup(group){const section=document.createElement("section");section.className="group";const live=group.clients.filter(c=>!c.stale&&!c.isDisconnected).length;const head=document.createElement("div");head.className="grouphead";head.innerHTML=`<div><div class="title">${kAcct(group.accountId)}</div><div class="sub">${group.clients.length} client(s) tracked</div></div><div class="sub">${live} live, ${group.clients.length-live} stale/disconnected</div>`;const grid=document.createElement("div");grid.className="grid";const visible=showStale.checked?group.clients:group.clients.filter(c=>!c.stale&&!c.isDisconnected);if(visible.length===0){const empty=document.createElement("div");empty.className="empty";empty.textContent="All tracked clients in this account are stale or disconnected.";grid.appendChild(empty)}else for(const client of visible)grid.appendChild(renderClient(client));section.append(head,grid);return section}
async function refresh(){try{const res=await fetch("/api/state",{cache:"no-store"});if(!res.ok)throw new Error(`HTTP ${res.status}`);const state=await res.json();summary.textContent=`${state.totalClients} client(s) across ${state.accountGroups.length} account group(s)`;stamp.textContent=`Generated ${state.generatedAtUtc} | Stale after ${state.staleSeconds}s`;app.replaceChildren();if(state.accountGroups.length===0){const empty=document.createElement("div");empty.className="empty";empty.textContent="No clients connected yet. Start the server, point TTSL at it, then enable remote publishing.";app.appendChild(empty);return}for(const group of state.accountGroups)app.appendChild(renderGroup(group))}catch(err){summary.textContent="Refresh failed";stamp.textContent=String(err)}}
krangle.addEventListener("change",refresh);showStale.addEventListener("change",refresh);refresh();setInterval(refresh,1000);
</script></body></html>"""


class TTSLStateStore:
    def __init__(self, stale_seconds: int) -> None:
        self.stale_seconds = stale_seconds
        self._clients: dict[tuple[str, str, str], dict] = {}
        self._lock = threading.Lock()

    def update(self, payload: dict) -> None:
        key = self._make_key(payload)
        now = utc_now()
        with self._lock:
            client = self._clients.get(key) or {
                "accountId": key[0],
                "characterName": key[1],
                "worldName": key[2],
            }
            client["updateKind"] = payload.get("updateKind", "full")
            client["lastSeenUtc"] = utc_iso(now)
            client["lastSeenUnix"] = time.time()
            client["isDisconnected"] = False
            client["goodbyeUtc"] = None
            for field in ("territoryId", "territoryName", "position", "player", "conditions", "repair", "party"):
                if field in payload and payload[field] is not None:
                    client[field] = payload[field]
            self._clients[key] = client
            self._prune_locked(now)

    def goodbye(self, payload: dict) -> None:
        key = self._make_key(payload)
        now = utc_now()
        with self._lock:
            client = self._clients.get(key) or {
                "accountId": key[0],
                "characterName": key[1],
                "worldName": key[2],
                "lastSeenUtc": utc_iso(now),
                "lastSeenUnix": time.time(),
            }
            client["updateKind"] = "goodbye"
            client["isDisconnected"] = True
            client["goodbyeUtc"] = utc_iso(now)
            self._clients[key] = client
            self._prune_locked(now)

    def snapshot(self) -> dict:
        now = utc_now()
        with self._lock:
            self._prune_locked(now)
            groups: dict[str, list[dict]] = {}
            now_unix = time.time()
            for client in self._clients.values():
                item = deepcopy(client)
                age_seconds = max(0.0, now_unix - float(item.get("lastSeenUnix", now_unix)))
                item["ageSeconds"] = age_seconds
                item["stale"] = age_seconds >= self.stale_seconds
                item.pop("lastSeenUnix", None)
                groups.setdefault(item["accountId"], []).append(item)
            account_groups = []
            for account_id, clients in groups.items():
                clients.sort(key=lambda item: (item["stale"], item["isDisconnected"], item["characterName"], item["worldName"]))
                account_groups.append({"accountId": account_id, "clients": clients})
            account_groups.sort(key=lambda item: item["accountId"])
            return {
                "generatedAtUtc": utc_iso(now),
                "staleSeconds": self.stale_seconds,
                "totalClients": sum(len(group["clients"]) for group in account_groups),
                "accountGroups": account_groups,
            }

    def _prune_locked(self, now: datetime) -> None:
        cutoff = now.timestamp() - self.stale_seconds
        for key in [key for key, client in self._clients.items() if float(client.get("lastSeenUnix", 0)) < cutoff]:
            self._clients.pop(key, None)

    @staticmethod
    def _make_key(payload: dict) -> tuple[str, str, str]:
        account_id = str(payload.get("accountId", "")).strip()
        character_name = str(payload.get("characterName", "")).strip()
        world_name = str(payload.get("worldName", "")).strip()
        if not account_id or not character_name or not world_name:
            raise ValueError("accountId, characterName, and worldName are required")
        return account_id, character_name, world_name


def make_handler(state: TTSLStateStore):
    class Handler(BaseHTTPRequestHandler):
        server_version = "TTSLHTTP/0.1"

        def do_GET(self) -> None:  # noqa: N802
            if self.path == "/" or self.path.startswith("/?"):
                body = PAGE.encode("utf-8")
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if self.path.startswith("/api/state"):
                body = json.dumps(state.snapshot(), ensure_ascii=False).encode("utf-8")
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Cache-Control", "no-store")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            self.send_error(HTTPStatus.NOT_FOUND, "Unknown path")

        def do_POST(self) -> None:  # noqa: N802
            try:
                payload = self._read_json()
                if self.path == "/api/update":
                    state.update(payload)
                    return self._write_json({"ok": True})
                if self.path == "/api/goodbye":
                    state.goodbye(payload)
                    return self._write_json({"ok": True})
                self.send_error(HTTPStatus.NOT_FOUND, "Unknown path")
            except ValueError as exc:
                self._write_json({"ok": False, "error": str(exc)}, HTTPStatus.BAD_REQUEST)
            except json.JSONDecodeError as exc:
                self._write_json({"ok": False, "error": f"Invalid JSON: {exc}"}, HTTPStatus.BAD_REQUEST)

        def log_message(self, format: str, *args) -> None:
            print(f"[{datetime.now():%Y-%m-%d %H:%M:%S}] {self.address_string()} {format % args}")

        def _read_json(self) -> dict:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0:
                raise ValueError("Request body is required")
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(payload, dict):
                raise ValueError("JSON object body is required")
            return payload

        def _write_json(self, payload: dict, status: HTTPStatus = HTTPStatus.OK) -> None:
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    return Handler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Serve a multi-client TTSL remote HUD.")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host. Use 0.0.0.0 for LAN access.")
    parser.add_argument("--port", type=int, default=69420, help="HTTP port for clients and viewers.")
    parser.add_argument("--stale-seconds", type=int, default=300, help="How long stale clients remain visible.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    state = TTSLStateStore(stale_seconds=max(30, args.stale_seconds))
    server = ThreadingHTTPServer((args.host, args.port), make_handler(state))
    print(f"TTSL remote HUD listening on http://{args.host}:{args.port} (stale prune {state.stale_seconds}s)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping TTSL remote HUD server.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
