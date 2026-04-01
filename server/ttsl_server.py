#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib
import json
import mimetypes
import os
import socket
import subprocess
import sys
import threading
import time
from copy import deepcopy
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlparse


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def utc_iso(value: datetime) -> str:
    return value.isoformat().replace("+00:00", "Z")


def log_event(message: str) -> None:
    print(f"[{datetime.now():%Y-%m-%d %H:%M:%S}] {message}")


SERVER_ROOT = os.path.dirname(os.path.abspath(__file__))
EXTRACT_SCRIPT_PATH = os.path.join(SERVER_ROOT, "extract_ttsl_assets.py")
EXTRACT_OUTPUT_ROOT = os.path.join(SERVER_ROOT, "extracted")
EXTRACT_SUMMARY_PATH = os.path.join(EXTRACT_OUTPUT_ROOT, "ttsl_asset_extract_summary.json")
CACHE_ROOT = os.path.join(SERVER_ROOT, "cache")
LOCAL_PYTHON_DEPS_ROOT = os.path.join(SERVER_ROOT, "_pydeps")
TEX_HEADER_SIZE = 80
TEX_FORMAT_A8R8G8B8 = 5200
_PILLOW_IMPORT_RESULT: tuple[object | None, str] | None = None


def ensure_local_dependency_root() -> None:
    if LOCAL_PYTHON_DEPS_ROOT not in sys.path:
        sys.path.insert(0, LOCAL_PYTHON_DEPS_ROOT)


def ensure_pillow_dependency() -> tuple[object | None, str]:
    global _PILLOW_IMPORT_RESULT
    if _PILLOW_IMPORT_RESULT is not None:
        return _PILLOW_IMPORT_RESULT

    ensure_local_dependency_root()
    importlib.invalidate_caches()

    try:
        from PIL import Image  # type: ignore

        _PILLOW_IMPORT_RESULT = (Image, "")
        return _PILLOW_IMPORT_RESULT
    except ModuleNotFoundError:
        pass

    os.makedirs(LOCAL_PYTHON_DEPS_ROOT, exist_ok=True)
    install_command = [
        sys.executable,
        "-m",
        "pip",
        "install",
        "--disable-pip-version-check",
        "--no-input",
        "--target",
        LOCAL_PYTHON_DEPS_ROOT,
        "Pillow>=11,<12",
    ]
    result = subprocess.run(
        install_command,
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )

    importlib.invalidate_caches()
    ensure_local_dependency_root()

    try:
        from PIL import Image  # type: ignore

        _PILLOW_IMPORT_RESULT = (
            Image,
            "Pillow was missing from this Python environment and was installed into TTSL's local _pydeps cache.",
        )
        return _PILLOW_IMPORT_RESULT
    except ModuleNotFoundError:
        stdout = (result.stdout or "").strip()
        stderr = (result.stderr or "").strip()
        output = " | ".join(bit for bit in (stdout, stderr) if bit)
        message = (
            "Pillow is required to render extracted TEX assets for the web HUD."
            if not output
            else f"Pillow auto-install failed: {output}"
        )
        _PILLOW_IMPORT_RESULT = (None, message)
        return _PILLOW_IMPORT_RESULT


def read_tex_header(raw_data: bytes) -> tuple[int, int, int]:
    if len(raw_data) < TEX_HEADER_SIZE:
        raise ValueError("TEX file is smaller than the expected 80-byte header.")

    format_code = int.from_bytes(raw_data[4:8], byteorder="little", signed=False)
    width = int.from_bytes(raw_data[8:10], byteorder="little", signed=False)
    height = int.from_bytes(raw_data[10:12], byteorder="little", signed=False)
    if width <= 0 or height <= 0:
        raise ValueError("TEX file reported invalid dimensions.")
    return format_code, width, height


def convert_tex_to_png(raw_tex_path: str, png_path: str) -> None:
    image_type, error = ensure_pillow_dependency()
    if image_type is None:
        raise RuntimeError(error or "Pillow is not available for TEX conversion.")

    with open(raw_tex_path, "rb") as handle:
        raw_data = handle.read()

    format_code, width, height = read_tex_header(raw_data)
    if format_code != TEX_FORMAT_A8R8G8B8:
        raise ValueError(f"Unsupported TEX image format {format_code} in {raw_tex_path}.")

    pixel_data = raw_data[TEX_HEADER_SIZE:]
    required_bytes = width * height * 4
    if len(pixel_data) < required_bytes:
        raise ValueError(
            f"TEX pixel payload is truncated: expected {required_bytes} bytes, got {len(pixel_data)}."
        )

    image = image_type.frombytes("RGBA", (width, height), pixel_data[:required_bytes], "raw", "BGRA")
    os.makedirs(os.path.dirname(png_path), exist_ok=True)
    image.save(png_path, format="PNG")


def ensure_png_cache(raw_tex_path: str, cache_relative_path: str) -> str:
    cache_path = os.path.normpath(os.path.join(CACHE_ROOT, cache_relative_path.replace("/", os.sep)))
    cache_root = os.path.normpath(CACHE_ROOT)
    if os.path.commonpath([cache_root, cache_path]) != cache_root:
        raise ValueError(f"Refusing to write cache asset outside {cache_root}: {cache_relative_path}")

    if not os.path.isfile(cache_path) or os.path.getmtime(cache_path) < os.path.getmtime(raw_tex_path):
        convert_tex_to_png(raw_tex_path, cache_path)

    return cache_path


