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


def log_event(message: str) -> None:
    print(f"[{datetime.now():%Y-%m-%d %H:%M:%S}] {message}")


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>TTSL Remote HUD</title>
<style>
:root{--bg:#071018;--panel:#101925;--panel2:#152231;--line:rgba(255,255,255,.08);--text:#eaf4ff;--muted:#93a7bc;--ok:#79e58d;--warn:#ffbf74;--bad:#ff7f7f;--accent:#87d7ff}
*{box-sizing:border-box}body{margin:0;font-family:"Segoe UI",Tahoma,sans-serif;color:var(--text);background:radial-gradient(circle at top left,rgba(135,215,255,.12),transparent 26%),linear-gradient(180deg,#071018,#0b1621 48%,#101925)}
header{position:sticky;top:0;padding:8px 10px 6px;border-bottom:1px solid var(--line);background:rgba(7,16,24,.9);backdrop-filter:blur(10px);z-index:2}
h1{margin:0 0 4px;font-size:18px}.toolbar{display:flex;flex-wrap:wrap;gap:6px 10px;color:var(--muted);font-size:11px}.toolbar label{display:inline-flex;align-items:center;gap:5px}
main{padding:8px 10px 10px;display:grid;grid-template-columns:repeat(auto-fit,minmax(235px,1fr));gap:8px;align-items:start}
.card{display:grid;gap:6px;padding:8px;border-radius:11px;background:linear-gradient(180deg,rgba(16,25,37,.96),rgba(11,18,28,.98));border:1px solid var(--line)}
.head{display:flex;justify-content:space-between;gap:6px;align-items:flex-start}.name{font-weight:700;font-size:14px;line-height:1.15}.zone,.sub,.foot{font-size:10px;color:var(--muted)}
.badges,.states{display:flex;flex-wrap:wrap;gap:5px}.badge,.state{padding:3px 7px;border-radius:999px;font-size:11px;font-weight:700;border:1px solid transparent}
.badge.ok,.state.on{color:var(--ok);background:rgba(121,229,141,.14);border-color:rgba(121,229,141,.22)}
.badge.warn,.state.warn{color:var(--warn);background:rgba(255,191,116,.12);border-color:rgba(255,191,116,.22)}
.badge.bad,.state.bad{color:var(--bad);background:rgba(255,127,127,.12);border-color:rgba(255,127,127,.22)}
.state.off{color:#627385;background:rgba(255,255,255,.04);border-color:rgba(255,255,255,.06)}
.meta{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:4px}.tile{padding:4px 5px;border-radius:8px;background:rgba(255,255,255,.04)}
.label{font-size:9px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:2px}.value{font-size:11px;font-weight:600;line-height:1.2}
.section{display:grid;gap:4px}.sectionhead{font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}
.party{display:grid;gap:3px}.member{display:grid;grid-template-columns:20px minmax(0,1fr) 36px 40px;gap:4px;align-items:center;padding:3px 5px;border-radius:7px;background:rgba(255,255,255,.035);font-size:11px}
.slot,.job,.hp,.dist{text-align:right;color:var(--muted)}.membername{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.radarbox{display:grid;justify-items:center;gap:3px}canvas{width:124px;max-width:100%;aspect-ratio:1/1;background:rgba(6,10,16,.92);border:1px solid var(--line);border-radius:12px}
.empty{padding:20px;text-align:center;color:var(--muted);background:rgba(16,25,37,.84);border:1px dashed rgba(255,255,255,.14);border-radius:12px}
@media (max-width:720px){header{padding:8px 10px 6px}main{padding:8px 10px 10px;grid-template-columns:1fr}}
</style></head><body>
<header><h1>TTSL Remote HUD</h1><div class="toolbar"><span id="summary">Waiting for clients...</span><span id="stamp">No updates yet.</span><label><input id="krangle" type="checkbox"> Krangle names/account IDs</label><label><input id="showStale" type="checkbox" checked> Show stale/disconnected</label></div></header>
<main id="app"><div class="empty">No clients connected yet. Start the server, point TTSL at it, then enable remote publishing.</div></main>
<script>
const app=document.getElementById("app"),summary=document.getElementById("summary"),stamp=document.getElementById("stamp"),krangle=document.getElementById("krangle"),showStale=document.getElementById("showStale");
const hash=s=>{let h=2166136261;for(let i=0;i<s.length;i++){h^=s.charCodeAt(i);h=Math.imul(h,16777619)}return h>>>0};
const kName=s=>krangle.checked?`Krangle-${hash(s).toString(36).toUpperCase().padStart(6,"0").slice(0,6)}`:s;
const kAcct=s=>krangle.checked?`ACC-${hash(s).toString(16).toUpperCase().padStart(8,"0").slice(0,8)}`:s;
const pct=(cur,max)=>!max||max<=0?"--":`${Math.round((cur/max)*100)}%`;
const posText=p=>!p?"Unavailable":`X ${p.x.toFixed(1)} | Y ${p.y.toFixed(1)} | Z ${p.z.toFixed(1)}`;
function chip(text,kind){const el=document.createElement("span");el.className=`badge ${kind}`.trim();el.textContent=text;return el}
function stateChip(text,active,kind=""){const el=document.createElement("span");el.className=`state ${kind || (active?"on":"off")}`.trim();el.textContent=text;return el}
function tile(label,value){const el=document.createElement("div");el.className="tile";el.innerHTML=`<div class="label">${label}</div><div class="value">${value}</div>`;return el}
function drawRadar(canvas,client){const ctx=canvas.getContext("2d"),w=canvas.width,h=canvas.height,cx=w/2,cy=h/2,r=w/2-16;ctx.clearRect(0,0,w,h);ctx.fillStyle="#071018";ctx.fillRect(0,0,w,h);ctx.strokeStyle="rgba(255,255,255,.12)";ctx.strokeRect(9,9,w-18,h-18);ctx.beginPath();ctx.moveTo(cx,14);ctx.lineTo(cx,h-14);ctx.moveTo(14,cy);ctx.lineTo(w-14,cy);ctx.stroke();ctx.fillStyle="#79e58d";ctx.beginPath();ctx.arc(cx,cy,4,0,Math.PI*2);ctx.fill();if(!client.position||!Array.isArray(client.party)||client.party.length===0){ctx.fillStyle="#93a7bc";ctx.font="11px Segoe UI";ctx.fillText("No party",46,cy+4);return}for(const m of client.party){if(!m.position)continue;const dx=m.position.x-client.position.x,dz=m.position.z-client.position.z,px=cx+Math.max(-1,Math.min(1,dx/35))*r,py=cy+Math.max(-1,Math.min(1,dz/35))*r;ctx.fillStyle="#ffbf74";ctx.beginPath();ctx.arc(px,py,3.5,0,Math.PI*2);ctx.fill();ctx.fillStyle="#eaf4ff";ctx.font="10px Segoe UI";ctx.fillText(String(m.slot),px+5,py+3)}}
function renderParty(client){const wrap=document.createElement("div");wrap.className="party";if(Array.isArray(client.party)&&client.party.length>0){for(const m of client.party){const row=document.createElement("div");row.className="member";const dist=typeof m.distance==="number"?`${m.distance.toFixed(1)}y`:"--";const hp=(m.currentHp!=null&&m.maxHp!=null&&m.maxHp>0)?pct(m.currentHp,m.maxHp):"--";row.innerHTML=`<div class="slot">${m.slot}</div><div class="membername">${kName(m.name)}</div><div class="job">${m.job}</div><div class="dist">${dist}</div>`;row.title=`HP ${hp}`;wrap.appendChild(row)}}else{const row=document.createElement("div");row.className="member";row.innerHTML=`<div class="slot">-</div><div class="membername">No party data captured yet.</div><div class="job">--</div><div class="dist">--</div>`;wrap.appendChild(row)}return wrap}
function renderStates(client){const wrap=document.createElement("div");wrap.className="states";wrap.append(stateChip("Combat",!!client.conditions?.inCombat),stateChip("Duty",!!client.conditions?.boundByDuty),stateChip("Queue",!!client.conditions?.waitingForDuty),stateChip("Mount",!!client.conditions?.mounted),stateChip("Cast",!!client.conditions?.casting),stateChip("Dead",!!client.conditions?.dead,client.conditions?.dead?"bad":"off"));return wrap}
function renderClient(client){const card=document.createElement("section");card.className="card";const head=document.createElement("div");head.className="head";const info=document.createElement("div");info.innerHTML=`<div class="name">${kName(client.characterName)} @ ${client.worldName}</div><div class="zone">${client.territoryName||"Unknown zone"} (${client.territoryId??0})</div><div class="sub">${kAcct(client.accountId)}</div>`;const badges=document.createElement("div");badges.className="badges";badges.appendChild(chip(client.isDisconnected?"Disconnected":client.stale?"Stale":"Live",client.isDisconnected?"bad":client.stale?"warn":"ok"));badges.appendChild(chip(`${client.ageSeconds.toFixed(1)}s`,""));const metrics=document.createElement("div");metrics.className="meta";const hpCur=client.player?.currentHp??0,hpMax=client.player?.maxHp??0,mpCur=client.player?.currentMp??0,mpMax=client.player?.maxMp??0;metrics.append(tile("HP",`${hpCur.toLocaleString()} / ${hpMax.toLocaleString()} (${pct(hpCur,hpMax)})`),tile("MP",`${mpCur.toLocaleString()} / ${mpMax.toLocaleString()} (${pct(mpCur,mpMax)})`),tile("Position",posText(client.position)),tile("Repair",client.repair?`${client.repair.minCondition}% min | ${client.repair.averageCondition}% avg`:"Unavailable"));const stateSection=document.createElement("div");stateSection.className="section";stateSection.innerHTML=`<div class="sectionhead">Status</div>`;stateSection.appendChild(renderStates(client));const partySection=document.createElement("div");partySection.className="section";partySection.innerHTML=`<div class="sectionhead">Party</div>`;partySection.appendChild(renderParty(client));const radarSection=document.createElement("div");radarSection.className="radarbox";radarSection.innerHTML=`<div class="sectionhead">Radar</div>`;const radar=document.createElement("canvas");radar.width=140;radar.height=140;radarSection.appendChild(radar);const foot=document.createElement("div");foot.className="foot";foot.textContent=`Last update ${client.lastSeenUtc} | ${client.updateKind}`;head.append(info,badges);card.append(head,metrics,stateSection,partySection,radarSection,foot);requestAnimationFrame(()=>drawRadar(radar,client));return card}
function flattenGroups(groups){return groups.flatMap(group=>group.clients.map(client=>({...client,accountId:group.accountId})))}
async function refresh(){try{const res=await fetch("/api/state",{cache:"no-store"});if(!res.ok)throw new Error(`HTTP ${res.status}`);const state=await res.json();const clients=flattenGroups(state.accountGroups).sort((a,b)=>Number(a.stale||a.isDisconnected)-Number(b.stale||b.isDisconnected)||String(a.characterName).localeCompare(String(b.characterName))||String(a.worldName).localeCompare(String(b.worldName)));const visible=showStale.checked?clients:clients.filter(c=>!c.stale&&!c.isDisconnected);const live=clients.filter(c=>!c.stale&&!c.isDisconnected).length;summary.textContent=`${clients.length} client(s) tracked | ${live} live | ${clients.length-live} stale/disconnected`;stamp.textContent=`Generated ${state.generatedAtUtc} | stale after ${state.staleSeconds}s`;app.replaceChildren();if(visible.length===0){const empty=document.createElement("div");empty.className="empty";empty.textContent=clients.length===0?"No clients connected yet. Start the server, point TTSL at it, then enable remote publishing.":"All tracked clients are stale or disconnected.";app.appendChild(empty);return}for(const client of visible)app.appendChild(renderClient(client))}catch(err){summary.textContent="Refresh failed";stamp.textContent=String(err)}}
krangle.addEventListener("change",refresh);showStale.addEventListener("change",refresh);refresh();setInterval(refresh,1000);
</script></body></html>"""


class TTSLStateStore:
    def __init__(self, stale_seconds: int) -> None:
        self.stale_seconds = stale_seconds
        self.retention_seconds = max(stale_seconds * 2, stale_seconds + 60)
        self._clients: dict[tuple[str, str, str], dict] = {}
        self._lock = threading.Lock()

    def update(self, payload: dict) -> None:
        key = self._make_key(payload)
        now = utc_now()
        now_unix = time.time()
        with self._lock:
            previous = self._clients.get(key)
            was_disconnected = bool(previous and previous.get("isDisconnected"))
            was_stale = bool(previous and now_unix - float(previous.get("lastSeenUnix", now_unix)) >= self.stale_seconds)

            client = previous or {
                "accountId": key[0],
                "characterName": key[1],
                "worldName": key[2],
            }
            client["updateKind"] = payload.get("updateKind", "full")
            client["lastSeenUtc"] = utc_iso(now)
            client["lastSeenUnix"] = now_unix
            client["isDisconnected"] = False
            client["goodbyeUtc"] = None
            for field in ("territoryId", "territoryName", "position", "player", "conditions", "repair", "party"):
                if field in payload and payload[field] is not None:
                    client[field] = payload[field]
            self._clients[key] = client

            if previous is None:
                log_event(f"Client connected: {self._format_key(key)}")
            elif was_disconnected or was_stale:
                log_event(f"Client resumed: {self._format_key(key)}")

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
            already_disconnected = bool(client.get("isDisconnected"))
            client["updateKind"] = "goodbye"
            client["isDisconnected"] = True
            client["goodbyeUtc"] = utc_iso(now)
            self._clients[key] = client

            if not already_disconnected:
                log_event(f"Client goodbye: {self._format_key(key)}")

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
        cutoff = now.timestamp() - self.retention_seconds
        stale_keys = [key for key, client in self._clients.items() if float(client.get("lastSeenUnix", 0)) < cutoff]
        for key in stale_keys:
            self._clients.pop(key, None)
            log_event(f"Client removed after inactivity: {self._format_key(key)}")

    @staticmethod
    def _make_key(payload: dict) -> tuple[str, str, str]:
        account_id = str(payload.get("accountId", "")).strip()
        character_name = str(payload.get("characterName", "")).strip()
        world_name = str(payload.get("worldName", "")).strip()
        if not account_id or not character_name or not world_name:
            raise ValueError("accountId, characterName, and worldName are required")
        return account_id, character_name, world_name

    @staticmethod
    def _format_key(key: tuple[str, str, str]) -> str:
        return f"{key[1]}@{key[2]} ({key[0]})"


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
                log_event(f"Bad request on {self.path}: {exc}")
                self._write_json({"ok": False, "error": str(exc)}, HTTPStatus.BAD_REQUEST)
            except json.JSONDecodeError as exc:
                log_event(f"Invalid JSON on {self.path}: {exc}")
                self._write_json({"ok": False, "error": f"Invalid JSON: {exc}"}, HTTPStatus.BAD_REQUEST)

        def log_message(self, format: str, *args) -> None:
            return

        def _read_json(self) -> dict:
            transfer_encoding = self.headers.get("Transfer-Encoding", "")
            if "chunked" in transfer_encoding.lower():
                body = self._read_chunked_body()
            else:
                length = int(self.headers.get("Content-Length", "0"))
                if length <= 0:
                    raise ValueError("Request body is required")
                body = self.rfile.read(length)

            payload = json.loads(body.decode("utf-8"))
            if not isinstance(payload, dict):
                raise ValueError("JSON object body is required")
            return payload

        def _read_chunked_body(self) -> bytes:
            body = bytearray()
            while True:
                size_line = self.rfile.readline().strip()
                if not size_line:
                    continue

                chunk_size = int(size_line.split(b";", 1)[0], 16)
                if chunk_size == 0:
                    while True:
                        trailer_line = self.rfile.readline()
                        if trailer_line in (b"\r\n", b"\n", b""):
                            return bytes(body)

                body.extend(self.rfile.read(chunk_size))
                terminator = self.rfile.read(2)
                if terminator != b"\r\n":
                    raise ValueError("Invalid chunk framing in request body")

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
    parser.add_argument("--port", type=int, default=6942, help="HTTP port for clients and viewers.")
    parser.add_argument("--stale-seconds", type=int, default=300, help="How long stale clients remain visible.")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    state = TTSLStateStore(stale_seconds=max(30, args.stale_seconds))
    server = ThreadingHTTPServer((args.host, args.port), make_handler(state))
    log_event(f"TTSL remote HUD listening on http://{args.host}:{args.port} (stale {state.stale_seconds}s, prune {state.retention_seconds}s)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping TTSL remote HUD server.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