def build_cache_url(cache_path: str) -> str:
    relative_path = os.path.relpath(cache_path, CACHE_ROOT).replace(os.sep, "/")
    version = int(os.path.getmtime(cache_path))
    return f"/assets/{relative_path}?v={version}"


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>TTSL Remote HUD</title>
<style>
:root{--bg:#071018;--panel:#101925;--panel2:#152231;--line:rgba(255,255,255,.08);--text:#eaf4ff;--muted:#93a7bc;--ok:#79e58d;--warn:#ffbf74;--bad:#ff7f7f;--accent:#87d7ff;--tank:#78c5ff;--heal:#93f2a5;--dps:#ff9b7a;--util:#d5b7ff}
*{box-sizing:border-box}body{margin:0;font-family:"Segoe UI",Tahoma,sans-serif;color:var(--text);background:radial-gradient(circle at top left,rgba(135,215,255,.12),transparent 26%),linear-gradient(180deg,#071018,#0b1621 48%,#101925)}
header{position:sticky;top:0;padding:8px 10px 6px;border-bottom:1px solid var(--line);background:rgba(7,16,24,.9);backdrop-filter:blur(10px);z-index:2}
h1{margin:0 0 4px;font-size:18px}.toolbar{display:flex;flex-wrap:wrap;gap:6px 10px;color:var(--muted);font-size:11px}.toolbar label{display:inline-flex;align-items:center;gap:5px}.toolbar button{padding:3px 8px;border-radius:999px;border:1px solid rgba(255,255,255,.14);background:rgba(135,215,255,.12);color:var(--text);font:inherit;cursor:pointer}.toolbar button:disabled{opacity:.45;cursor:not-allowed}.toolbar input[type="number"]{width:64px;padding:2px 6px;border-radius:999px;border:1px solid rgba(255,255,255,.14);background:rgba(255,255,255,.05);color:var(--text);font:inherit}
main{padding:8px 10px 10px;display:grid;grid-template-columns:repeat(auto-fit,minmax(235px,1fr));gap:8px;align-items:start}
.card{display:grid;gap:6px;padding:8px;border-radius:11px;background:linear-gradient(180deg,rgba(16,25,37,.96),rgba(11,18,28,.98));border:1px solid var(--line)}
.head{display:flex;justify-content:space-between;gap:6px;align-items:flex-start}.name{font-weight:700;font-size:14px;line-height:1.15}.zone,.sub,.foot{font-size:10px;color:var(--muted)}
.badges,.states,.ident{display:flex;flex-wrap:wrap;gap:5px}.ident{align-items:center}.badge,.state{padding:3px 7px;border-radius:999px;font-size:11px;font-weight:700;border:1px solid transparent}
.badge.ok,.state.on{color:var(--ok);background:rgba(121,229,141,.14);border-color:rgba(121,229,141,.22)}
.badge.warn,.state.warn{color:var(--warn);background:rgba(255,191,116,.12);border-color:rgba(255,191,116,.22)}
.badge.bad,.state.bad{color:var(--bad);background:rgba(255,127,127,.12);border-color:rgba(255,127,127,.22)}
.badge.tank{color:var(--tank);background:rgba(120,197,255,.12);border-color:rgba(120,197,255,.22)}
.badge.heal{color:var(--heal);background:rgba(147,242,165,.12);border-color:rgba(147,242,165,.22)}
.badge.dps{color:var(--dps);background:rgba(255,155,122,.12);border-color:rgba(255,155,122,.22)}
.badge.util{color:var(--util);background:rgba(213,183,255,.12);border-color:rgba(213,183,255,.22)}
.state.off{color:#627385;background:rgba(255,255,255,.04);border-color:rgba(255,255,255,.06)}
.meta{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:4px}.tile{padding:4px 5px;border-radius:8px;background:rgba(255,255,255,.04)}
.label{font-size:9px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:2px}.value{font-size:11px;font-weight:600;line-height:1.2}.value.bad{color:var(--bad)}
.section{display:grid;gap:4px}.sectionhead{font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}
.party{display:grid;gap:3px}.member{display:grid;grid-template-columns:20px minmax(0,1fr) 36px 40px;gap:4px;align-items:center;padding:3px 5px;border-radius:7px;background:rgba(255,255,255,.035);font-size:11px}
.slot,.job,.hp,.dist{text-align:right;color:var(--muted)}.membername{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.radarbox{display:grid;justify-items:center;gap:3px}canvas{display:block;max-width:100%;aspect-ratio:1/1;background:rgba(6,10,16,.92);border:1px solid var(--line);border-radius:12px}
.iconimg{width:18px;height:18px;border-radius:4px;border:1px solid var(--line);background:rgba(255,255,255,.04);object-fit:cover}
.mapframe{position:relative;max-width:100%;aspect-ratio:1/1;overflow:hidden;border-radius:12px;border:1px solid var(--line);background:rgba(6,10,16,.92)}
.mapimg{position:absolute;display:block;max-width:none;max-height:none}
.mapdot{position:absolute;width:10px;height:10px;border-radius:999px;background:var(--ok);border:2px solid rgba(7,16,24,.95);transform:translate(-50%,-50%);box-shadow:0 0 0 1px rgba(121,229,141,.24)}
.aggmembers{display:grid;gap:4px}.aggmember{display:grid;gap:4px;padding:5px 6px;border-radius:8px;background:rgba(255,255,255,.035);border:1px solid rgba(255,255,255,.04)}.aggmember.stranger{border-color:rgba(255,127,127,.18)}
.aggmain{display:flex;justify-content:space-between;gap:6px;align-items:flex-start;flex-wrap:wrap}.aggname{display:flex;align-items:center;gap:5px;min-width:0;flex-wrap:wrap}
.aggname .slot,.aggname .job,.aggname .lvl{color:var(--muted);font-size:10px;font-weight:700}.aggname .membername{font-size:12px;font-weight:700;line-height:1.1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:220px}
.aggmeta{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:4px}.aggnote{font-size:10px;color:var(--muted)}.aggnote.bad{color:var(--bad)}
.empty{padding:20px;text-align:center;color:var(--muted);background:rgba(16,25,37,.84);border:1px dashed rgba(255,255,255,.14);border-radius:12px}
@media (max-width:900px){.aggmeta{grid-template-columns:repeat(2,minmax(0,1fr))}.aggname .membername{max-width:none}}
@media (max-width:720px){header{padding:8px 10px 6px}main{padding:8px 10px 10px;grid-template-columns:1fr}}
</style></head><body>
<header><h1>TTSL Remote HUD</h1><div class="toolbar"><span id="summary">Waiting for clients...</span><span id="stamp">No updates yet.</span><span id="assetPlan">Asset plan pending.</span><span id="extractStatus">Extraction idle.</span><button id="extractAssets" type="button">Extract Assets</button><label><input id="krangle" type="checkbox"> Krangle names/account IDs</label><label><input id="krangleEnemies" type="checkbox"> Krangle enemy names</label><label><input id="showStale" type="checkbox" checked> Show stale/disconnected</label><label><input id="aggregateParties" type="checkbox"> Aggregate parties</label><label><input id="icons" type="checkbox" checked> Icons</label><label><input id="enumerate" type="checkbox"> Enumerate</label><label>Box px <input id="mapBoxPx" type="number" min="96" max="320" step="4" value="160"></label><label>Combat W <input id="combatWidth" type="number" min="5" max="300" step="1" value="20"></label><label>Combat H <input id="combatHeight" type="number" min="5" max="300" step="1" value="20"></label><label>Travel W <input id="travelWidth" type="number" min="5" max="500" step="1" value="50"></label><label>Travel H <input id="travelHeight" type="number" min="5" max="500" step="1" value="50"></label></div></header>
<main id="app"><div class="empty">No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.</div></main>
<script>
const app=document.getElementById("app"),summary=document.getElementById("summary"),stamp=document.getElementById("stamp"),assetPlan=document.getElementById("assetPlan"),extractStatus=document.getElementById("extractStatus"),extractAssets=document.getElementById("extractAssets"),krangle=document.getElementById("krangle"),krangleEnemies=document.getElementById("krangleEnemies"),showStale=document.getElementById("showStale"),aggregateParties=document.getElementById("aggregateParties"),icons=document.getElementById("icons"),enumerate=document.getElementById("enumerate"),mapBoxPxInput=document.getElementById("mapBoxPx"),combatWidthInput=document.getElementById("combatWidth"),combatHeightInput=document.getElementById("combatHeight"),travelWidthInput=document.getElementById("travelWidth"),travelHeightInput=document.getElementById("travelHeight");
const UI_STORAGE_PREFIX="ttslhud.",DEFAULT_MAP_BOX_PX=160,DEFAULT_COMBAT_WIDTH_YALMS=20,DEFAULT_COMBAT_HEIGHT_YALMS=20,DEFAULT_TRAVEL_WIDTH_YALMS=50,DEFAULT_TRAVEL_HEIGHT_YALMS=50,MAP_PERCENT_PER_YALM=(41/2048/40)*100;
const tankJobs=new Set(["GLA","MRD","PLD","WAR","DRK","GNB"]),healJobs=new Set(["CNJ","WHM","SCH","AST","SGE"]),dpsJobs=new Set(["PGL","LNC","ROG","ARC","THM","ACN","MNK","DRG","NIN","SAM","RPR","VPR","BRD","MCH","DNC","BLM","SMN","RDM","PCT","BLU"]);
let currentAssetCatalog={jobIcons:{},maps:{},warnings:[]};
const clampNumber=(value,min,max,fallback)=>{const parsed=Number(value);return Number.isFinite(parsed)?Math.max(min,Math.min(max,parsed)):fallback};
function loadNumericPreference(key,fallback,min,max){try{const stored=window.localStorage.getItem(`${UI_STORAGE_PREFIX}${key}`);return clampNumber(stored,min,max,fallback)}catch{return fallback}}
function persistNumericPreference(key,value){try{window.localStorage.setItem(`${UI_STORAGE_PREFIX}${key}`,String(value))}catch{}}
function wireNumericPreference(input,key,fallback,min,max){const apply=()=>{const value=clampNumber(input.value,min,max,fallback);input.value=String(value);persistNumericPreference(key,value);refresh()};input.value=String(loadNumericPreference(key,fallback,min,max));input.addEventListener("change",apply);input.addEventListener("input",apply)}
const hash=s=>{let h=2166136261;for(let i=0;i<s.length;i++){h^=s.charCodeAt(i);h=Math.imul(h,16777619)}return h>>>0};
const shortCode=s=>hash(String(s)).toString(36).toUpperCase().padStart(4,"0").slice(0,4);
const kAcct=s=>krangle.checked?`ACC-${hash(String(s)).toString(16).toUpperCase().padStart(8,"0").slice(0,8)}`:String(s||"");
const pct=(cur,max)=>!max||max<=0?"--":`${Math.round((cur/max)*100)}%`;
const hpText=(cur,max)=>cur==null||max==null?"Unavailable":`${Number(cur).toLocaleString()} / ${Number(max).toLocaleString()} (${pct(cur,max)})`;
const mpText=(cur,max)=>cur==null||max==null?"Unavailable":`${Number(cur).toLocaleString()} / ${Number(max).toLocaleString()} (${pct(cur,max)})`;
const levelText=level=>level==null?"Lv --":`Lv ${level}`;
const posText=p=>!p?"Unavailable":`X ${p.x.toFixed(1)} | Y ${p.y.toFixed(1)} | Z ${p.z.toFixed(1)}`;
const rawCharacter=(name,world)=>{const rawName=String(name||"");return rawName.includes("@")||!world?rawName:`${rawName}@${String(world||"")}`;};
const displayCharacter=(name,world,krangledName)=>krangle.checked&&krangledName?String(krangledName):rawCharacter(name,world);
const displayName=(name,krangledName)=>krangle.checked&&krangledName?String(krangledName):String(name||"");
const displayEnemyName=(name,krangledName)=>krangleEnemies.checked&&krangledName?String(krangledName):String(name||"");
const shortLabel=(name,slot,world)=>enumerate.checked?String(slot??"?"):krangle.checked?shortCode(`${name||""}@${world||""}`):(String(name||"?").split(" ")[0]||"?").slice(0,4);
const genderSymbol=value=>value===0?"M":value===1?"F":"?";
const jobKind=job=>tankJobs.has(job)?"tank":healJobs.has(job)?"heal":dpsJobs.has(job)?"dps":"util";
function chip(text,kind=""){const el=document.createElement("span");el.className=`badge ${kind}`.trim();el.textContent=text;return el}
function stateChip(text,active,kind=""){const el=document.createElement("span");el.className=`state ${kind || (active?"on":"off")}`.trim();el.textContent=text;return el}
function tile(label,value,kind=""){const el=document.createElement("div");el.className="tile";el.innerHTML=`<div class="label">${label}</div><div class="value ${kind}">${value}</div>`;return el}
function jobIconAsset(jobIconId){return jobIconId==null?null:(currentAssetCatalog.jobIcons||{})[String(jobIconId)]||null}
function mapAsset(map){return !map||map.mapId==null?null:(currentAssetCatalog.maps||{})[String(map.mapId)]||null}
function currentViewportSettings(inCombat){return{boxPx:clampNumber(mapBoxPxInput.value,96,320,DEFAULT_MAP_BOX_PX),widthYalms:clampNumber(inCombat?combatWidthInput.value:travelWidthInput.value,5,500,inCombat?DEFAULT_COMBAT_WIDTH_YALMS:DEFAULT_TRAVEL_WIDTH_YALMS),heightYalms:clampNumber(inCombat?combatHeightInput.value:travelHeightInput.value,5,500,inCombat?DEFAULT_COMBAT_HEIGHT_YALMS:DEFAULT_TRAVEL_HEIGHT_YALMS)}}
function aggregatePartyInCombat(party){const source=Array.isArray(party?.members)?party.members.find(member=>member.isSource)||party.members.find(member=>!member.isStranger):null;return !!source?.conditions?.inCombat}
function worldSpanToMapPercent(spanYalms){return Math.max(.5,Math.min(100,Number(spanYalms||0)*MAP_PERCENT_PER_YALM))}
function mapCoordinate(value,offset,sizeFactor){if(value==null||offset==null||sizeFactor==null||sizeFactor===0)return null;const scale=Number(sizeFactor)/100;return(41/scale)*(((Number(value)+Number(offset))*scale+1024)/2048)+1}
function buildMapMarker(position,map){if(!position||!map)return null;const x=mapCoordinate(position.x,map.offsetX,map.sizeFactor),y=mapCoordinate(position.z,map.offsetY,map.sizeFactor);if(x==null||y==null)return null;return{x,y,left:Math.max(0,Math.min(100,((x-1)/40)*100)),top:Math.max(0,Math.min(100,((y-1)/40)*100))}}
function buildMapViewport(position,map,widthYalms,heightYalms){const marker=buildMapMarker(position,map);if(!marker)return{marker:null};const viewWidthPct=worldSpanToMapPercent(widthYalms),viewHeightPct=worldSpanToMapPercent(heightYalms),scaleX=Math.max(1,100/viewWidthPct),scaleY=Math.max(1,100/viewHeightPct),markerU=marker.left/100,markerV=marker.top/100,offsetX=Math.max(0,Math.min(1-(1/scaleX),markerU-(.5/scaleX))),offsetY=Math.max(0,Math.min(1-(1/scaleY),markerV-(.5/scaleY))),dotLeft=Math.max(0,Math.min(100,(markerU-offsetX)*scaleX*100)),dotTop=Math.max(0,Math.min(100,(markerV-offsetY)*scaleY*100));return{marker,imageWidthPercent:scaleX*100,imageHeightPercent:scaleY*100,imageLeftPercent:-offsetX*scaleX*100,imageTopPercent:-offsetY*scaleY*100,dotLeftPercent:dotLeft,dotTopPercent:dotTop}}
function renderIdentity(entity){const wrap=document.createElement("div");wrap.className="ident";if(!icons.checked)return wrap;const asset=jobIconAsset(entity.jobIconId);if(asset?.pngUrl){const img=document.createElement("img");img.className="iconimg";img.src=asset.pngUrl;img.alt=entity.job||`Job ${entity.jobIconId}`;img.title=entity.job||`Job ${entity.jobIconId}`;wrap.appendChild(img)}if(entity.job)wrap.appendChild(chip(entity.job,jobKind(entity.job)));if(entity.level!=null)wrap.appendChild(chip(`Lv ${entity.level}`,"util"));if(entity.gender!=null)wrap.appendChild(chip(genderSymbol(entity.gender),"util"));return wrap}
function buildEnemyPoints(combat){return Array.isArray(combat?.hostiles)?combat.hostiles.filter(enemy=>enemy.position).map((enemy,index)=>({position:enemy.position,color:enemy.isCurrentTarget?"#ff5e7d":enemy.isTargetingTrackedParty?"#ff9b7a":"#ff7f7f",label:enemy.isCurrentTarget?"TGT":`E${index+1}`})):[]}
function drawRadarBase(canvas,points,origin,labeler,widthYalms,heightYalms){const ctx=canvas.getContext("2d"),w=canvas.width,h=canvas.height,cx=w/2,cy=h/2,r=w/2-16,halfWidth=Math.max(1,Number(widthYalms)/2),halfHeight=Math.max(1,Number(heightYalms)/2);ctx.clearRect(0,0,w,h);ctx.fillStyle="#071018";ctx.fillRect(0,0,w,h);ctx.strokeStyle="rgba(255,255,255,.12)";ctx.strokeRect(9,9,w-18,h-18);ctx.beginPath();ctx.moveTo(cx,14);ctx.lineTo(cx,h-14);ctx.moveTo(14,cy);ctx.lineTo(w-14,cy);ctx.stroke();ctx.fillStyle="#79e58d";ctx.beginPath();ctx.arc(cx,cy,4,0,Math.PI*2);ctx.fill();if(!origin||points.length===0){ctx.fillStyle="#93a7bc";ctx.font="11px Segoe UI";ctx.fillText("No radar data",34,cy+4);return}for(const point of points){if(!point.position)continue;const dx=point.position.x-origin.x,dz=point.position.z-origin.z,px=cx+Math.max(-1,Math.min(1,dx/halfWidth))*r,py=cy+Math.max(-1,Math.min(1,dz/halfHeight))*r;ctx.fillStyle=point.color;ctx.beginPath();ctx.arc(px,py,3.5,0,Math.PI*2);ctx.fill();ctx.fillStyle="#eaf4ff";ctx.font="10px Segoe UI";ctx.fillText(labeler(point),px+5,py+3)}}
function drawRadar(canvas,client){const viewport=currentViewportSettings(!!client?.conditions?.inCombat);canvas.width=viewport.boxPx;canvas.height=viewport.boxPx;if(!client.position||!Array.isArray(client.party)||client.party.length===0){const hostiles=buildEnemyPoints(client.combat);if(hostiles.length===0){drawRadarBase(canvas,[],null,()=>"-",viewport.widthYalms,viewport.heightYalms);return}drawRadarBase(canvas,hostiles,client.position||null,point=>point.label,viewport.widthYalms,viewport.heightYalms);return}const points=client.party.filter(m=>m.position).map(m=>({position:m.position,color:"#ffbf74",slot:m.slot,name:m.name,world:client.worldName}));drawRadarBase(canvas,points.concat(buildEnemyPoints(client.combat)),client.position,point=>point.label||shortLabel(point.name,point.slot,point.world),viewport.widthYalms,viewport.heightYalms)}
function drawAggregateRadar(canvas,party){const viewport=currentViewportSettings(aggregatePartyInCombat(party));canvas.width=viewport.boxPx;canvas.height=viewport.boxPx;const source=party.members.find(m=>m.isSource&&m.position)||party.members.find(m=>m.position&&!m.isStranger)||null;if(!source){drawRadarBase(canvas,buildEnemyPoints(party.combat),null,point=>point.label,viewport.widthYalms,viewport.heightYalms);return}const points=party.members.filter(m=>m.position&&m!==source).map(m=>({position:m.position,color:m.isStranger?"#ff7f7f":m.isSubmitting?"#ffbf74":"#93a7bc",slot:m.slotText,name:m.name,world:m.worldName}));drawRadarBase(canvas,points.concat(buildEnemyPoints(party.combat)),source.position,point=>point.label||shortLabel(point.name,point.slot,point.world),viewport.widthYalms,viewport.heightYalms)}
function renderParty(client){const wrap=document.createElement("div");wrap.className="party";if(Array.isArray(client.party)&&client.party.length>0){for(const m of client.party){const row=document.createElement("div");row.className="member";const dist=typeof m.distance==="number"?`${m.distance.toFixed(1)}y`:"--";row.innerHTML=`<div class="slot">${m.slot}</div><div class="membername">${displayName(m.name,m.krangledName)}</div><div class="job">${m.job}</div><div class="dist">${dist}</div>`;row.title=`${levelText(m.level)} | HP ${hpText(m.currentHp,m.maxHp)} | MP ${mpText(m.currentMp,m.maxMp)}`;wrap.appendChild(row)}}else{const row=document.createElement("div");row.className="member";row.innerHTML=`<div class="slot">-</div><div class="membername">No party data captured yet.</div><div class="job">--</div><div class="dist">--</div>`;wrap.appendChild(row)}return wrap}
function renderStates(client){const wrap=document.createElement("div");wrap.className="states";wrap.append(stateChip("Combat",!!client.conditions?.inCombat),stateChip("Duty",!!client.conditions?.boundByDuty),stateChip("Queue",!!client.conditions?.waitingForDuty),stateChip("Mount",!!client.conditions?.mounted),stateChip("Cast",!!client.conditions?.casting),stateChip("Dead",!!client.conditions?.dead,client.conditions?.dead?"bad":"off"));return wrap}
function renderThreats(combat){const section=document.createElement("div");section.className="section";section.innerHTML=`<div class="sectionhead">Threat</div>`;const list=document.createElement("div");list.className="party";const hostiles=[];if(combat?.currentTarget)hostiles.push(combat.currentTarget);for(const hostile of combat?.hostiles||[]){if(!hostiles.some(existing=>existing.dataId===hostile.dataId&&existing.distance===hostile.distance&&existing.name===hostile.name))hostiles.push(hostile)}if(hostiles.length===0){const row=document.createElement("div");row.className="member";row.innerHTML=`<div class="slot">-</div><div class="membername">No combat telemetry captured.</div><div class="job">--</div><div class="dist">--</div>`;list.appendChild(row);section.appendChild(list);return section}for(const hostile of hostiles){const row=document.createElement("div");row.className="member";const dist=typeof hostile.distance==="number"?`${hostile.distance.toFixed(1)}y`:"--";const label=hostile.isCurrentTarget?"T":hostile.isTargetingTrackedParty?"A":"E";const hp=pct(hostile.currentHp,hostile.maxHp);row.innerHTML=`<div class="slot">${label}</div><div class="membername">${displayEnemyName(hostile.name,hostile.krangledName)}</div><div class="job">${dist}</div><div class="dist">${hp}</div>`;row.title=`${hostile.isCurrentTarget?"Current target":hostile.isTargetingLocalPlayer?"Targeting you":hostile.isTargetingTrackedParty?`Targeting ${displayName(hostile.targetName||"party",hostile.krangledTargetName||"")}`:hostile.targetName?`Targeting ${displayName(hostile.targetName,hostile.krangledTargetName)}`:"No tracked target"} | ${hostile.isCasting?`Cast ${hostile.castActionId??"?"} | ${hostile.castTimeRemaining?.toFixed(1)??"?"}s`:"Not casting"}`;list.appendChild(row)}section.appendChild(list);return section}
function renderMapSection(map,position,title="Map",inCombat=false){
  const section=document.createElement("div");
  section.className="section";
  section.innerHTML=`<div class="sectionhead">${title}</div>`;

  const viewport=currentViewportSettings(inCombat);
  const asset=mapAsset(map);
  const mapViewport=buildMapViewport(position,map,viewport.widthYalms,viewport.heightYalms);

  if(asset?.pngUrl){
    const frame=document.createElement("div");
    frame.className="mapframe";
    frame.style.width=`${viewport.boxPx}px`;
    frame.style.height=`${viewport.boxPx}px`;

    const img=document.createElement("img");
    img.className="mapimg";
    img.src=asset.pngUrl;
    img.alt=asset.texturePath||map?.texturePath||`Map ${map?.mapId??"?"}`;

    if(mapViewport.marker){
      img.style.width=`${mapViewport.imageWidthPercent}%`;
      img.style.height=`${mapViewport.imageHeightPercent}%`;
      img.style.left=`${mapViewport.imageLeftPercent}%`;
      img.style.top=`${mapViewport.imageTopPercent}%`;
    }else{
      img.style.width="100%";
      img.style.height="100%";
      img.style.left="0";
      img.style.top="0";
    }

    frame.appendChild(img);

    if(mapViewport.marker){
      const dot=document.createElement("span");
      dot.className="mapdot";
      dot.style.left=`${mapViewport.dotLeftPercent}%`;
      dot.style.top=`${mapViewport.dotTopPercent}%`;
      dot.title=`${mapViewport.marker.x.toFixed(1)}, ${mapViewport.marker.y.toFixed(1)}`;
      frame.appendChild(dot);
    }

    section.appendChild(frame);
  }else{
    const row=document.createElement("div");
    row.className="member";
    row.innerHTML=`<div class="slot">-</div><div class="membername">${map?.mapId!=null?"Map texture not extracted yet.":"No map data captured yet."}</div><div class="job">--</div><div class="dist">--</div>`;
    section.appendChild(row);
  }

  const textureLabel=asset?.texturePath||map?.texturePath;
  const meta=document.createElement("div");
  meta.className="meta";
  meta.append(
    tile(
      "Map",
      mapViewport.marker?`${mapViewport.marker.x.toFixed(1)}, ${mapViewport.marker.y.toFixed(1)}`:map?.mapId!=null?`Map ${map.mapId}`:"Unavailable",
      mapViewport.marker||map?.mapId!=null?"":"bad"
    ),
    tile("View",`${viewport.widthYalms.toFixed(0)}y x ${viewport.heightYalms.toFixed(0)}y`,""),
    tile(
      "Texture",
      textureLabel?String(textureLabel).split("/").pop()||String(textureLabel):asset?.pngUrl?"Extracted":"Unavailable",
      textureLabel||asset?.pngUrl?"":"bad"
    )
  );
  section.appendChild(meta);
  return section;
}
function renderClient(client){
  const card=document.createElement("section");
  card.className="card";

  const head=document.createElement("div");
  head.className="head";

  const info=document.createElement("div");
  info.innerHTML=`<div class="name">${displayCharacter(client.characterName,client.worldName,client.krangledName)}</div><div class="zone">${client.territoryName||"Unknown zone"} (${client.territoryId??0})</div><div class="sub">${kAcct(client.accountId)}</div>`;
  info.appendChild(renderIdentity({job:client.job,jobIconId:client.jobIconId,level:client.player?.level,gender:client.gender}));

  const badges=document.createElement("div");
  badges.className="badges";
  badges.appendChild(chip(client.isDisconnected?"Disconnected":client.stale?"Stale":"Live",client.isDisconnected?"bad":client.stale?"warn":"ok"));
  badges.appendChild(chip(`${client.ageSeconds.toFixed(1)}s`,""));

  const metrics=document.createElement("div");
  metrics.className="meta";
  metrics.append(
    tile("HP",hpText(client.player?.currentHp,client.player?.maxHp),client.player?.currentHp==null?"bad":""),
    tile("MP",mpText(client.player?.currentMp,client.player?.maxMp),client.player?.currentMp==null?"bad":""),
    tile("Position",posText(client.position),client.position?"":"bad"),
    tile("Repair",client.repair?`${client.repair.minCondition}% min | ${client.repair.averageCondition}% avg`:"Unavailable",client.repair?"":"bad")
  );

  const stateSection=document.createElement("div");
  stateSection.className="section";
  stateSection.innerHTML=`<div class="sectionhead">Status</div>`;
  stateSection.appendChild(renderStates(client));

  const partySection=document.createElement("div");
  partySection.className="section";
  partySection.innerHTML=`<div class="sectionhead">Party</div>`;
  partySection.appendChild(renderParty(client));

  const viewport=currentViewportSettings(!!client?.conditions?.inCombat);
  const radarSection=document.createElement("div");
  radarSection.className="radarbox";
  radarSection.innerHTML=`<div class="sectionhead">Radar | ${viewport.widthYalms.toFixed(0)}y x ${viewport.heightYalms.toFixed(0)}y</div>`;
  const radar=document.createElement("canvas");
  radarSection.appendChild(radar);

  const foot=document.createElement("div");
  foot.className="foot";
  foot.textContent=`Last update ${client.lastSeenUtc} | ${client.updateKind}`;

  head.append(info,badges);
  card.append(
    head,
    metrics,
    stateSection,
    partySection,
    renderMapSection(client.map,client.position,"Map",!!client?.conditions?.inCombat),
    renderThreats(client.combat),
    radarSection,
    foot
  );

  requestAnimationFrame(()=>drawRadar(radar,client));
  return card;
}
function renderAggregateMember(member){const row=document.createElement("div");row.className=`aggmember ${member.isStranger?"stranger":""}`.trim();const main=document.createElement("div");main.className="aggmain";const info=document.createElement("div");info.className="aggname";info.innerHTML=`<span class="slot">${member.slotText}</span><span class="membername">${displayCharacter(member.name,member.worldName,member.krangledName)}</span><span class="job">${member.job||"--"}</span><span class="lvl">${levelText(member.level)}</span>`;info.appendChild(renderIdentity(member));const badges=document.createElement("div");badges.className="badges";if(member.isStranger){badges.append(chip("Stranger","bad"),chip("Limited data","warn"))}else{badges.append(chip(member.isDisconnected?"Disconnected":member.stale?"Stale":"Live",member.isDisconnected?"bad":member.stale?"warn":"ok"));badges.append(chip(member.isSubmitting?"Submitting":"Monitored",member.isSubmitting?"ok":"warn"));if(member.isSource)badges.append(chip("Source","ok"))}main.append(info,badges);const meta=document.createElement("div");meta.className="aggmeta";meta.append(tile("HP",hpText(member.currentHp,member.maxHp),member.currentHp==null?"bad":""),tile("MP",mpText(member.currentMp,member.maxMp),member.currentMp==null?"bad":""),tile("Position",posText(member.position),member.position?"" :"bad"),tile("Extra",member.isStranger?"Conditions/repair unavailable":member.repair?`${member.repair.minCondition}% min | ${member.repair.averageCondition}% avg`:"No repair data",member.isStranger||!member.repair?"bad":""));row.append(main,meta);if(!member.isStranger){const states=renderStates(member);row.append(states);const note=document.createElement("div");note.className="aggnote";note.textContent=`${member.territoryName||"Unknown zone"} (${member.territoryId??0}) | Last update ${member.lastSeenUtc} | ${member.updateKind}`;row.append(note)}else{const note=document.createElement("div");note.className="aggnote bad";note.textContent="Only party-list fields are available for strangers: name, position, HP, MP, level, and job.";row.append(note)}return row}
function renderAggregateParty(party){
  const card=document.createElement("section");
  card.className="card";

  const head=document.createElement("div");
  head.className="head";

  const info=document.createElement("div");
  info.innerHTML=`<div class="name">Party | ${party.territoryName||"Unknown zone"}</div><div class="zone">Source ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}</div><div class="sub">${party.monitoredCount} monitored | ${party.strangerCount} stranger</div>`;

  const badges=document.createElement("div");
  badges.className="badges";
  badges.append(
    chip(`${party.liveCount} live`,"ok"),
    chip(`${party.staleCount} stale`,"warn"),
    chip(`${party.disconnectedCount} disconnected`,"bad")
  );

  const section=document.createElement("div");
  section.className="section";
  section.innerHTML=`<div class="sectionhead">Aggregated Party</div>`;

  const radarViewport=currentViewportSettings(aggregatePartyInCombat(party));
  const radarSection=document.createElement("div");
  radarSection.className="radarbox";
  radarSection.innerHTML=`<div class="sectionhead">Party Radar | ${radarViewport.widthYalms.toFixed(0)}y x ${radarViewport.heightYalms.toFixed(0)}y</div>`;
  const radar=document.createElement("canvas");
  radarSection.appendChild(radar);

  const members=document.createElement("div");
  members.className="aggmembers";
  for(const member of party.members)
    members.appendChild(renderAggregateMember(member));

  section.append(
    renderMapSection(party.map,party.sourcePosition,"Source Map",aggregatePartyInCombat(party)),
    radarSection,
    members,
    renderThreats(party.combat)
  );

  const foot=document.createElement("div");
  foot.className="foot";
  foot.textContent=`Stranger source locked to first monitored client: ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)} | Connected ${party.sourceConnectedAtUtc}`;

  head.append(info,badges);
  card.append(head,section,foot);
  requestAnimationFrame(()=>drawAggregateRadar(radar,party));
  return card;
}
function flattenGroups(groups){return groups.flatMap(group=>group.clients.map(client=>({...client,accountId:group.accountId})))}
function pathSummary(info){if(!info||!info.captured)return"same-PC game path not captured yet";return`same-PC game path ready from ${displayCharacter(info.sourceCharacterName,info.sourceWorldName,info.sourceKrangledName)}`}
function assetSummary(plan,catalog){const warning=(catalog?.warnings||[])[0];if(!plan||!plan.summary)return warning||"Asset plan pending.";const s=plan.summary;const readyIcons=Object.keys(catalog?.jobIcons||{}).length;const readyMaps=Object.keys(catalog?.maps||{}).length;const base=`Asset plan: ${s.jobIcons} icon tex path(s), ${s.maps} map texture(s), ${s.races} race id(s), ${s.tribes} tribe id(s), ${s.enemies} enemy id(s) | web cache ${readyIcons} icon png(s), ${readyMaps} map png(s)`;return warning?`${base} | ${warning}`:base}
function extractionSummary(state){if(!state)return"Extraction idle.";if(state.running)return`Extraction running: ${state.message||"working..."}`;if(state.lastCompletedUtc)return`Extraction ${state.lastExitCode===0?"ready":"failed"}: ${state.message||"see server log"}`;return state.message||"Extraction idle."}
async function triggerExtract(){try{extractAssets.disabled=true;const res=await fetch("/api/extract-assets",{method:"POST",headers:{"Content-Type":"application/json"},body:"{}"});const payload=await res.json();if(!res.ok||!payload.ok)throw new Error(payload.error||`HTTP ${res.status}`);extractStatus.textContent=payload.message||"Extraction started.";await refresh()}catch(err){extractStatus.textContent=`Extraction request failed: ${err}`;extractAssets.disabled=false}}
async function refresh(){try{const res=await fetch("/api/state",{cache:"no-store"});if(!res.ok)throw new Error(`HTTP ${res.status}`);const state=await res.json();currentAssetCatalog=state.assetCatalog||{jobIcons:{},maps:{},warnings:[]};const clients=flattenGroups(state.accountGroups).sort((a,b)=>Number(a.stale||a.isDisconnected)-Number(b.stale||b.isDisconnected)||String(a.characterName).localeCompare(String(b.characterName))||String(a.worldName).localeCompare(String(b.worldName)));const live=clients.filter(c=>!c.stale&&!c.isDisconnected).length;const aggregate=Array.isArray(state.aggregateParties)?state.aggregateParties:[];const looseFromServer=Array.isArray(state.looseClients)?state.looseClients:clients;const visibleLoose=(showStale.checked?looseFromServer:looseFromServer.filter(c=>!c.stale&&!c.isDisconnected)).sort((a,b)=>Number(a.stale||a.isDisconnected)-Number(b.stale||b.isDisconnected)||String(a.characterName).localeCompare(String(b.characterName))||String(a.worldName).localeCompare(String(b.worldName)));const visibleAggregate=aggregateParties.checked?(showStale.checked?aggregate:aggregate.filter(p=>p.liveCount>0)):[];summary.textContent=`${clients.length} client(s) tracked | ${live} live | ${clients.length-live} stale/disconnected${aggregateParties.checked?` | ${aggregate.length} party group(s)`:""}`;stamp.textContent=`Generated ${state.generatedAtUtc} | stale after ${state.staleSeconds}s | ${pathSummary(state.gamePathInfo)}`;assetPlan.textContent=assetSummary(state.assetPlan,currentAssetCatalog);extractStatus.textContent=extractionSummary(state.assetExtraction);extractAssets.textContent=state.assetExtraction?.running?"Extracting...":"Extract Assets";extractAssets.disabled=!!state.assetExtraction?.running||!state.gamePathInfo?.captured;app.replaceChildren();if(aggregateParties.checked){for(const party of visibleAggregate)app.appendChild(renderAggregateParty(party));for(const client of visibleLoose)app.appendChild(renderClient(client));if(visibleAggregate.length===0&&visibleLoose.length===0){const empty=document.createElement("div");empty.className="empty";empty.textContent=clients.length===0?"No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"All tracked clients are stale or disconnected.";app.appendChild(empty)}return}const visible=showStale.checked?clients:clients.filter(c=>!c.stale&&!c.isDisconnected);if(visible.length===0){const empty=document.createElement("div");empty.className="empty";empty.textContent=clients.length===0?"No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"All tracked clients are stale or disconnected.";app.appendChild(empty);return}for(const client of visible)app.appendChild(renderClient(client))}catch(err){summary.textContent="Refresh failed";stamp.textContent=String(err);assetPlan.textContent="Asset plan unavailable.";extractStatus.textContent="Extraction status unavailable.";extractAssets.disabled=false}}
wireNumericPreference(mapBoxPxInput,"mapBoxPx",DEFAULT_MAP_BOX_PX,96,320);wireNumericPreference(combatWidthInput,"combatWidth",DEFAULT_COMBAT_WIDTH_YALMS,5,300);wireNumericPreference(combatHeightInput,"combatHeight",DEFAULT_COMBAT_HEIGHT_YALMS,5,300);wireNumericPreference(travelWidthInput,"travelWidth",DEFAULT_TRAVEL_WIDTH_YALMS,5,500);wireNumericPreference(travelHeightInput,"travelHeight",DEFAULT_TRAVEL_HEIGHT_YALMS,5,500);extractAssets.addEventListener("click",triggerExtract);krangle.addEventListener("change",refresh);krangleEnemies.addEventListener("change",refresh);showStale.addEventListener("change",refresh);aggregateParties.addEventListener("change",refresh);icons.addEventListener("change",refresh);enumerate.addEventListener("change",refresh);refresh();setInterval(refresh,1000);
</script></body></html>"""


class TTSLStateStore:
    def __init__(self, stale_seconds: int) -> None:
        self.stale_seconds = stale_seconds
        self.retention_seconds = max(stale_seconds * 2, stale_seconds + 60)
        self._clients: dict[tuple[str, str, str], dict] = {}
        self._server_host_name = socket.gethostname().strip().casefold()
        self._session_game_path: str | None = None
        self._session_game_path_source: dict | None = None
        self._asset_plan_output_path = os.path.join(SERVER_ROOT, "ttsl_asset_plan.json")
        self._last_asset_plan_json = ""
        self._asset_catalog_cache_mtime = -1.0
        self._asset_catalog_cache = {"available": False, "jobIcons": {}, "maps": {}, "warnings": []}
        self._asset_extract_state = {
            "running": False,
            "message": "Extraction idle.",
            "lastStartedUtc": None,
            "lastCompletedUtc": None,
            "lastExitCode": None,
        }
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
                "connectedAtUtc": utc_iso(now),
                "connectedAtUnix": now_unix,
            }
            client["updateKind"] = payload.get("updateKind", "full")
            client["lastSeenUtc"] = utc_iso(now)
            client["lastSeenUnix"] = now_unix
            client["isDisconnected"] = False
            client["goodbyeUtc"] = None
            for field in (
                "hostName",
                "gameInstallPath",
                "krangledName",
                "enumeratePartyMembers",
                "job",
                "jobId",
                "jobIconId",
                "gender",
                "territoryId",
                "territoryName",
                "mapId",
                "map",
                "position",
                "player",
                "raceId",
                "tribeId",
                "conditions",
                "repair",
                "party",
                "combat",
            ):
                if field in payload and payload[field] is not None:
                    client[field] = payload[field]
            self._clients[key] = client

            self._capture_same_pc_game_path_if_needed(client)

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
                "connectedAtUtc": utc_iso(now),
                "connectedAtUnix": time.time(),
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
            snapshot_clients: list[dict] = []
            now_unix = time.time()
            for client in self._clients.values():
                item = deepcopy(client)
                age_seconds = max(0.0, now_unix - float(item.get("lastSeenUnix", now_unix)))
                item["ageSeconds"] = age_seconds
                item["stale"] = age_seconds >= self.stale_seconds
                snapshot_clients.append(item)

                group_item = self._sanitize_client_for_output(item)
                groups.setdefault(group_item["accountId"], []).append(group_item)
            account_groups = []
            for account_id, clients in groups.items():
                clients.sort(key=lambda item: (item["stale"], item["isDisconnected"], item["characterName"], item["worldName"]))
                account_groups.append({"accountId": account_id, "clients": clients})
            account_groups.sort(key=lambda item: item["accountId"])
            aggregate_parties, loose_clients = self._build_aggregate_parties(snapshot_clients)
            asset_plan = self._build_asset_plan_locked(snapshot_clients, now)
            asset_catalog = self._build_asset_catalog_locked()
            return {
                "generatedAtUtc": utc_iso(now),
                "staleSeconds": self.stale_seconds,
                "totalClients": sum(len(group["clients"]) for group in account_groups),
                "accountGroups": account_groups,
                "aggregateParties": aggregate_parties,
                "looseClients": loose_clients,
                "assetPlan": asset_plan,
                "assetCatalog": asset_catalog,
                "assetExtraction": deepcopy(self._asset_extract_state),
                "gamePathInfo": {
                    "captured": self._session_game_path is not None,
                    "gameInstallPath": self._session_game_path,
                    "sourceCharacterName": None if self._session_game_path_source is None else self._session_game_path_source.get("characterName"),
                    "sourceWorldName": None if self._session_game_path_source is None else self._session_game_path_source.get("worldName"),
                    "sourceKrangledName": None if self._session_game_path_source is None else self._session_game_path_source.get("krangledName"),
                    "sourceHostName": None if self._session_game_path_source is None else self._session_game_path_source.get("hostName"),
                },
            }

    def trigger_asset_extract(self) -> tuple[bool, str]:
        now = utc_now()
        with self._lock:
            if self._asset_extract_state["running"]:
                return False, "Asset extraction is already running."
            if self._session_game_path is None:
                return False, "Same-PC game path not captured yet."
            if not os.path.isfile(EXTRACT_SCRIPT_PATH):
                return False, f"Extractor script not found: {EXTRACT_SCRIPT_PATH}"

            self._build_asset_plan_locked([deepcopy(client) for client in self._clients.values()], now)
            self._asset_extract_state = {
                "running": True,
                "message": "Launching extractor with the current session plan.",
                "lastStartedUtc": utc_iso(now),
                "lastCompletedUtc": self._asset_extract_state.get("lastCompletedUtc"),
                "lastExitCode": self._asset_extract_state.get("lastExitCode"),
            }

        threading.Thread(target=self._run_asset_extract, daemon=True).start()
        log_event("Asset extraction requested from web UI.")
        return True, "Asset extraction started."

    def _build_asset_plan_locked(self, snapshot_clients: list[dict], generated_at: datetime) -> dict:
        territory_ids: set[int] = set()
        map_ids: set[int] = set()
        map_textures: dict[str, dict] = {}
        race_ids: set[int] = set()
        tribe_ids: set[int] = set()
        job_ids: set[int] = set()
        job_icon_ids: set[int] = set()
        enemy_data_ids: set[int] = set()

        for client in snapshot_clients:
            self._append_asset_ids_from_entity(client, territory_ids, map_ids, map_textures, race_ids, tribe_ids, job_ids, job_icon_ids)
            for party_member in client.get("party") or []:
                self._append_asset_ids_from_entity(party_member, territory_ids, map_ids, map_textures, race_ids, tribe_ids, job_ids, job_icon_ids)

            combat = client.get("combat") or {}
            current_target = combat.get("currentTarget")
            if isinstance(current_target, dict):
                self._append_enemy_id(current_target, enemy_data_ids)
            for hostile in combat.get("hostiles") or []:
                self._append_enemy_id(hostile, enemy_data_ids)

        job_icon_tex_paths = [
            f"ui/icon/{(icon_id // 1000) * 1000:06d}/{icon_id:06d}_hr1.tex"
            for icon_id in sorted(job_icon_ids)
        ]
        ordered_map_textures = sorted(
            map_textures.values(),
            key=lambda entry: (int(entry.get("mapId") or 0), str(entry.get("texturePath") or "")),
        )
        asset_plan = {
            "generatedAtUtc": utc_iso(generated_at),
            "samePcCaptured": self._session_game_path is not None,
            "gameInstallPath": self._session_game_path,
            "sourceCharacterName": None if self._session_game_path_source is None else self._session_game_path_source.get("characterName"),
            "sourceWorldName": None if self._session_game_path_source is None else self._session_game_path_source.get("worldName"),
            "sourceKrangledName": None if self._session_game_path_source is None else self._session_game_path_source.get("krangledName"),
            "territoryIds": sorted(territory_ids),
            "mapIds": sorted(map_ids),
            "raceIds": sorted(race_ids),
            "tribeIds": sorted(tribe_ids),
            "jobIds": sorted(job_ids),
            "jobIconIds": sorted(job_icon_ids),
            "jobIconTexPaths": job_icon_tex_paths,
            "mapTextures": ordered_map_textures,
            "enemyDataIds": sorted(enemy_data_ids),
            "goals": {
                "jobIcons": {"status": "ready_to_extract" if job_icon_tex_paths else "waiting_for_data", "count": len(job_icon_tex_paths)},
                "raceIcons": {"status": "needs_sheet_mapping", "count": len(race_ids)},
                "mapTiles": {"status": "ready_to_extract" if ordered_map_textures else "waiting_for_data", "count": len(ordered_map_textures)},
            },
            "summary": {
                "jobIcons": len(job_icon_tex_paths),
                "maps": len(ordered_map_textures),
                "territories": len(territory_ids),
                "races": len(race_ids),
                "tribes": len(tribe_ids),
                "enemies": len(enemy_data_ids),
            },
        }

        serialized = json.dumps(asset_plan, ensure_ascii=False, indent=2, sort_keys=True)
        if serialized != self._last_asset_plan_json:
            with open(self._asset_plan_output_path, "w", encoding="utf-8") as handle:
                handle.write(serialized)
                handle.write("\n")
            self._last_asset_plan_json = serialized

        return asset_plan

    def _run_asset_extract(self) -> None:
        started_at = utc_now()
        try:
            result = subprocess.run(
                [
                    sys.executable,
                    EXTRACT_SCRIPT_PATH,
                    "--plan",
                    self._asset_plan_output_path,
                    "--output-root",
                    EXTRACT_OUTPUT_ROOT,
                    "--summary",
                    EXTRACT_SUMMARY_PATH,
                ],
                cwd=SERVER_ROOT,
                capture_output=True,
                text=True,
                check=False,
            )
            stdout = (result.stdout or "").strip()
            stderr = (result.stderr or "").strip()
            if stdout:
                log_event(f"Asset extraction stdout:\n{stdout}")
            if stderr:
                log_event(f"Asset extraction stderr:\n{stderr}")
            message = stdout.splitlines()[-1] if stdout else "Extractor finished."
            if result.returncode != 0:
                message = stderr.splitlines()[-1] if stderr else (stdout.splitlines()[-1] if stdout else "Extractor failed.")
                log_event(f"Asset extraction failed ({result.returncode}): {message}")
            else:
                log_event(f"Asset extraction finished: {message}")

            completed_at = utc_now()
            with self._lock:
                self._asset_catalog_cache_mtime = -2.0
                self._asset_extract_state = {
                    "running": False,
                    "message": message,
                    "lastStartedUtc": utc_iso(started_at),
                    "lastCompletedUtc": utc_iso(completed_at),
                    "lastExitCode": result.returncode,
                }
        except Exception as exc:
            completed_at = utc_now()
            log_event(f"Asset extraction crashed: {exc}")
            with self._lock:
                self._asset_catalog_cache_mtime = -2.0
                self._asset_extract_state = {
                    "running": False,
                    "message": str(exc),
                    "lastStartedUtc": utc_iso(started_at),
                    "lastCompletedUtc": utc_iso(completed_at),
                    "lastExitCode": -1,
                }

    def _build_asset_catalog_locked(self) -> dict:
        summary_mtime = os.path.getmtime(EXTRACT_SUMMARY_PATH) if os.path.isfile(EXTRACT_SUMMARY_PATH) else -1.0
        if summary_mtime == self._asset_catalog_cache_mtime:
            return deepcopy(self._asset_catalog_cache)

        catalog = {"available": False, "jobIcons": {}, "maps": {}, "warnings": []}
        if summary_mtime < 0:
            catalog["warnings"].append("No extracted asset summary found yet.")
            self._asset_catalog_cache = catalog
            self._asset_catalog_cache_mtime = summary_mtime
            return deepcopy(catalog)

        try:
            with open(EXTRACT_SUMMARY_PATH, "r", encoding="utf-8") as handle:
                summary = json.load(handle)

            if not isinstance(summary, dict):
                raise ValueError("Extracted asset summary is not a JSON object.")

            status = str(summary.get("status") or "").strip()
            if status and status != "ok":
                catalog["warnings"].append(str(summary.get("error") or f"Last asset extraction status was {status}."))

            for entry in summary.get("extractedFiles") or []:
                if not isinstance(entry, dict):
                    continue

                raw_path = self._resolve_extracted_raw_path(entry)
                if not raw_path:
                    continue

                kind = self._infer_extracted_file_kind(entry)
                try:
                    if kind == "jobIcon":
                        job_icon_id = self._infer_job_icon_id(entry)
                        if job_icon_id is None:
                            continue

                        cache_path = ensure_png_cache(raw_path, f"job-icons/{job_icon_id:06d}.png")
                        catalog["jobIcons"][str(job_icon_id)] = {
                            "jobIconId": job_icon_id,
                            "pngUrl": build_cache_url(cache_path),
                        }
                        continue

                    if kind == "mapTexture":
                        map_id = entry.get("mapId")
                        if map_id in (None, ""):
                            continue

                        map_id_value = int(map_id)
                        base_name = os.path.splitext(os.path.basename(str(entry.get("relativePath") or f"map_{map_id_value}.tex")))[0]
                        cache_path = ensure_png_cache(raw_path, f"maps/{map_id_value:06d}_{base_name}.png")
                        catalog["maps"][str(map_id_value)] = {
                            "mapId": map_id_value,
                            "pngUrl": build_cache_url(cache_path),
                            "texturePath": entry.get("relativePath") or entry.get("texturePath"),
                            "offsetX": entry.get("offsetX"),
                            "offsetY": entry.get("offsetY"),
                            "sizeFactor": entry.get("sizeFactor"),
                        }
                except Exception as exc:
                    catalog["warnings"].append(f"{kind}: {exc}")

            catalog["available"] = bool(catalog["jobIcons"] or catalog["maps"])
        except Exception as exc:
            catalog["warnings"].append(str(exc))

        self._asset_catalog_cache = catalog
        self._asset_catalog_cache_mtime = summary_mtime
        return deepcopy(catalog)

    @staticmethod
    def _resolve_extracted_raw_path(entry: dict) -> str | None:
        output_path = str(entry.get("outputPath") or "").strip()
        if output_path and os.path.isfile(output_path):
            return output_path

        relative_path = str(entry.get("relativePath") or "").strip()
        if not relative_path:
            return None

        candidate = os.path.join(EXTRACT_OUTPUT_ROOT, "raw", relative_path.replace("/", os.sep))
        return candidate if os.path.isfile(candidate) else None

    @staticmethod
    def _infer_extracted_file_kind(entry: dict) -> str:
        explicit_kind = str(entry.get("kind") or "").strip()
        if explicit_kind:
            return explicit_kind

        relative_path = str(entry.get("relativePath") or "").replace("\\", "/")
        if relative_path.startswith("ui/icon/"):
            return "jobIcon"
        if relative_path.startswith("ui/map/"):
            return "mapTexture"
        return "asset"

    @staticmethod
    def _infer_job_icon_id(entry: dict) -> int | None:
        value = entry.get("jobIconId")
        if value not in (None, ""):
            try:
                return int(value)
            except (TypeError, ValueError):
                pass

        relative_path = str(entry.get("relativePath") or "")
        file_name = os.path.splitext(os.path.basename(relative_path))[0]
        primary_token = file_name.split("_", 1)[0]
        digits = "".join(ch for ch in primary_token if ch.isdigit())
        if not digits:
            return None
        try:
            return int(digits)
        except ValueError:
            return None

    @staticmethod
    def _append_enemy_id(enemy: dict, enemy_data_ids: set[int]) -> None:
        data_id = enemy.get("dataId")
        if data_id:
            enemy_data_ids.add(int(data_id))

    @staticmethod
    def _append_numeric(value: object, target: set[int]) -> None:
        if value in (None, ""):
            return
        try:
            target.add(int(value))
        except (TypeError, ValueError):
            return

    def _append_asset_ids_from_entity(
        self,
        entity: dict,
        territory_ids: set[int],
        map_ids: set[int],
        map_textures: dict[str, dict],
        race_ids: set[int],
        tribe_ids: set[int],
        job_ids: set[int],
        job_icon_ids: set[int],
    ) -> None:
        self._append_numeric(entity.get("territoryId"), territory_ids)
        self._append_numeric(entity.get("mapId"), map_ids)
        map_info = entity.get("map")
        if isinstance(map_info, dict):
            self._append_numeric(map_info.get("mapId"), map_ids)
            texture_candidates: list[str] = []
            texture_path = str(map_info.get("texturePath") or "").strip()
            if texture_path:
                texture_candidates.append(texture_path)

            for candidate in map_info.get("texturePathCandidates") or []:
                candidate_text = str(candidate or "").strip()
                if candidate_text and candidate_text not in texture_candidates:
                    texture_candidates.append(candidate_text)

            map_id = map_info.get("mapId")
            map_id_value = 0
            if map_id not in (None, ""):
                try:
                    map_id_value = int(map_id)
                except (TypeError, ValueError):
                    map_id_value = 0

            if texture_candidates:
                map_key = f"map:{map_id_value}" if map_id_value > 0 else f"texture:{texture_candidates[0].casefold()}"
                existing = map_textures.get(map_key)
                replacement = {
                    "mapId": map_id_value if map_id_value > 0 else map_info.get("mapId"),
                    "texturePath": texture_candidates[0],
                    "texturePathCandidates": texture_candidates,
                    "offsetX": map_info.get("offsetX"),
                    "offsetY": map_info.get("offsetY"),
                    "sizeFactor": map_info.get("sizeFactor"),
                }
                if existing is None or len(texture_candidates) > len(existing.get("texturePathCandidates") or []):
                    map_textures[map_key] = replacement
        self._append_numeric(entity.get("raceId"), race_ids)
        self._append_numeric(entity.get("tribeId"), tribe_ids)
        self._append_numeric(entity.get("jobId"), job_ids)
        self._append_numeric(entity.get("jobIconId"), job_icon_ids)

    def _capture_same_pc_game_path_if_needed(self, client: dict) -> None:
        if self._session_game_path is not None:
            return

        host_name = str(client.get("hostName", "")).strip().casefold()
        game_install_path = str(client.get("gameInstallPath", "")).strip()
        if not host_name or host_name != self._server_host_name or not game_install_path:
            return

        normalized_path = os.path.normpath(game_install_path)
        if not os.path.isdir(normalized_path):
            return

        self._session_game_path = normalized_path
        self._session_game_path_source = {
            "characterName": client.get("characterName", ""),
            "worldName": client.get("worldName", ""),
            "krangledName": client.get("krangledName", ""),
            "hostName": client.get("hostName", ""),
        }
        log_event(
            f"Locked same-PC game path from {client.get('characterName', '')}@{client.get('worldName', '')}: {normalized_path}"
        )

    def _build_aggregate_parties(self, snapshot_clients: list[dict]) -> tuple[list[dict], list[dict]]:
        party_clients = [client for client in snapshot_clients if isinstance(client.get("party"), list) and len(client["party"]) > 0]
        if not party_clients:
            loose_clients = sorted(snapshot_clients, key=self._client_sort_key)
            return [], [self._sanitize_client_for_output(client) for client in loose_clients]

        client_by_key = {self._client_key_from_item(client): client for client in snapshot_clients}
        adjacency: dict[tuple[str, str, str], set[tuple[str, str, str]]] = {
            self._client_key_from_item(client): set() for client in party_clients
        }

        for index, left in enumerate(party_clients):
            left_key = self._client_key_from_item(left)
            for right in party_clients[index + 1:]:
                right_key = self._client_key_from_item(right)
                if self._clients_share_party(left, right):
                    adjacency[left_key].add(right_key)
                    adjacency[right_key].add(left_key)

        grouped_keys: set[tuple[str, str, str]] = set()
        component_lists: list[list[tuple[str, str, str]]] = []
        visited_keys: set[tuple[str, str, str]] = set()
        for start_key in sorted(adjacency, key=lambda key: self._client_sort_key(client_by_key[key])):
            if start_key in visited_keys:
                continue

            stack = [start_key]
            component_keys: list[tuple[str, str, str]] = []
            while stack:
                current = stack.pop()
                if current in visited_keys:
                    continue

                visited_keys.add(current)
                component_keys.append(current)
                stack.extend(neighbor for neighbor in adjacency[current] if neighbor not in visited_keys)

            component_lists.append(component_keys)

        aggregate_parties: list[dict] = []
        for component_keys in component_lists:
            if all(key in grouped_keys for key in component_keys):
                continue

            component_clients = [client_by_key[key] for key in component_keys if key not in grouped_keys]
            aggregate_party, represented_keys = self._build_aggregate_party(component_clients, snapshot_clients, grouped_keys)
            if aggregate_party is None:
                continue

            grouped_keys.update(represented_keys)
            aggregate_parties.append(aggregate_party)

        loose_clients = [
            self._sanitize_client_for_output(client)
            for client in sorted(snapshot_clients, key=self._client_sort_key)
            if self._client_key_from_item(client) not in grouped_keys
        ]

        aggregate_parties.sort(key=lambda party: (party["sourceConnectedAtUtc"], party["sourceCharacterName"], party["sourceWorldName"]))
        return aggregate_parties, loose_clients

    def _build_aggregate_party(
        self,
        component_clients: list[dict],
        all_clients: list[dict],
        reserved_keys: set[tuple[str, str, str]],
    ) -> tuple[dict | None, set[tuple[str, str, str]]]:
        if not component_clients:
            return None, set()

        source_client = min(
            component_clients,
            key=lambda client: (
                float(client.get("connectedAtUnix", client.get("lastSeenUnix", 0.0))),
                str(client.get("characterName", "")),
                str(client.get("worldName", "")),
            ),
        )
        source_key = self._client_key_from_item(source_client)
        used_keys: set[tuple[str, str, str]] = set()
        represented_names: set[tuple[str, str]] = set()
        members: list[dict] = []

        source_party = self._dedupe_party_members(source_client.get("party") or [])
        for party_member in source_party:
            normalized_member_name = self._normalize_party_identity(party_member.get("name"))
            if not normalized_member_name[0]:
                continue

            matched_client = self._match_monitored_client(all_clients, party_member, used_keys | reserved_keys)
            if matched_client is not None:
                used_keys.add(self._client_key_from_item(matched_client))
                represented_names.add(normalized_member_name)
                members.append(self._build_monitored_member(matched_client, source_key, party_member))
            else:
                represented_names.add(normalized_member_name)
                members.append(self._build_stranger_member(party_member))

        for extra_client in sorted(component_clients, key=self._client_sort_key):
            extra_key = self._client_key_from_item(extra_client)
            normalized_extra_name = self._client_identity(extra_client)
            if extra_key in used_keys or extra_key in reserved_keys or (normalized_extra_name[0] and normalized_extra_name in represented_names):
                continue

            if normalized_extra_name[0]:
                represented_names.add(normalized_extra_name)
            used_keys.add(extra_key)
            members.append(self._build_monitored_member(extra_client, source_key, None))

        represented_clients = [client for client in all_clients if self._client_key_from_item(client) in used_keys]
        live_count = sum(1 for client in represented_clients if not client.get("stale") and not client.get("isDisconnected"))
        stale_count = sum(1 for client in represented_clients if client.get("stale") and not client.get("isDisconnected"))
        disconnected_count = sum(1 for client in represented_clients if client.get("isDisconnected"))

        return {
            "sourceCharacterName": source_client.get("characterName", ""),
            "sourceWorldName": source_client.get("worldName", ""),
            "sourceKrangledName": source_client.get("krangledName", ""),
            "sourceConnectedAtUtc": source_client.get("connectedAtUtc", source_client.get("lastSeenUtc", "Unknown")),
            "territoryId": source_client.get("territoryId"),
            "territoryName": source_client.get("territoryName", "Unknown zone"),
            "map": deepcopy(source_client.get("map")),
            "sourcePosition": deepcopy(source_client.get("position")),
            "monitoredCount": len(used_keys),
            "strangerCount": sum(1 for member in members if member["isStranger"]),
            "liveCount": live_count,
            "staleCount": stale_count,
            "disconnectedCount": disconnected_count,
            "combat": deepcopy(source_client.get("combat")),
            "members": members,
        }, used_keys

    def _match_monitored_client(
        self,
        all_clients: list[dict],
        party_member: dict,
        used_keys: set[tuple[str, str, str]],
    ) -> dict | None:
        candidates = []
        for client in all_clients:
            client_key = self._client_key_from_item(client)
            if client_key in used_keys:
                continue
            if self._client_matches_party_member(client, party_member):
                candidates.append(client)

        if not candidates:
            return None

        candidates.sort(
            key=lambda client: (
                bool(client.get("stale")),
                bool(client.get("isDisconnected")),
                self._territory_sort_penalty(client, party_member),
                self._client_sort_key(client),
            )
        )
        return candidates[0]

    def _build_monitored_member(
        self,
        client: dict,
        source_key: tuple[str, str, str],
        party_member: dict | None,
    ) -> dict:
        fallback_party_member = party_member or self._find_self_party_member(client)
        player = client.get("player") or {}

        return {
            "slotText": self._slot_text((fallback_party_member or {}).get("slot")),
            "name": client.get("characterName", ""),
            "worldName": client.get("worldName", ""),
            "krangledName": client.get("krangledName", ""),
            "job": (fallback_party_member or {}).get("job") or client.get("job", "UNK"),
            "jobId": (fallback_party_member or {}).get("jobId") or client.get("jobId"),
            "jobIconId": (fallback_party_member or {}).get("jobIconId") or client.get("jobIconId"),
            "level": player.get("level") or (fallback_party_member or {}).get("level"),
            "gender": client.get("gender"),
            "currentHp": player.get("currentHp"),
            "maxHp": player.get("maxHp"),
            "currentMp": player.get("currentMp"),
            "maxMp": player.get("maxMp"),
            "raceId": client.get("raceId"),
            "tribeId": client.get("tribeId"),
            "position": deepcopy(client.get("position")),
            "conditions": deepcopy(client.get("conditions")),
            "repair": deepcopy(client.get("repair")),
            "territoryId": client.get("territoryId"),
            "territoryName": client.get("territoryName", "Unknown zone"),
            "lastSeenUtc": client.get("lastSeenUtc", "Unknown"),
            "updateKind": client.get("updateKind", "full"),
            "stale": bool(client.get("stale")),
            "isDisconnected": bool(client.get("isDisconnected")),
            "isMonitored": True,
            "isSubmitting": not client.get("stale") and not client.get("isDisconnected"),
            "isSource": self._client_key_from_item(client) == source_key,
            "isStranger": False,
        }

    def _build_stranger_member(self, party_member: dict) -> dict:
        return {
            "slotText": self._slot_text(party_member.get("slot")),
            "name": party_member.get("name", ""),
            "worldName": self._display_world_from_party_member(party_member.get("name")),
            "krangledName": party_member.get("krangledName", ""),
            "job": party_member.get("job", "UNK"),
            "jobId": party_member.get("jobId"),
            "jobIconId": party_member.get("jobIconId"),
            "level": party_member.get("level"),
            "gender": None,
            "currentHp": party_member.get("currentHp"),
            "maxHp": party_member.get("maxHp"),
            "currentMp": party_member.get("currentMp"),
            "maxMp": party_member.get("maxMp"),
            "raceId": party_member.get("raceId"),
            "tribeId": party_member.get("tribeId"),
            "position": deepcopy(party_member.get("position")),
            "conditions": None,
            "repair": None,
            "territoryId": None,
            "territoryName": "Unavailable",
            "lastSeenUtc": "Unavailable",
            "updateKind": "party",
            "stale": False,
            "isDisconnected": False,
            "isMonitored": False,
            "isSubmitting": False,
            "isSource": False,
            "isStranger": True,
        }

    @staticmethod
    def _client_key_from_item(client: dict) -> tuple[str, str, str]:
        return (
            str(client.get("accountId", "")).strip(),
            str(client.get("characterName", "")).strip(),
            str(client.get("worldName", "")).strip(),
        )

    @staticmethod
    def _sanitize_client_for_output(client: dict) -> dict:
        output = deepcopy(client)
        output.pop("lastSeenUnix", None)
        output.pop("connectedAtUnix", None)
        return output

    @staticmethod
    def _normalize_name(value: object) -> str:
        return str(value or "").strip().casefold()

    @classmethod
    def _normalize_party_identity(cls, value: object) -> tuple[str, str]:
        raw = " ".join(str(value or "").strip().split())
        if not raw:
            return "", ""

        if "@" in raw:
            raw_name, raw_world = raw.split("@", 1)
            return cls._normalize_name(raw_name), cls._normalize_name(raw_world)

        return cls._normalize_name(raw), ""

    @classmethod
    def _client_identity(cls, client: dict) -> tuple[str, str]:
        return cls._normalize_name(client.get("characterName")), cls._normalize_name(client.get("worldName"))

    @classmethod
    def _client_matches_party_member(cls, client: dict, party_member: dict) -> bool:
        member_name, member_world = cls._normalize_party_identity(party_member.get("name"))
        if not member_name:
            return False

        client_name, client_world = cls._client_identity(client)
        if client_name != member_name:
            return False
        if member_world and client_world != member_world:
            return False
        return True

    @staticmethod
    def _display_world_from_party_member(value: object) -> str:
        raw = str(value or "").strip()
        if "@" not in raw:
            return ""
        return raw.split("@", 1)[1].strip()

    @classmethod
    def _dedupe_party_members(cls, party_members: list[dict]) -> list[dict]:
        deduped: list[dict] = []
        seen_names: set[tuple[str, str]] = set()
        for party_member in sorted(party_members, key=lambda member: (int(member.get("slot", 999)), str(member.get("name", "")))):
            normalized_name = cls._normalize_party_identity(party_member.get("name"))
            if not normalized_name[0] or normalized_name in seen_names:
                continue

            seen_names.add(normalized_name)
            deduped.append(party_member)

        return deduped

    @staticmethod
    def _slot_text(value: object) -> str:
        return "?" if value in (None, "") else str(value)

    @classmethod
    def _client_sort_key(cls, client: dict) -> tuple:
        return (
            float(client.get("connectedAtUnix", client.get("lastSeenUnix", 0.0))),
            cls._normalize_name(client.get("characterName")),
            cls._normalize_name(client.get("worldName")),
        )

    @classmethod
    def _clients_share_party(cls, left: dict, right: dict) -> bool:
        return cls._party_contains(left, right) or cls._party_contains(right, left)

    @classmethod
    def _party_contains(cls, source: dict, target: dict) -> bool:
        target_name, target_world = cls._client_identity(target)
        if not target_name or not cls._territory_matches(source, target):
            return False

        for member in source.get("party") or []:
            member_name, member_world = cls._normalize_party_identity(member.get("name"))
            if member_name == target_name and (not member_world or member_world == target_world):
                return True

        return False

    @staticmethod
    def _territory_matches(left: dict, right: dict) -> bool:
        left_territory = left.get("territoryId")
        right_territory = right.get("territoryId")
        return left_territory is None or right_territory is None or left_territory == right_territory

    @classmethod
    def _territory_sort_penalty(cls, client: dict, party_member: dict) -> int:
        party_territory = party_member.get("territoryId")
        client_territory = client.get("territoryId")
        if party_territory is None or client_territory is None:
            return 0
        return 0 if party_territory == client_territory else 1

    @classmethod
    def _find_self_party_member(cls, client: dict) -> dict | None:
        own_name = cls._client_identity(client)
        for party_member in client.get("party") or []:
            if cls._normalize_party_identity(party_member.get("name")) == own_name:
                return party_member

        return None

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
            if self.path.startswith("/assets/"):
                asset_path = self._resolve_asset_path(self.path)
                if asset_path is None:
                    self.send_error(HTTPStatus.NOT_FOUND, "Unknown asset")
                    return

                with open(asset_path, "rb") as handle:
                    body = handle.read()

                content_type = mimetypes.guess_type(asset_path)[0] or "application/octet-stream"
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", content_type)
                self.send_header("Cache-Control", "public, max-age=300")
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
                if self.path == "/api/extract-assets":
                    ok, message = state.trigger_asset_extract()
                    return self._write_json({"ok": ok, "message": message, "error": None if ok else message}, HTTPStatus.OK if ok else HTTPStatus.CONFLICT)
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

        @staticmethod
        def _resolve_asset_path(request_path: str) -> str | None:
            parsed_path = urlparse(request_path).path
            relative_path = unquote(parsed_path[len("/assets/"):]).lstrip("/")
            if not relative_path:
                return None

            cache_root = os.path.normpath(CACHE_ROOT)
            candidate = os.path.normpath(os.path.join(cache_root, relative_path.replace("/", os.sep)))
            if os.path.commonpath([cache_root, candidate]) != cache_root:
                return None
            return candidate if os.path.isfile(candidate) else None

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
