#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import io
import importlib
import json
import mimetypes
import os
import shutil
import socket
import struct
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
SCREENSHOT_CACHE_ROOT = os.path.join(CACHE_ROOT, "screenshots")
LOCAL_PYTHON_DEPS_ROOT = os.path.join(SERVER_ROOT, "_pydeps")
AUTO_EXTRACT_RETRY_COOLDOWN_SECONDS = 30.0
TEX_HEADER_SIZE = 80
TEX_FORMAT_A8R8G8B8 = 5200
TEX_FORMAT_DXT1 = 13344
TEX_FORMAT_DXT3 = 13360
TEX_FORMAT_DXT5 = 13361
DDS_MAGIC = 0x20534444
DDS_HEADER_SIZE = 124
DDS_PIXEL_FORMAT_SIZE = 32
DDS_CAPS_TEXTURE = 0x1000
DDS_CAPS_MIPMAP = 0x400000
DDSD_CAPS = 0x1
DDSD_HEIGHT = 0x2
DDSD_WIDTH = 0x4
DDSD_PITCH = 0x8
DDSD_PIXELFORMAT = 0x1000
DDSD_MIPMAPCOUNT = 0x20000
DDSD_LINEARSIZE = 0x80000
DDPF_ALPHAPIXELS = 0x1
DDPF_ALPHA = 0x2
DDPF_FOURCC = 0x4
DDPF_RGB = 0x40
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


def read_tex_header(raw_data: bytes) -> tuple[int, int, int, int]:
    if len(raw_data) < TEX_HEADER_SIZE:
        raise ValueError("TEX file is smaller than the expected 80-byte header.")

    format_code = int.from_bytes(raw_data[4:8], byteorder="little", signed=False)
    width = int.from_bytes(raw_data[8:10], byteorder="little", signed=False)
    height = int.from_bytes(raw_data[10:12], byteorder="little", signed=False)
    mip_count = raw_data[14] & 0x0F
    if width <= 0 or height <= 0:
        raise ValueError("TEX file reported invalid dimensions.")
    if mip_count <= 0:
        mip_count = 1
    return format_code, width, height, mip_count


def build_dds_payload(format_code: int, width: int, height: int, mip_count: int, pixel_data: bytes) -> bytes:
    four_cc_map = {
        TEX_FORMAT_DXT1: b"DXT1",
        TEX_FORMAT_DXT3: b"DXT3",
        TEX_FORMAT_DXT5: b"DXT5",
    }
    four_cc = four_cc_map.get(format_code)
    if four_cc is None:
        raise ValueError(f"Unsupported TEX image format {format_code}.")

    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DDSD_LINEARSIZE
    linear_size = (width * height) // 2 if format_code == TEX_FORMAT_DXT1 else (width * height)
    caps = DDS_CAPS_TEXTURE | (DDS_CAPS_MIPMAP if mip_count > 1 else 0)
    header = bytearray()
    header += struct.pack("<I", DDS_MAGIC)
    header += struct.pack("<I", DDS_HEADER_SIZE)
    header += struct.pack("<I", flags)
    header += struct.pack("<I", height)
    header += struct.pack("<I", width)
    header += struct.pack("<I", linear_size)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", mip_count)
    header += bytes(44)
    header += struct.pack("<I", DDS_PIXEL_FORMAT_SIZE)
    header += struct.pack("<I", DDPF_FOURCC)
    header += four_cc
    header += struct.pack("<I", 0)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", caps)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", 0)
    header += struct.pack("<I", 0)
    return bytes(header) + pixel_data


def convert_tex_to_png(raw_tex_path: str, png_path: str) -> None:
    image_type, error = ensure_pillow_dependency()
    if image_type is None:
        raise RuntimeError(error or "Pillow is not available for TEX conversion.")

    with open(raw_tex_path, "rb") as handle:
        raw_data = handle.read()

    format_code, width, height, mip_count = read_tex_header(raw_data)
    pixel_data = raw_data[TEX_HEADER_SIZE:]
    if format_code == TEX_FORMAT_A8R8G8B8:
        required_bytes = width * height * 4
        if len(pixel_data) < required_bytes:
            raise ValueError(
                f"TEX pixel payload is truncated: expected {required_bytes} bytes, got {len(pixel_data)}."
            )
        image = image_type.frombytes("RGBA", (width, height), pixel_data[:required_bytes], "raw", "BGRA")
    else:
        dds_payload = build_dds_payload(format_code, width, height, mip_count, pixel_data)
        image = image_type.open(io.BytesIO(dds_payload))
        image.load()

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


def ensure_static_cache_copy(source_path: str, cache_relative_path: str) -> str:
    cache_path = os.path.normpath(os.path.join(CACHE_ROOT, cache_relative_path.replace("/", os.sep)))
    cache_root = os.path.normpath(CACHE_ROOT)
    if os.path.commonpath([cache_root, cache_path]) != cache_root:
        raise ValueError(f"Refusing to write cache asset outside {cache_root}: {cache_relative_path}")

    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
    if not os.path.isfile(cache_path) or os.path.getmtime(cache_path) < os.path.getmtime(source_path):
        shutil.copyfile(source_path, cache_path)

    return cache_path


def build_cache_url(cache_path: str) -> str:
    relative_path = os.path.relpath(cache_path, CACHE_ROOT).replace(os.sep, "/")
    version = int(os.path.getmtime(cache_path))
    return f"/assets/{relative_path}?v={version}"


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>TTSL Remote HUD</title>
<style>
:root{--panel:rgba(16,25,37,.95);--panel2:rgba(21,34,49,.98);--line:rgba(255,255,255,.08);--text:#eaf4ff;--muted:#93a7bc;--ok:#79e58d;--warn:#ffbf74;--bad:#ff7f7f;--accent:#87d7ff;--accent2:#79e58d;--tank:#78c5ff;--heal:#93f2a5;--dps:#ff9b7a;--util:#d5b7ff;--page:radial-gradient(circle at top left,rgba(135,215,255,.12),transparent 26%),linear-gradient(180deg,#071018,#0b1621 48%,#101925);--font-sans:"Segoe UI Variable Text","Segoe UI",Tahoma,sans-serif;--font-display:"Aptos Display","Trebuchet MS","Segoe UI",sans-serif;--shadow:0 22px 42px rgba(0,0,0,.26)}
*{box-sizing:border-box}body{margin:0;font-family:var(--font-sans);color:var(--text);background:var(--page)}
body[data-view-mode="operator"]{--page:radial-gradient(circle at 15% 0%,rgba(121,229,141,.14),transparent 24%),radial-gradient(circle at 85% 0%,rgba(135,215,255,.14),transparent 24%),linear-gradient(180deg,#061116,#0b1c23 48%,#10252e);--panel:rgba(10,23,29,.95);--panel2:rgba(14,31,40,.98);--line:rgba(121,229,141,.12);--accent:#85f2d5;--accent2:#9fd9ff}
body[data-view-mode="command"]{--page:radial-gradient(circle at 18% 0%,rgba(255,191,116,.16),transparent 24%),radial-gradient(circle at 85% 10%,rgba(255,155,122,.14),transparent 22%),linear-gradient(180deg,#16110b,#22180f 48%,#2c1d10);--panel:rgba(34,24,15,.95);--panel2:rgba(44,30,18,.98);--line:rgba(255,191,116,.16);--text:#fff4e8;--muted:#d4b59a;--accent:#ffc47a;--accent2:#ff9b7a;--font-display:"Georgia","Palatino Linotype",serif}
body[data-view-mode="matrix"]{--page:linear-gradient(180deg,rgba(146,255,172,.06),rgba(146,255,172,0) 18%),linear-gradient(180deg,#07110a,#0c1810 48%,#102116);--panel:rgba(11,21,14,.96);--panel2:rgba(14,28,19,.98);--line:rgba(146,255,172,.14);--text:#e9ffe9;--muted:#9ac7a2;--accent:#92ffac;--accent2:#7effdf;--font-display:"Bahnschrift","Segoe UI",sans-serif}
header{position:sticky;top:0;padding:12px 14px 10px;border-bottom:1px solid var(--line);background:rgba(7,16,24,.9);backdrop-filter:blur(14px);z-index:3}
.masthead{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;flex-wrap:wrap;margin-bottom:10px}.eyebrow{margin:0 0 4px;color:var(--accent);font-size:11px;letter-spacing:.14em;text-transform:uppercase}.headline-note{color:var(--muted);font-size:12px}.modebar{display:flex;gap:8px;flex-wrap:wrap}.modechip,.toolbar button,.controlrow button,.controlrow a,.opitem,.matrix-row{border:1px solid rgba(255,255,255,.14);background:color-mix(in srgb,var(--accent) 12%,transparent);color:var(--text);font:inherit;cursor:pointer;text-decoration:none;transition:transform .14s ease,background .14s ease,border-color .14s ease}.modechip{padding:6px 11px;border-radius:999px;font-weight:700}.modechip.active{background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 30%,transparent),color-mix(in srgb,var(--accent2) 18%,transparent));border-color:color-mix(in srgb,var(--accent) 52%,rgba(255,255,255,.14))}
h1{margin:0;font-size:27px;line-height:1;font-family:var(--font-display)}.statusbar{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px;margin-bottom:10px}.statuspill{min-height:44px;display:flex;align-items:center;padding:8px 12px;border-radius:999px;border:1px solid var(--line);background:rgba(255,255,255,.035);color:var(--muted);font-size:12px;line-height:1.25}
.toolbar{display:flex;flex-wrap:wrap;gap:8px 12px;color:var(--muted);font-size:11px;align-items:center}.toolbar label{display:inline-flex;align-items:center;gap:5px}.toolbar button{padding:5px 10px;border-radius:999px}.toolbar button:disabled{opacity:.45;cursor:not-allowed}.toolbar input[type="number"]{width:64px;padding:3px 7px;border-radius:999px;border:1px solid rgba(255,255,255,.14);background:rgba(255,255,255,.05);color:var(--text);font:inherit}
main{padding:12px;display:grid;gap:12px;align-items:start}.layout-classic{grid-template-columns:repeat(auto-fit,minmax(250px,1fr))}.card,.overviewpanel,.operator-rail,.operator-detail,.matrixpane{display:grid;gap:8px;padding:10px;border-radius:14px;background:linear-gradient(180deg,var(--panel),var(--panel2));border:1px solid var(--line);box-shadow:var(--shadow)}
.head{display:flex;justify-content:space-between;gap:8px;align-items:flex-start;flex-wrap:wrap}.name{font-weight:700;font-size:15px;line-height:1.15}.zone,.sub,.foot,.hint{font-size:10px;color:var(--muted);line-height:1.35}.badges,.states,.ident{display:flex;flex-wrap:wrap;gap:5px}.ident{align-items:center}.badge,.state{padding:3px 7px;border-radius:999px;font-size:11px;font-weight:700;border:1px solid transparent}
.badge.ok,.state.on{color:var(--ok);background:rgba(121,229,141,.14);border-color:rgba(121,229,141,.22)}.badge.warn,.state.warn{color:var(--warn);background:rgba(255,191,116,.12);border-color:rgba(255,191,116,.22)}.badge.bad,.state.bad{color:var(--bad);background:rgba(255,127,127,.12);border-color:rgba(255,127,127,.22)}.badge.tank{color:var(--tank);background:rgba(120,197,255,.12);border-color:rgba(120,197,255,.22)}.badge.heal{color:var(--heal);background:rgba(147,242,165,.12);border-color:rgba(147,242,165,.22)}.badge.dps{color:var(--dps);background:rgba(255,155,122,.12);border-color:rgba(255,155,122,.22)}.badge.util{color:var(--util);background:rgba(213,183,255,.12);border-color:rgba(213,183,255,.22)}.state.off{color:#627385;background:rgba(255,255,255,.04);border-color:rgba(255,255,255,.06)}
.meta{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:5px}.meta.wide{grid-template-columns:repeat(3,minmax(0,1fr))}.tile{padding:6px;border-radius:9px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.04)}.label{font-size:9px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);margin-bottom:2px}.value{font-size:11px;font-weight:600;line-height:1.25;word-break:break-word}.value.bad{color:var(--bad)}
.section{display:grid;gap:5px;padding:8px;border-radius:11px;background:rgba(255,255,255,.025);border:1px solid rgba(255,255,255,.04)}.section.tight{padding:7px}.sectionhead{font-size:10px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}.facts{display:grid;gap:5px}.factrow{display:grid;grid-template-columns:78px minmax(0,1fr);gap:7px;padding-bottom:4px;border-bottom:1px solid rgba(255,255,255,.05)}.factrow:last-child{padding-bottom:0;border-bottom:none}.factlabel{color:var(--muted);font-size:9px;letter-spacing:.08em;text-transform:uppercase}.factvalue.bad{color:var(--bad)}
.party{display:grid;gap:3px}.member{display:grid;grid-template-columns:20px minmax(0,1fr) 42px 46px;gap:4px;align-items:center;padding:4px 6px;border-radius:8px;background:rgba(255,255,255,.035);font-size:11px}.slot,.job,.hp,.dist{text-align:right;color:var(--muted)}.membername{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.controls{display:grid;gap:6px}.controlrow{display:flex;gap:6px;align-items:center;flex-wrap:wrap}.controlrow input{flex:1 1 180px;padding:5px 9px;border-radius:999px;border:1px solid rgba(255,255,255,.14);background:rgba(255,255,255,.05);color:var(--text);font:inherit}.controlrow button,.controlrow a{padding:5px 9px;border-radius:999px}.controlrow button:disabled{opacity:.45;cursor:not-allowed}.controlnote{font-size:10px;color:var(--muted)}
.radarbox{display:grid;justify-items:center;gap:3px}canvas{display:block;max-width:100%;aspect-ratio:1/1;background:rgba(6,10,16,.92);border:1px solid var(--line);border-radius:12px}.iconimg{width:18px;height:18px;border-radius:4px;border:1px solid var(--line);background:rgba(255,255,255,.04);object-fit:cover}.mapframe{position:relative;max-width:100%;aspect-ratio:1/1;overflow:hidden;border-radius:12px;border:1px solid var(--line);background:rgba(6,10,16,.92)}.mapimg{position:absolute;display:block;max-width:none;max-height:none}.mapoverlay{position:absolute;inset:0;width:100%;height:100%;pointer-events:none;background:transparent;border:none}
.aggmembers{display:grid;gap:4px}.aggmember{display:grid;gap:4px;padding:6px;border-radius:9px;background:rgba(255,255,255,.035);border:1px solid rgba(255,255,255,.04)}.aggmember.stranger{border-color:rgba(255,127,127,.18)}.aggmain{display:flex;justify-content:space-between;gap:6px;align-items:flex-start;flex-wrap:wrap}.aggname{display:flex;align-items:center;gap:5px;min-width:0;flex-wrap:wrap}.aggname .slot,.aggname .job,.aggname .lvl{color:var(--muted);font-size:10px;font-weight:700}.aggname .membername{font-size:12px;font-weight:700;line-height:1.1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:220px}.aggmeta{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:4px}.aggnote{font-size:10px;color:var(--muted)}.aggnote.bad{color:var(--bad)}
.empty{padding:20px;text-align:center;color:var(--muted);background:rgba(16,25,37,.84);border:1px dashed rgba(255,255,255,.14);border-radius:12px}.overviewgrid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:8px}.overviewcard{padding:9px;border-radius:10px;background:rgba(255,255,255,.04);border:1px solid rgba(255,255,255,.04)}.overviewvalue{font-size:20px;font-family:var(--font-display);line-height:1}.overviewnote{color:var(--muted);font-size:10px;line-height:1.3;margin-top:4px}
.operator-shell{display:grid;grid-template-columns:minmax(280px,340px) minmax(0,1fr);gap:12px}.operator-rail,.operator-detail{align-content:start}.oplist{display:grid;gap:8px}.opitem{width:100%;text-align:left;padding:9px 10px;border-radius:11px}.opitem.active{background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 18%,transparent),rgba(255,255,255,.04));border-color:color-mix(in srgb,var(--accent) 48%,rgba(255,255,255,.14))}.oprow{display:flex;justify-content:space-between;gap:8px;align-items:center;flex-wrap:wrap}.opname{font-weight:700;font-size:13px}.opsub,.opmeta{color:var(--muted);font-size:10px;line-height:1.35}
.command-shell{display:grid;gap:12px}.command-columns{display:grid;grid-template-columns:minmax(0,1.6fr) minmax(280px,.9fr);gap:12px}.command-stage,.command-side{display:grid;gap:12px}.compactgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px}.party-stage{border-color:color-mix(in srgb,var(--accent) 30%,var(--line))}
.matrix-shell{display:grid;gap:12px}.matrix-layout{display:grid;grid-template-columns:minmax(0,1.1fr) minmax(320px,.9fr);gap:12px}.matrixtable{display:grid;gap:6px}.matrixhead,.matrix-row{display:grid;grid-template-columns:72px minmax(150px,1.3fr) minmax(110px,1fr) 90px 110px 100px 92px 78px 90px;gap:8px;align-items:center}.matrixhead{padding:8px 10px;border-radius:10px;background:rgba(255,255,255,.03);color:var(--muted);font-size:10px;letter-spacing:.08em;text-transform:uppercase}.matrix-row{width:100%;text-align:left;padding:9px 10px;border-radius:10px}.matrix-row.active{background:linear-gradient(135deg,color-mix(in srgb,var(--accent) 15%,transparent),rgba(255,255,255,.04));border-color:color-mix(in srgb,var(--accent) 48%,rgba(255,255,255,.14))}.matrixcell{min-width:0;font-size:11px;line-height:1.25;word-break:break-word}.matrixcell.mono{font-family:Consolas,"Courier New",monospace}.kindtag{display:inline-flex;align-items:center;justify-content:center;padding:3px 7px;border-radius:999px;background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.06);font-size:10px;font-weight:700;text-transform:uppercase}
@media (max-width:1180px){.statusbar,.overviewgrid,.operator-shell,.command-columns,.matrix-layout{grid-template-columns:1fr}}
@media (max-width:900px){.aggmeta,.meta.wide{grid-template-columns:repeat(2,minmax(0,1fr))}.aggname .membername{max-width:none}.matrixhead{display:none}.matrix-row{grid-template-columns:repeat(2,minmax(0,1fr))}.matrixcell::before{content:attr(data-label);display:block;color:var(--muted);font-size:9px;letter-spacing:.08em;text-transform:uppercase;margin-bottom:2px}}
@media (max-width:720px){header{padding:10px 12px 8px}main,.layout-classic{grid-template-columns:1fr}.statusbar,.overviewgrid,.meta,.meta.wide,.aggmeta,.compactgrid{grid-template-columns:1fr}.factrow{grid-template-columns:1fr;gap:3px}}
</style></head><body>
<header><div class="masthead"><div><div class="eyebrow">Remote Monitor + Command Relay</div><h1>TTSL Remote HUD</h1><div class="headline-note">Four layouts for 4-12 clients: classic cards, operator board, party command board, and dense matrix.</div></div><div class="modebar"><button class="modechip" type="button" data-mode="classic">Classic</button><button class="modechip" type="button" data-mode="operator">Operator</button><button class="modechip" type="button" data-mode="command">Command</button><button class="modechip" type="button" data-mode="matrix">Matrix</button></div></div><div class="statusbar"><div id="summary" class="statuspill">Waiting for clients...</div><div id="stamp" class="statuspill">No updates yet.</div><div id="assetPlan" class="statuspill">Asset plan pending.</div><div id="extractStatus" class="statuspill">Extraction idle.</div></div><div class="toolbar"><button id="extractAssets" type="button">Extract Assets</button><label><input id="krangle" type="checkbox"> Krangle names/account IDs</label><label><input id="krangleEnemies" type="checkbox"> Krangle enemy names</label><label><input id="showStale" type="checkbox" checked> Show stale/disconnected</label><label><input id="aggregateParties" type="checkbox"> Aggregate parties</label><label><input id="icons" type="checkbox" checked> Icons</label><label><input id="enumerate" type="checkbox"> Enumerate</label><label>Box px <input id="mapBoxPx" type="number" min="96" max="320" step="4" value="160"></label><label>Combat W <input id="combatWidth" type="number" min="5" max="300" step="1" value="20"></label><label>Combat H <input id="combatHeight" type="number" min="5" max="300" step="1" value="20"></label><label>Travel W <input id="travelWidth" type="number" min="5" max="500" step="1" value="50"></label><label>Travel H <input id="travelHeight" type="number" min="5" max="500" step="1" value="50"></label></div></header>
<main id="app" class="layout-operator"><div class="empty">No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.</div></main>
<script>
const app=document.getElementById("app"),summary=document.getElementById("summary"),stamp=document.getElementById("stamp"),assetPlan=document.getElementById("assetPlan"),extractStatus=document.getElementById("extractStatus"),extractAssets=document.getElementById("extractAssets"),krangle=document.getElementById("krangle"),krangleEnemies=document.getElementById("krangleEnemies"),showStale=document.getElementById("showStale"),aggregateParties=document.getElementById("aggregateParties"),icons=document.getElementById("icons"),enumerate=document.getElementById("enumerate"),mapBoxPxInput=document.getElementById("mapBoxPx"),combatWidthInput=document.getElementById("combatWidth"),combatHeightInput=document.getElementById("combatHeight"),travelWidthInput=document.getElementById("travelWidth"),travelHeightInput=document.getElementById("travelHeight"),layoutButtons=[...document.querySelectorAll(".modechip")];
const UI_STORAGE_PREFIX="ttslhud.",DEFAULT_LAYOUT_MODE="operator",DEFAULT_MAP_BOX_PX=160,DEFAULT_COMBAT_WIDTH_YALMS=20,DEFAULT_COMBAT_HEIGHT_YALMS=20,DEFAULT_TRAVEL_WIDTH_YALMS=50,DEFAULT_TRAVEL_HEIGHT_YALMS=50,LAYOUT_MODES=new Set(["classic","operator","command","matrix"]);
const tankJobs=new Set(["GLA","MRD","PLD","WAR","DRK","GNB"]),healJobs=new Set(["CNJ","WHM","SCH","AST","SGE"]),dpsJobs=new Set(["PGL","LNC","ROG","ARC","THM","ACN","MNK","DRG","NIN","SAM","RPR","VPR","BRD","MCH","DNC","BLM","SMN","RDM","PCT","BLU"]);
let currentAssetCatalog={jobIcons:{},maps:{},raceIcons:{},tribeIcons:{},warnings:[]},currentLayoutMode=DEFAULT_LAYOUT_MODE,selectedEntityKey="";
const remoteControlDrafts=new Map();
const clampNumber=(value,min,max,fallback)=>{const parsed=Number(value);return Number.isFinite(parsed)?Math.max(min,Math.min(max,parsed)):fallback};
function loadNumericPreference(key,fallback,min,max){try{const stored=window.localStorage.getItem(`${UI_STORAGE_PREFIX}${key}`);return clampNumber(stored,min,max,fallback)}catch{return fallback}}
function loadStringPreference(key,fallback,allowed=null){try{const stored=String(window.localStorage.getItem(`${UI_STORAGE_PREFIX}${key}`)||"").trim();if(!stored)return fallback;return allowed&&!allowed.has(stored)?fallback:stored}catch{return fallback}}
function persistNumericPreference(key,value){try{window.localStorage.setItem(`${UI_STORAGE_PREFIX}${key}`,String(value))}catch{}}
function persistStringPreference(key,value){try{window.localStorage.setItem(`${UI_STORAGE_PREFIX}${key}`,String(value))}catch{}}
function wireNumericPreference(input,key,fallback,min,max){const apply=()=>{const value=clampNumber(input.value,min,max,fallback);input.value=String(value);persistNumericPreference(key,value);refresh()};input.value=String(loadNumericPreference(key,fallback,min,max));input.addEventListener("change",apply);input.addEventListener("input",apply)}
function applyLayoutMode(mode){currentLayoutMode=LAYOUT_MODES.has(mode)?mode:DEFAULT_LAYOUT_MODE;document.body.dataset.viewMode=currentLayoutMode;app.className=`layout-${currentLayoutMode}`;for(const button of layoutButtons)button.classList.toggle("active",button.dataset.mode===currentLayoutMode);persistStringPreference("layoutMode",currentLayoutMode)}
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
function tile(label,value,kind="",title=""){const el=document.createElement("div");el.className="tile";if(title)el.title=title;el.innerHTML=`<div class="label">${label}</div><div class="value ${kind}">${value}</div>`;return el}
const formatAge=value=>typeof value==="number"&&Number.isFinite(value)?`${value.toFixed(1)}s`:"--";
const pathLeaf=value=>{const normalized=String(value||"").trim().replace(/\\\\/g,"/");if(!normalized)return"Unavailable";const parts=normalized.split("/").filter(Boolean);return parts.length>=2?parts.slice(-2).join("/"):parts[0]};
const repairText=repair=>!repair?"Unavailable":`${repair.minCondition}% min | ${repair.averageCondition}% avg | ${repair.equippedCount??0} slots`;
const policyText=policy=>{const bits=[];if(policy?.allowEchoCommands)bits.push("Text");if(policy?.allowScreenshotRequests)bits.push("Screens");return bits.length>0?bits.join(" + "):"Locked"};
const queueStateText=entity=>entity?.conditions?.boundByDuty?"In duty":entity?.conditions?.waitingForDuty?"Queued":entity?.conditions?.inCombat?"Combat":"Travel";
const clientStatusText=client=>client.isDisconnected?"Disconnected":client.stale?"Stale":"Live";
const clientStatusKind=client=>client.isDisconnected?"bad":client.stale?"warn":"ok";
const clientKey=client=>`client|${String(client?.accountId||"").trim()}|${String(client?.characterName||"").trim()}|${String(client?.worldName||"").trim()}`;
const partyKey=party=>`party|${String(party?.sourceAccountId||"").trim()}|${String(party?.sourceCharacterName||"").trim()}|${String(party?.sourceWorldName||"").trim()}`;
function selectEntity(key){selectedEntityKey=String(key||"");persistStringPreference("selectedEntity",selectedEntityKey);refresh()}
function overviewCard(label,value,note){const card=document.createElement("div");card.className="overviewcard";card.innerHTML=`<div class="label">${label}</div><div class="overviewvalue">${value}</div><div class="overviewnote">${note}</div>`;return card}
function factSection(title,rows){const section=document.createElement("div");section.className="section tight";section.innerHTML=`<div class="sectionhead">${title}</div>`;const facts=document.createElement("div");facts.className="facts";for(const row of rows){const wrap=document.createElement("div");wrap.className="factrow";const label=document.createElement("div");label.className="factlabel";label.textContent=row.label;const value=document.createElement("div");value.className=`factvalue ${row.kind||""}`.trim();value.textContent=row.value;if(row.title)value.title=row.title;wrap.append(label,value);facts.appendChild(wrap)}section.appendChild(facts);return section}
function jobIconAsset(jobIconId){return jobIconId==null?null:(currentAssetCatalog.jobIcons||{})[String(jobIconId)]||null}
function raceIconAsset(raceId){return raceId==null?null:(currentAssetCatalog.raceIcons||{})[String(raceId)]||null}
function tribeIconAsset(tribeId){return tribeId==null?null:(currentAssetCatalog.tribeIcons||{})[String(tribeId)]||null}
function assetUrl(asset){return asset?.pngUrl||asset?.svgUrl||null}
function localizedAssetName(asset,gender){if(!asset)return"";if(gender===1&&asset.feminineName)return String(asset.feminineName);if(asset.masculineName)return String(asset.masculineName);if(asset.feminineName)return String(asset.feminineName);return""}
function mapAsset(map){if(!map)return null;const catalogMaps=currentAssetCatalog.maps||{};const candidates=[];if(map.texturePath)candidates.push(String(map.texturePath));for(const candidate of map.texturePathCandidates||[]){const text=String(candidate||"");if(text&&!candidates.includes(text))candidates.push(text)}for(const candidate of candidates){const key=`texture:${candidate.replace(/\\\\/g,"/").trim().toLowerCase()}`;if(catalogMaps[key])return catalogMaps[key]}if(map.mapId!=null){const fallback=Object.values(catalogMaps).find(entry=>Number(entry?.mapId)===Number(map.mapId));if(fallback)return fallback}return null}
function currentViewportSettings(inCombat){return{boxPx:clampNumber(mapBoxPxInput.value,96,320,DEFAULT_MAP_BOX_PX),widthYalms:clampNumber(inCombat?combatWidthInput.value:travelWidthInput.value,5,500,inCombat?DEFAULT_COMBAT_WIDTH_YALMS:DEFAULT_TRAVEL_WIDTH_YALMS),heightYalms:clampNumber(inCombat?combatHeightInput.value:travelHeightInput.value,5,500,inCombat?DEFAULT_COMBAT_HEIGHT_YALMS:DEFAULT_TRAVEL_HEIGHT_YALMS)}}
function aggregatePartyInCombat(party){const source=Array.isArray(party?.members)?party.members.find(member=>member.isSource)||party.members.find(member=>!member.isStranger):null;return !!source?.conditions?.inCombat}
function mapVisibleCoordinate(value,offset,sizeFactor){if(value==null||offset==null||sizeFactor==null||sizeFactor===0)return null;const scale=Number(sizeFactor)/100;return(41/scale)*(((Number(value)+Number(offset))*scale+1024)/2048)+1}
function mapTextureCoordinate(value,offset,sizeFactor){if(value==null||offset==null||sizeFactor==null||sizeFactor===0)return null;const scale=Number(sizeFactor)/100;return Math.max(0,Math.min(1,(((Number(value)+Number(offset))*scale+1024)/2048)))}
function buildMapMarker(position,map){if(!position||!map)return null;const leftUnit=mapTextureCoordinate(position.x,map.offsetX,map.sizeFactor),topUnit=mapTextureCoordinate(position.z,map.offsetY,map.sizeFactor),mapX=mapVisibleCoordinate(position.x,map.offsetX,map.sizeFactor),mapY=mapVisibleCoordinate(position.z,map.offsetY,map.sizeFactor);if(leftUnit==null||topUnit==null)return null;return{left:leftUnit*100,top:topUnit*100,x:mapX,y:mapY}}
function buildMapViewport(position,map,widthYalms,heightYalms){const marker=buildMapMarker(position,map);if(!marker)return{marker:null};const halfWidth=Math.max(.5,Number(widthYalms||0)/2),halfHeight=Math.max(.5,Number(heightYalms||0)/2),leftUnit=mapTextureCoordinate(Number(position.x)-halfWidth,map.offsetX,map.sizeFactor),rightUnit=mapTextureCoordinate(Number(position.x)+halfWidth,map.offsetX,map.sizeFactor),topUnit=mapTextureCoordinate(Number(position.z)-halfHeight,map.offsetY,map.sizeFactor),bottomUnit=mapTextureCoordinate(Number(position.z)+halfHeight,map.offsetY,map.sizeFactor);if(leftUnit==null||rightUnit==null||topUnit==null||bottomUnit==null)return{marker};const leftPct=Math.max(0,Math.min(100,Math.min(leftUnit,rightUnit)*100)),rightPct=Math.max(0,Math.min(100,Math.max(leftUnit,rightUnit)*100)),topPct=Math.max(0,Math.min(100,Math.min(topUnit,bottomUnit)*100)),bottomPct=Math.max(0,Math.min(100,Math.max(topUnit,bottomUnit)*100)),viewWidthPct=Math.max(.5,rightPct-leftPct),viewHeightPct=Math.max(.5,bottomPct-topPct),scaleX=Math.max(1,100/viewWidthPct),scaleY=Math.max(1,100/viewHeightPct),markerU=marker.left/100,markerV=marker.top/100,offsetX=Math.max(0,Math.min(1-(1/scaleX),markerU-(.5/scaleX))),offsetY=Math.max(0,Math.min(1-(1/scaleY),markerV-(.5/scaleY))),dotLeft=Math.max(0,Math.min(100,(markerU-offsetX)*scaleX*100)),dotTop=Math.max(0,Math.min(100,(markerV-offsetY)*scaleY*100));return{marker,imageWidthPercent:scaleX*100,imageHeightPercent:scaleY*100,imageLeftPercent:-offsetX*scaleX*100,imageTopPercent:-offsetY*scaleY*100,dotLeftPercent:dotLeft,dotTopPercent:dotTop,scaleX,scaleY,offsetXUnit:offsetX,offsetYUnit:offsetY}}
function renderIdentity(entity){const wrap=document.createElement("div");wrap.className="ident";if(!icons.checked)return wrap;const appendIcon=(asset,label)=>{const url=assetUrl(asset);if(!url)return;const img=document.createElement("img");img.className="iconimg";img.src=url;img.alt=label;img.title=label;wrap.appendChild(img)};const jobAsset=jobIconAsset(entity.jobIconId);if(jobAsset)appendIcon(jobAsset,entity.job||`Job ${entity.jobIconId}`);const ancestryAsset=tribeIconAsset(entity.tribeId)||raceIconAsset(entity.raceId);const ancestryName=localizedAssetName(ancestryAsset,entity.gender);if(ancestryAsset&&ancestryName)appendIcon(ancestryAsset,ancestryName);if(entity.job)wrap.appendChild(chip(entity.job,jobKind(entity.job)));if(entity.level!=null)wrap.appendChild(chip(`Lv ${entity.level}`,"util"));if(entity.gender!=null)wrap.appendChild(chip(genderSymbol(entity.gender),"util"));return wrap}
function buildEnemyPoints(combat){return Array.isArray(combat?.hostiles)?combat.hostiles.filter(enemy=>enemy.position).map((enemy,index)=>({position:enemy.position,color:enemy.isCurrentTarget?"#ff5e7d":enemy.isTargetingTrackedParty?"#ff9b7a":"#ff7f7f",label:enemy.isCurrentTarget?"TGT":`E${index+1}`})):[]}
function drawFacingCone(ctx,x,y,rotation,color,size){
  if(typeof rotation!=="number"||!Number.isFinite(rotation))return;
  const facing=rotation,dirX=Math.sin(facing),dirY=Math.cos(facing),shaft=size*.95,tip=size*1.45,wing=size*.55,base=size*.2;
  ctx.save();
  ctx.strokeStyle="rgba(7,16,24,.95)";
  ctx.lineWidth=4;
  ctx.beginPath();
  ctx.moveTo(x,y);
  ctx.lineTo(x+dirX*shaft,y+dirY*shaft);
  ctx.stroke();
  ctx.strokeStyle=color;
  ctx.lineWidth=2.25;
  ctx.beginPath();
  ctx.moveTo(x,y);
  ctx.lineTo(x+dirX*shaft,y+dirY*shaft);
  ctx.stroke();
  ctx.fillStyle=color;
  ctx.globalAlpha=.92;
  ctx.beginPath();
  ctx.moveTo(x+dirX*base,y+dirY*base);
  ctx.lineTo(x+dirX*tip+dirY*wing,y+dirY*tip-dirX*wing);
  ctx.lineTo(x+dirX*tip-dirY*wing,y+dirY*tip+dirX*wing);
  ctx.closePath();
  ctx.fill();
  ctx.strokeStyle="rgba(7,16,24,.95)";
  ctx.lineWidth=1.4;
  ctx.stroke();
  ctx.restore()
}
function drawRadarBase(canvas,points,origin,labeler,widthYalms,heightYalms){const ctx=canvas.getContext("2d"),w=canvas.width,h=canvas.height,cx=w/2,cy=h/2,r=w/2-16,halfWidth=Math.max(1,Number(widthYalms)/2),halfHeight=Math.max(1,Number(heightYalms)/2);ctx.clearRect(0,0,w,h);ctx.fillStyle="#071018";ctx.fillRect(0,0,w,h);ctx.strokeStyle="rgba(255,255,255,.12)";ctx.strokeRect(9,9,w-18,h-18);ctx.beginPath();ctx.moveTo(cx,14);ctx.lineTo(cx,h-14);ctx.moveTo(14,cy);ctx.lineTo(w-14,cy);ctx.stroke();drawFacingCone(ctx,cx,cy,origin?.rotation,"#79e58d",17);ctx.fillStyle="#79e58d";ctx.beginPath();ctx.arc(cx,cy,4.5,0,Math.PI*2);ctx.fill();if(!origin||points.length===0){ctx.fillStyle="#93a7bc";ctx.font="11px Segoe UI";ctx.fillText("No radar data",34,cy+4);return}for(const point of points){if(!point.position)continue;const dx=point.position.x-origin.x,dz=point.position.z-origin.z,px=cx+Math.max(-1,Math.min(1,dx/halfWidth))*r,py=cy+Math.max(-1,Math.min(1,dz/halfHeight))*r;drawFacingCone(ctx,px,py,point.position.rotation,point.color,14);ctx.fillStyle=point.color;ctx.beginPath();ctx.arc(px,py,4.2,0,Math.PI*2);ctx.fill();ctx.fillStyle="#eaf4ff";ctx.font="10px Segoe UI";ctx.fillText(labeler(point),px+6,py+3)}}
function sameTrackedPosition(left,right){return !!left&&!!right&&Math.abs(Number(left.x)-Number(right.x))<.05&&Math.abs(Number(left.z)-Number(right.z))<.05}
function buildClientMinimapPoints(client){const points=[];for(const member of client.party||[]){if(!member.position||sameTrackedPosition(member.position,client.position))continue;points.push({position:member.position,color:"#ffbf74",label:shortLabel(member.name,member.slot,client.worldName),rotation:member.position.rotation,size:12})}for(const enemy of buildEnemyPoints(client.combat))points.push({...enemy,rotation:enemy.position?.rotation,size:11});return points}
function buildAggregateMinimapPoints(party,source){const points=[];for(const member of party.members||[]){if(member===source||!member.position||sameTrackedPosition(member.position,source?.position))continue;points.push({position:member.position,color:member.isStranger?"#ff7f7f":member.isSubmitting?"#ffbf74":"#93a7bc",label:shortLabel(member.name,member.slotText,member.worldName),rotation:member.position.rotation,size:member.isStranger?11:12})}for(const enemy of buildEnemyPoints(party.combat))points.push({...enemy,rotation:enemy.position?.rotation,size:11});return points}
function projectMarkerToViewport(marker,mapViewport){if(!marker||!mapViewport?.marker||mapViewport.scaleX==null||mapViewport.scaleY==null)return null;const markerU=marker.left/100,markerV=marker.top/100;return{left:Math.max(0,Math.min(100,(markerU-mapViewport.offsetXUnit)*mapViewport.scaleX*100)),top:Math.max(0,Math.min(100,(markerV-mapViewport.offsetYUnit)*mapViewport.scaleY*100)),x:marker.x,y:marker.y}}
function drawMinimapOverlay(canvas,map,mapViewport,sourcePosition,points,sourceLabel){const ctx=canvas.getContext("2d"),width=canvas.width,height=canvas.height;ctx.clearRect(0,0,width,height);const drawPoint=(point,color,label,size)=>{const projected=projectMarkerToViewport(buildMapMarker(point.position,map),mapViewport);if(!projected)return;const px=width*(projected.left/100),py=height*(projected.top/100);drawFacingCone(ctx,px,py,point.rotation??point.position?.rotation,color,size);ctx.fillStyle=color;ctx.beginPath();ctx.arc(px,py,Math.max(3.6,size*.28),0,Math.PI*2);ctx.fill();ctx.strokeStyle="rgba(7,16,24,.95)";ctx.lineWidth=1.4;ctx.stroke();if(label){ctx.fillStyle="#eaf4ff";ctx.strokeStyle="rgba(7,16,24,.95)";ctx.lineWidth=2.8;ctx.font="10px Segoe UI";ctx.strokeText(label,px+7,py+4);ctx.fillText(label,px+7,py+4)}};for(const point of points||[])if(point?.position)drawPoint(point,point.color||"#ffbf74",point.label||"",point.size||11);if(sourcePosition)drawPoint({position:sourcePosition,rotation:sourcePosition.rotation},"#79e58d",sourceLabel||"",14)}
function drawRadar(canvas,client){const viewport=currentViewportSettings(!!client?.conditions?.inCombat);canvas.width=viewport.boxPx;canvas.height=viewport.boxPx;if(!client.position||!Array.isArray(client.party)||client.party.length===0){const hostiles=buildEnemyPoints(client.combat);if(hostiles.length===0){drawRadarBase(canvas,[],null,()=>"-",viewport.widthYalms,viewport.heightYalms);return}drawRadarBase(canvas,hostiles,client.position||null,point=>point.label,viewport.widthYalms,viewport.heightYalms);return}const points=client.party.filter(m=>m.position).map(m=>({position:m.position,color:"#ffbf74",slot:m.slot,name:m.name,world:client.worldName}));drawRadarBase(canvas,points.concat(buildEnemyPoints(client.combat)),client.position,point=>point.label||shortLabel(point.name,point.slot,point.world),viewport.widthYalms,viewport.heightYalms)}
function drawAggregateRadar(canvas,party){const viewport=currentViewportSettings(aggregatePartyInCombat(party));canvas.width=viewport.boxPx;canvas.height=viewport.boxPx;const source=party.members.find(m=>m.isSource&&m.position)||party.members.find(m=>m.position&&!m.isStranger)||null;if(!source){drawRadarBase(canvas,buildEnemyPoints(party.combat),null,point=>point.label,viewport.widthYalms,viewport.heightYalms);return}const points=party.members.filter(m=>m.position&&m!==source).map(m=>({position:m.position,color:m.isStranger?"#ff7f7f":m.isSubmitting?"#ffbf74":"#93a7bc",slot:m.slotText,name:m.name,world:m.worldName}));drawRadarBase(canvas,points.concat(buildEnemyPoints(party.combat)),source.position,point=>point.label||shortLabel(point.name,point.slot,point.world),viewport.widthYalms,viewport.heightYalms)}
function renderParty(client){const wrap=document.createElement("div");wrap.className="party";if(Array.isArray(client.party)&&client.party.length>0){for(const m of client.party){const row=document.createElement("div");row.className="member";const dist=typeof m.distance==="number"?`${m.distance.toFixed(1)}y`:"--";row.innerHTML=`<div class="slot">${m.slot}</div><div class="membername">${displayName(m.name,m.krangledName)}</div><div class="job">${m.job}</div><div class="dist">${dist}</div>`;row.title=`${levelText(m.level)} | HP ${hpText(m.currentHp,m.maxHp)} | MP ${mpText(m.currentMp,m.maxMp)}`;wrap.appendChild(row)}}else{const row=document.createElement("div");row.className="member";row.innerHTML=`<div class="slot">-</div><div class="membername">No party data captured yet.</div><div class="job">--</div><div class="dist">--</div>`;wrap.appendChild(row)}return wrap}
function renderStates(client){const wrap=document.createElement("div");wrap.className="states";wrap.append(stateChip("Combat",!!client.conditions?.inCombat),stateChip("Duty",!!client.conditions?.boundByDuty),stateChip("Queue",!!client.conditions?.waitingForDuty),stateChip("Mount",!!client.conditions?.mounted),stateChip("Cast",!!client.conditions?.casting),stateChip("Dead",!!client.conditions?.dead,client.conditions?.dead?"bad":"off"));return wrap}
function combatHeadline(combat){const target=combat?.currentTarget;if(!target)return"No current target";const name=displayEnemyName(target.name,target.krangledName);const targetText=target.isTargetingLocalPlayer?"targeting you":target.isTargetingTrackedParty?`targeting ${displayName(target.targetName||"party",target.krangledTargetName||"")}`:target.targetName?`targeting ${displayName(target.targetName,target.krangledTargetName)}`:"no tracked target";const castText=target.isCasting?` | cast ${target.castActionId??"?"} ${target.castTimeRemaining?.toFixed(1)??"?"}s`:"";return`${name} | ${targetText}${castText}`}
function renderClientTelemetry(client){return factSection("Telemetry",[{label:"Connected",value:client.connectedAtUtc||"Unknown"},{label:"Last update",value:`${client.lastSeenUtc||"Unknown"} | ${client.updateKind||"full"}`},{label:"Host",value:client.hostName||"Unknown host",kind:client.hostName?"":"bad"},{label:"Game path",value:pathLeaf(client.gameInstallPath),title:client.gameInstallPath||"",kind:client.gameInstallPath?"":"bad"},{label:"Labels",value:client.enumeratePartyMembers?"Slot numbers":"Names"},{label:"Controls",value:policyText(client.policy)},{label:"Queue",value:queueStateText(client)},{label:"Focus",value:combatHeadline(client.combat),title:combatHeadline(client.combat)}])}
function renderThreats(combat){const section=document.createElement("div");section.className="section";section.innerHTML=`<div class="sectionhead">Threat</div>`;const list=document.createElement("div");list.className="party";const hostiles=[];if(combat?.currentTarget)hostiles.push(combat.currentTarget);for(const hostile of combat?.hostiles||[]){if(!hostiles.some(existing=>existing.dataId===hostile.dataId&&existing.distance===hostile.distance&&existing.name===hostile.name))hostiles.push(hostile)}if(hostiles.length===0){const row=document.createElement("div");row.className="member";row.innerHTML=`<div class="slot">-</div><div class="membername">No combat telemetry captured.</div><div class="job">--</div><div class="dist">--</div>`;list.appendChild(row);section.appendChild(list);return section}for(const hostile of hostiles){const row=document.createElement("div");row.className="member";const dist=typeof hostile.distance==="number"?`${hostile.distance.toFixed(1)}y`:"--";const label=hostile.isCurrentTarget?"T":hostile.isTargetingTrackedParty?"A":"E";const hp=pct(hostile.currentHp,hostile.maxHp);row.innerHTML=`<div class="slot">${label}</div><div class="membername">${displayEnemyName(hostile.name,hostile.krangledName)}</div><div class="job">${dist}</div><div class="dist">${hp}</div>`;row.title=`${hostile.isCurrentTarget?"Current target":hostile.isTargetingLocalPlayer?"Targeting you":hostile.isTargetingTrackedParty?`Targeting ${displayName(hostile.targetName||"party",hostile.krangledTargetName||"")}`:hostile.targetName?`Targeting ${displayName(hostile.targetName,hostile.krangledTargetName)}`:"No tracked target"} | ${hostile.isCasting?`Cast ${hostile.castActionId??"?"} | ${hostile.castTimeRemaining?.toFixed(1)??"?"}s`:"Not casting"}`;list.appendChild(row)}section.appendChild(list);return section}
function renderMinimapSection(map,position,title="Minimap",inCombat=false,points=[],sourceLabel=""){
  const section=document.createElement("div");
  section.className="section";
  section.innerHTML=`<div class="sectionhead">${title}</div>`;

  const viewport=currentViewportSettings(inCombat);
  const asset=mapAsset(map);
  const mapViewport=buildMapViewport(position,map,viewport.widthYalms,viewport.heightYalms);

  if(asset?.pngUrl&&mapViewport.marker){
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
    const overlay=document.createElement("canvas");
    overlay.className="mapoverlay";
    overlay.width=viewport.boxPx;
    overlay.height=viewport.boxPx;
    frame.appendChild(overlay);

    section.appendChild(frame);
    requestAnimationFrame(()=>drawMinimapOverlay(overlay,map,mapViewport,position,points,sourceLabel));
  }else if(position||points.length>0){
    const fallback=document.createElement("canvas");
    fallback.width=viewport.boxPx;
    fallback.height=viewport.boxPx;
    section.appendChild(fallback);
    requestAnimationFrame(()=>drawRadarBase(fallback,points,position||null,point=>point.label||"",viewport.widthYalms,viewport.heightYalms));
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
  info.appendChild(renderIdentity({job:client.job,jobIconId:client.jobIconId,level:client.player?.level,gender:client.gender,raceId:client.raceId,tribeId:client.tribeId}));

  const badges=document.createElement("div");
  badges.className="badges";
  badges.appendChild(chip(clientStatusText(client),clientStatusKind(client)));
  badges.appendChild(chip(formatAge(client.ageSeconds),""));

  const metrics=document.createElement("div");
  metrics.className="meta wide";
  metrics.append(
    tile("HP",hpText(client.player?.currentHp,client.player?.maxHp),client.player?.currentHp==null?"bad":""),
    tile("MP",mpText(client.player?.currentMp,client.player?.maxMp),client.player?.currentMp==null?"bad":""),
    tile("Position",posText(client.position),client.position?"":"bad"),
    tile("Repair",repairText(client.repair),client.repair?"":"bad"),
    tile("Policy",policyText(client.policy),client.policy?.allowEchoCommands||client.policy?.allowScreenshotRequests?"":"warn"),
    tile("Focus",combatHeadline(client.combat),"",combatHeadline(client.combat))
  );

  const stateSection=document.createElement("div");
  stateSection.className="section";
  stateSection.innerHTML=`<div class="sectionhead">Status</div>`;
  stateSection.appendChild(renderStates(client));

  const partySection=document.createElement("div");
  partySection.className="section";
  partySection.innerHTML=`<div class="sectionhead">Party</div>`;
  partySection.appendChild(renderParty(client));

  const foot=document.createElement("div");
  foot.className="foot";
  foot.textContent=`Last update ${client.lastSeenUtc} | ${client.updateKind}`;

  head.append(info,badges);
  card.append(
    head,
    metrics,
    renderClientTelemetry(client),
    stateSection,
    partySection,
    renderMinimapSection(client.map,client.position,"Minimap",!!client?.conditions?.inCombat,buildClientMinimapPoints(client),"YOU"),
    renderThreats(client.combat),
    renderRemoteControlSection(client,"Remote Control"),
    foot
  );
  return card;
}
function renderAggregateMember(member){const row=document.createElement("div");row.className=`aggmember ${member.isStranger?"stranger":""}`.trim();const main=document.createElement("div");main.className="aggmain";const info=document.createElement("div");info.className="aggname";info.innerHTML=`<span class="slot">${member.slotText}</span><span class="membername">${displayCharacter(member.name,member.worldName,member.krangledName)}</span><span class="job">${member.job||"--"}</span><span class="lvl">${levelText(member.level)}</span>`;info.appendChild(renderIdentity(member));const badges=document.createElement("div");badges.className="badges";if(member.isStranger){badges.append(chip("Stranger","bad"),chip("Limited data","warn"))}else{badges.append(chip(member.isDisconnected?"Disconnected":member.stale?"Stale":"Live",member.isDisconnected?"bad":member.stale?"warn":"ok"));badges.append(chip(member.isSubmitting?"Submitting":"Monitored",member.isSubmitting?"ok":"warn"));if(member.isSource)badges.append(chip("Source","ok"))}main.append(info,badges);const meta=document.createElement("div");meta.className="aggmeta";meta.append(tile("HP",hpText(member.currentHp,member.maxHp),member.currentHp==null?"bad":""),tile("MP",mpText(member.currentMp,member.maxMp),member.currentMp==null?"bad":""),tile("Position",posText(member.position),member.position?"" :"bad"),tile("Extra",member.isStranger?"Conditions/repair unavailable":member.repair?repairText(member.repair):"No repair data",member.isStranger||!member.repair?"bad":""));row.append(main,meta);if(!member.isStranger){const states=renderStates(member);row.append(states);const note=document.createElement("div");note.className="aggnote";note.textContent=`${member.territoryName||"Unknown zone"} (${member.territoryId??0}) | Last update ${member.lastSeenUtc} | ${member.updateKind}`;row.append(note)}else{const note=document.createElement("div");note.className="aggnote bad";note.textContent="Only party-list fields are available for strangers: name, position, HP, MP, level, and job.";row.append(note)}return row}
function renderAggregateTelemetry(party){return factSection("Source",[{label:"Connected",value:party.sourceConnectedAtUtc||"Unknown"},{label:"Age",value:formatAge(party.sourceAgeSeconds)},{label:"Host",value:party.sourceHostName||"Unknown host",kind:party.sourceHostName?"":"bad"},{label:"Game path",value:pathLeaf(party.sourceGameInstallPath),title:party.sourceGameInstallPath||"",kind:party.sourceGameInstallPath?"":"bad"},{label:"Labels",value:party.sourceEnumeratePartyMembers?"Slot numbers":"Names"},{label:"Controls",value:policyText(party.sourcePolicy)},{label:"Routing",value:"Stranger actions route through the source client."},{label:"Focus",value:combatHeadline(party.combat),title:combatHeadline(party.combat)}])}
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

  const metrics=document.createElement("div");
  metrics.className="meta wide";
  metrics.append(
    tile("Source host",party.sourceHostName||"Unknown",party.sourceHostName?"":"bad"),
    tile("Policy",policyText(party.sourcePolicy),party.sourcePolicy?.allowEchoCommands||party.sourcePolicy?.allowScreenshotRequests?"":"warn"),
    tile("Path",pathLeaf(party.sourceGameInstallPath),party.sourceGameInstallPath?"":"bad",party.sourceGameInstallPath||""),
    tile("Monitored",String(party.monitoredCount),party.monitoredCount>0?"":"bad"),
    tile("Strangers",String(party.strangerCount),party.strangerCount>0?"warn":""),
    tile("Age",formatAge(party.sourceAgeSeconds))
  );

  const section=document.createElement("div");
  section.className="section";
  section.innerHTML=`<div class="sectionhead">Aggregated Party</div>`;
  const sourceMember=party.members.find(m=>m.isSource&&m.position)||party.members.find(m=>m.position&&!m.isStranger)||null;

  const members=document.createElement("div");
  members.className="aggmembers";
  for(const member of party.members)
    members.appendChild(renderAggregateMember(member));

  section.append(
    renderMinimapSection(party.map,party.sourcePosition,"Source Minimap",aggregatePartyInCombat(party),buildAggregateMinimapPoints(party,sourceMember),sourceMember?"SRC":""),
    members,
    renderThreats(party.combat),
    renderRemoteControlSection({accountId:party.sourceAccountId,characterName:party.sourceCharacterName,worldName:party.sourceWorldName,sourcePolicy:party.sourcePolicy,sourceLastScreenshot:party.sourceLastScreenshot},"Source Remote Control","Aggregate-party stranger actions route through the source client.")
  );

  const foot=document.createElement("div");
  foot.className="foot";
  foot.textContent=`Stranger source locked to first monitored client: ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)} | Connected ${party.sourceConnectedAtUtc}`;

  head.append(info,badges);
  card.append(head,metrics,renderAggregateTelemetry(party),section,foot);
  return card;
}
function renderEmptyState(totalClients){const empty=document.createElement("div");empty.className="empty";empty.textContent=totalClients===0?"No clients connected yet. Start the server, point TTSL at it, then enable remote publishing. Future sheet/icon extraction requires at least one client on the same PC as this Python monitor.":"All tracked clients are stale or disconnected.";return empty}
function renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){const panel=document.createElement("section");panel.className="overviewpanel";panel.innerHTML=`<div class="sectionhead">Situation</div>`;const grid=document.createElement("div");grid.className="overviewgrid";const visibleSurfaces=aggregateParties.checked?visibleAggregate.length+visibleLoose.length:visibleClients.length;grid.append(overviewCard("Tracked",String(totalClients),`${liveClients} live | ${totalClients-liveClients} stale/disconnected`),overviewCard("Visible",String(visibleSurfaces),aggregateParties.checked?`${visibleAggregate.length} party surfaces | ${visibleLoose.length} loose`:`${visibleClients.length} client surfaces`),overviewCard("Path",state.gamePathInfo?.captured?"Ready":"Missing",pathSummary(state.gamePathInfo)),overviewCard("Extract",state.assetExtraction?.running?"Busy":state.assetExtraction?.lastExitCode===0?"Ready":"Idle",extractionSummary(state.assetExtraction)));panel.appendChild(grid);return panel}
function buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose){return aggregateParties.checked?[...visibleAggregate.map(party=>({key:partyKey(party),kind:"party",item:party})),...visibleLoose.map(client=>({key:clientKey(client),kind:"client",item:client}))]:visibleClients.map(client=>({key:clientKey(client),kind:"client",item:client}))}
function resolveSelectedEntry(entries){if(entries.length===0){selectedEntityKey="";persistStringPreference("selectedEntity","");return null}const found=entries.find(entry=>entry.key===selectedEntityKey);if(found)return found;selectedEntityKey=entries[0].key;persistStringPreference("selectedEntity",selectedEntityKey);return entries[0]}
function renderOperatorItem(entry,active){const button=document.createElement("button");button.type="button";button.className=`opitem ${active?"active":""}`.trim();button.addEventListener("click",()=>selectEntity(entry.key));if(entry.kind==="party"){const party=entry.item;button.innerHTML=`<div class="oprow"><div class="opname">Party | ${party.territoryName||"Unknown zone"}</div><div>${""}</div></div><div class="opsub">Source ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}</div><div class="opmeta">${party.monitoredCount} monitored | ${party.strangerCount} stranger | ${formatAge(party.sourceAgeSeconds)} | ${party.sourceHostName||"Unknown host"}</div>`;button.querySelector(".oprow div:last-child").replaceWith(chip(`${party.liveCount} live`,party.liveCount>0?"ok":"bad"));return button}const client=entry.item;button.innerHTML=`<div class="oprow"><div class="opname">${displayCharacter(client.characterName,client.worldName,client.krangledName)}</div><div>${""}</div></div><div class="opsub">${client.territoryName||"Unknown zone"} | ${client.job||"UNK"} | ${queueStateText(client)}</div><div class="opmeta">${formatAge(client.ageSeconds)} | ${client.hostName||"Unknown host"} | ${policyText(client.policy)}</div>`;button.querySelector(".oprow div:last-child").replaceWith(chip(clientStatusText(client),clientStatusKind(client)));return button}
function renderOperatorLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){const shell=document.createElement("div");shell.className="operator-shell";const rail=document.createElement("aside");rail.className="operator-rail";rail.append(renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients),Object.assign(document.createElement("div"),{className:"hint",textContent:"Select a client or aggregate party surface to inspect the detail pane."}));const entries=buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose);const detail=document.createElement("section");detail.className="operator-detail";if(entries.length===0){detail.appendChild(renderEmptyState(totalClients));shell.append(rail,detail);return shell}const selected=resolveSelectedEntry(entries);const list=document.createElement("div");list.className="oplist";for(const entry of entries)list.appendChild(renderOperatorItem(entry,entry.key===selected?.key));rail.appendChild(list);detail.appendChild(selected.kind==="party"?renderAggregateParty(selected.item):renderClient(selected.item));shell.append(rail,detail);return shell}
function renderCompactClientCard(client){const card=document.createElement("section");card.className="card";const head=document.createElement("div");head.className="head";const info=document.createElement("div");info.innerHTML=`<div class="name">${displayCharacter(client.characterName,client.worldName,client.krangledName)}</div><div class="zone">${client.territoryName||"Unknown zone"}</div><div class="sub">${client.hostName||"Unknown host"} | ${formatAge(client.ageSeconds)}</div>`;info.appendChild(renderIdentity({job:client.job,jobIconId:client.jobIconId,level:client.player?.level,gender:client.gender,raceId:client.raceId,tribeId:client.tribeId}));const badges=document.createElement("div");badges.className="badges";badges.append(chip(clientStatusText(client),clientStatusKind(client)),chip(queueStateText(client),client.conditions?.waitingForDuty?"warn":client.conditions?.boundByDuty?"ok":""));head.append(info,badges);const meta=document.createElement("div");meta.className="meta wide";meta.append(tile("HP",hpText(client.player?.currentHp,client.player?.maxHp),client.player?.currentHp==null?"bad":""),tile("MP",mpText(client.player?.currentMp,client.player?.maxMp),client.player?.currentMp==null?"bad":""),tile("Repair",repairText(client.repair),client.repair?"":"bad"),tile("Policy",policyText(client.policy),client.policy?.allowEchoCommands||client.policy?.allowScreenshotRequests?"":"warn"),tile("Path",pathLeaf(client.gameInstallPath),client.gameInstallPath?"":"bad",client.gameInstallPath||""),tile("Focus",combatHeadline(client.combat),"",combatHeadline(client.combat)));card.append(head,meta,renderStates(client),renderRemoteControlSection(client,"Actions"));return card}
function renderCommandLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){const shell=document.createElement("div");shell.className="command-shell";shell.appendChild(renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients));const columns=document.createElement("div");columns.className="command-columns";const stage=document.createElement("div");stage.className="command-stage";const side=document.createElement("div");side.className="command-side";if(aggregateParties.checked&&visibleAggregate.length>0){for(const party of visibleAggregate){const card=renderAggregateParty(party);card.classList.add("party-stage");stage.appendChild(card)}if(visibleLoose.length>0){const reserve=document.createElement("section");reserve.className="overviewpanel";reserve.innerHTML=`<div class="sectionhead">Loose Clients</div><div class="hint">Clients not currently represented inside an aggregate party surface.</div>`;const grid=document.createElement("div");grid.className="compactgrid";for(const client of visibleLoose)grid.appendChild(renderCompactClientCard(client));reserve.appendChild(grid);side.appendChild(reserve)}}else{const note=document.createElement("section");note.className="overviewpanel";note.innerHTML=`<div class="sectionhead">Command View</div><div class="hint">${aggregateParties.checked?"No aggregate party surfaces are available right now, so command view is showing compact client cards.":"Aggregate parties are disabled. Enable the toggle above to unlock the full party command board."}</div>`;stage.appendChild(note);const grid=document.createElement("div");grid.className="compactgrid";const source=aggregateParties.checked?visibleLoose:visibleClients;if(source.length===0)stage.appendChild(renderEmptyState(totalClients));else{for(const client of source)grid.appendChild(renderCompactClientCard(client));stage.appendChild(grid)}}columns.appendChild(stage);if(side.childElementCount>0)columns.appendChild(side);shell.appendChild(columns);return shell}
function matrixCell(label,value,extraClass=""){const cell=document.createElement("div");cell.className=`matrixcell ${extraClass}`.trim();cell.dataset.label=label;cell.textContent=value;return cell}
function renderMatrixRow(entry,active){const button=document.createElement("button");button.type="button";button.className=`matrix-row ${active?"active":""}`.trim();button.addEventListener("click",()=>selectEntity(entry.key));if(entry.kind==="party"){const party=entry.item;const kind=document.createElement("div");kind.className="matrixcell";kind.dataset.label="Type";kind.appendChild(Object.assign(document.createElement("span"),{className:"kindtag",textContent:"Party"}));button.append(kind,matrixCell("Name",`Source ${displayCharacter(party.sourceCharacterName,party.sourceWorldName,party.sourceKrangledName)}`),matrixCell("Zone",party.territoryName||"Unknown zone"),matrixCell("Status",`${party.liveCount}/${party.staleCount}/${party.disconnectedCount}`),matrixCell("Flow",aggregatePartyInCombat(party)?"Combat":"Travel"),matrixCell("Vitals",`Mon ${party.monitoredCount} | Str ${party.strangerCount}`,"mono"),matrixCell("Repair",policyText(party.sourcePolicy)),matrixCell("Age",formatAge(party.sourceAgeSeconds),"mono"),matrixCell("Host",party.sourceHostName||"--","mono"));return button}const client=entry.item;const kind=document.createElement("div");kind.className="matrixcell";kind.dataset.label="Type";kind.appendChild(Object.assign(document.createElement("span"),{className:"kindtag",textContent:"Client"}));button.append(kind,matrixCell("Name",displayCharacter(client.characterName,client.worldName,client.krangledName)),matrixCell("Zone",client.territoryName||"Unknown zone"),matrixCell("Status",clientStatusText(client)),matrixCell("Flow",`${queueStateText(client)}${client.conditions?.inCombat?" | Hot":""}`),matrixCell("Vitals",pct(client.player?.currentHp,client.player?.maxHp),"mono"),matrixCell("Repair",client.repair?`${client.repair.minCondition}%`:"n/a","mono"),matrixCell("Age",formatAge(client.ageSeconds),"mono"),matrixCell("Host",client.hostName||"--","mono"));return button}
function renderMatrixLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){const shell=document.createElement("div");shell.className="matrix-shell";shell.appendChild(renderOverviewPanel(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients));const entries=buildSurfaceEntries(visibleClients,visibleAggregate,visibleLoose);if(entries.length===0){shell.appendChild(renderEmptyState(totalClients));return shell}const selected=resolveSelectedEntry(entries);const layout=document.createElement("div");layout.className="matrix-layout";const left=document.createElement("section");left.className="matrixpane";left.innerHTML=`<div class="sectionhead">Surface Matrix</div>`;const table=document.createElement("div");table.className="matrixtable";const head=document.createElement("div");head.className="matrixhead";head.innerHTML=`<div>Type</div><div>Name</div><div>Zone</div><div>Status</div><div>Flow</div><div>Vitals</div><div>Repair</div><div>Age</div><div>Host</div>`;table.appendChild(head);for(const entry of entries)table.appendChild(renderMatrixRow(entry,entry.key===selected?.key));left.appendChild(table);const right=document.createElement("section");right.className="matrixpane";right.innerHTML=`<div class="sectionhead">Inspector</div>`;right.appendChild(selected.kind==="party"?renderAggregateParty(selected.item):renderClient(selected.item));layout.append(left,right);shell.appendChild(layout);return shell}
function renderSurface(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients){if(currentLayoutMode==="classic"){const fragment=document.createDocumentFragment();if(aggregateParties.checked){for(const party of visibleAggregate)fragment.appendChild(renderAggregateParty(party));for(const client of visibleLoose)fragment.appendChild(renderClient(client));if(visibleAggregate.length===0&&visibleLoose.length===0)fragment.appendChild(renderEmptyState(totalClients));return fragment}if(visibleClients.length===0){fragment.appendChild(renderEmptyState(totalClients));return fragment}for(const client of visibleClients)fragment.appendChild(renderClient(client));return fragment}if(currentLayoutMode==="command")return renderCommandLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients);if(currentLayoutMode==="matrix")return renderMatrixLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients);return renderOperatorLayout(state,visibleClients,visibleAggregate,visibleLoose,totalClients,liveClients)}
function flattenGroups(groups){return groups.flatMap(group=>group.clients.map(client=>({...client,accountId:group.accountId})))}
function pathSummary(info){if(!info||!info.captured)return"same-PC game path not captured yet";const host=info.sourceHostName?` on ${info.sourceHostName}`:"";return`same-PC game path ready from ${displayCharacter(info.sourceCharacterName,info.sourceWorldName,info.sourceKrangledName)}${host}`}
function assetSummary(plan,catalog){const warning=(catalog?.warnings||[])[0];if(!plan||!plan.summary)return warning||"Asset plan pending.";const s=plan.summary;const readyJobIcons=Object.keys(catalog?.jobIcons||{}).length;const readyRaceIcons=Object.keys(catalog?.raceIcons||{}).length;const readyTribeIcons=Object.keys(catalog?.tribeIcons||{}).length;const readyMaps=Object.keys(catalog?.maps||{}).length;const base=`Asset plan: ${s.jobIcons} job icon tex path(s), ${s.maps} map texture(s), ${s.races} race id(s), ${s.tribes} tribe id(s), ${s.enemies} enemy id(s) | web cache ${readyJobIcons} job icon(s), ${readyRaceIcons} race icon(s), ${readyTribeIcons} clan icon(s), ${readyMaps} map png(s)`;return warning?`${base} | ${warning}`:base}
function extractionSummary(state){if(!state)return"Extraction idle.";if(state.running)return`Extraction running: ${state.message||"working..."}`;if(state.lastCompletedUtc)return`Extraction ${state.lastExitCode===0?"ready":"failed"}: ${state.message||"see server log"}`;return state.message||"Extraction idle."}
async function triggerExtract(){try{extractAssets.disabled=true;const res=await fetch("/api/extract-assets",{method:"POST",headers:{"Content-Type":"application/json"},body:"{}"});const payload=await res.json();if(!res.ok||!payload.ok)throw new Error(payload.error||`HTTP ${res.status}`);extractStatus.textContent=payload.message||"Extraction started.";await refresh()}catch(err){extractStatus.textContent=`Extraction request failed: ${err}`;extractAssets.disabled=false}}
function remoteControlKey(target){return`${String(target?.accountId||"").trim()}|${String(target?.characterName||"").trim()}|${String(target?.worldName||"").trim()}`}
function activeRemoteDraftKey(){const active=document.activeElement;return active instanceof HTMLInputElement?String(active.dataset.remoteDraftKey||"").trim():""}
async function queueRemoteAction(target,actionType,text=""){try{const payload={accountId:target.accountId,characterName:target.characterName,worldName:target.worldName,actionType};if(text)payload.text=text;const res=await fetch("/api/queue-action",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(payload)});const response=await res.json();if(!res.ok||!response.ok)throw new Error(response.error||`HTTP ${res.status}`);extractStatus.textContent=response.message||"Queued remote action.";await refresh();return true}catch(err){extractStatus.textContent=`Remote action failed: ${err}`;return false}}
function renderRemoteControlSection(target,title,noteText=""){const section=document.createElement("div");section.className="section";section.innerHTML=`<div class="sectionhead">${title}</div>`;const controls=document.createElement("div");controls.className="controls";const policy=target?.policy||target?.sourcePolicy||{};const lastScreenshot=target?.lastScreenshot||target?.sourceLastScreenshot||null;if(policy.allowEchoCommands){const row=document.createElement("div");row.className="controlrow";const draftKey=remoteControlKey(target);const input=document.createElement("input");input.type="text";input.maxLength=220;input.placeholder="Plain text goes to /echo. Slash commands like /sit run verbatim";input.dataset.remoteDraftKey=draftKey;input.value=remoteControlDrafts.get(draftKey)||"";input.addEventListener("input",()=>remoteControlDrafts.set(draftKey,input.value));input.addEventListener("blur",()=>{const value=String(input.value||"");if(value)remoteControlDrafts.set(draftKey,value);else remoteControlDrafts.delete(draftKey)});const button=document.createElement("button");button.type="button";button.textContent="Send Text";button.addEventListener("click",()=>{const text=String(input.value||"").trim();if(!text)return;button.disabled=true;queueRemoteAction(target,"echoCommand",text).then(ok=>{button.disabled=false;if(ok){input.value="";remoteControlDrafts.delete(draftKey)}})});input.addEventListener("keydown",event=>{if(event.key==="Enter"){event.preventDefault();button.click()}});row.append(input,button);controls.appendChild(row)}if(policy.allowScreenshotRequests||lastScreenshot){const row=document.createElement("div");row.className="controlrow";if(policy.allowScreenshotRequests){const button=document.createElement("button");button.type="button";button.textContent="Request Screenshot";button.addEventListener("click",()=>{button.disabled=true;queueRemoteAction(target,"requestScreenshot").finally(()=>{button.disabled=false})});row.appendChild(button)}if(lastScreenshot?.url){const link=document.createElement("a");link.href=lastScreenshot.url;link.target="_blank";link.rel="noopener noreferrer";link.textContent="Last Screenshot Sent";row.appendChild(link);const stamp=document.createElement("span");stamp.className="controlnote";stamp.textContent=`${lastScreenshot.capturedAtUtc||"Unknown time"}`;row.appendChild(stamp)}controls.appendChild(row)}const note=document.createElement("div");note.className="controlnote";if(noteText){note.textContent=noteText}else if(policy.allowEchoCommands||policy.allowScreenshotRequests){note.textContent="Plain text is echoed with a [TTSL Web] prefix. Slash-prefixed input is sent verbatim, subject to this client's local policy settings."}else{note.textContent="This client is not currently allowing web-triggered text, slash commands, or screenshots."}controls.appendChild(note);section.appendChild(controls);return section}
async function refresh(){try{const editingRemoteDraftKey=activeRemoteDraftKey();const res=await fetch("/api/state",{cache:"no-store"});if(!res.ok)throw new Error(`HTTP ${res.status}`);const state=await res.json();currentAssetCatalog=state.assetCatalog||{jobIcons:{},maps:{},raceIcons:{},tribeIcons:{},warnings:[]};const clients=flattenGroups(state.accountGroups).sort((a,b)=>Number(a.stale||a.isDisconnected)-Number(b.stale||b.isDisconnected)||String(a.characterName).localeCompare(String(b.characterName))||String(a.worldName).localeCompare(String(b.worldName)));const live=clients.filter(c=>!c.stale&&!c.isDisconnected).length;const aggregate=Array.isArray(state.aggregateParties)?state.aggregateParties:[];const looseFromServer=Array.isArray(state.looseClients)?state.looseClients:clients;const visibleClients=showStale.checked?clients:clients.filter(c=>!c.stale&&!c.isDisconnected);const visibleLoose=(showStale.checked?looseFromServer:looseFromServer.filter(c=>!c.stale&&!c.isDisconnected)).sort((a,b)=>Number(a.stale||a.isDisconnected)-Number(b.stale||b.isDisconnected)||String(a.characterName).localeCompare(String(b.characterName))||String(a.worldName).localeCompare(String(b.worldName)));const visibleAggregate=aggregateParties.checked?(showStale.checked?aggregate:aggregate.filter(p=>p.liveCount>0)):[];summary.textContent=`${clients.length} client(s) tracked | ${live} live | ${clients.length-live} stale/disconnected${aggregateParties.checked?` | ${aggregate.length} party group(s)`:""}`;stamp.textContent=`Generated ${state.generatedAtUtc} | stale after ${state.staleSeconds}s | ${pathSummary(state.gamePathInfo)}`;assetPlan.textContent=assetSummary(state.assetPlan,currentAssetCatalog);extractStatus.textContent=extractionSummary(state.assetExtraction);extractAssets.textContent=state.assetExtraction?.running?"Extracting...":"Extract Assets";extractAssets.disabled=!!state.assetExtraction?.running||!state.gamePathInfo?.captured;if(editingRemoteDraftKey)return;app.className=`layout-${currentLayoutMode}`;app.replaceChildren();app.appendChild(renderSurface(state,visibleClients,visibleAggregate,visibleLoose,clients.length,live))}catch(err){summary.textContent="Refresh failed";stamp.textContent=String(err);assetPlan.textContent="Asset plan unavailable.";extractStatus.textContent="Extraction status unavailable.";extractAssets.disabled=false}}
wireNumericPreference(mapBoxPxInput,"mapBoxPx",DEFAULT_MAP_BOX_PX,96,320);wireNumericPreference(combatWidthInput,"combatWidth",DEFAULT_COMBAT_WIDTH_YALMS,5,300);wireNumericPreference(combatHeightInput,"combatHeight",DEFAULT_COMBAT_HEIGHT_YALMS,5,300);wireNumericPreference(travelWidthInput,"travelWidth",DEFAULT_TRAVEL_WIDTH_YALMS,5,500);wireNumericPreference(travelHeightInput,"travelHeight",DEFAULT_TRAVEL_HEIGHT_YALMS,5,500);currentLayoutMode=loadStringPreference("layoutMode",DEFAULT_LAYOUT_MODE,LAYOUT_MODES);selectedEntityKey=loadStringPreference("selectedEntity","",null);applyLayoutMode(currentLayoutMode);extractAssets.addEventListener("click",triggerExtract);krangle.addEventListener("change",refresh);krangleEnemies.addEventListener("change",refresh);showStale.addEventListener("change",refresh);aggregateParties.addEventListener("change",refresh);icons.addEventListener("change",refresh);enumerate.addEventListener("change",refresh);for(const button of layoutButtons)button.addEventListener("click",()=>{applyLayoutMode(button.dataset.mode);refresh()});refresh();setInterval(refresh,1000);
</script></body></html>"""


class TTSLStateStore:
    def __init__(self, stale_seconds: int) -> None:
        self.stale_seconds = stale_seconds
        self.retention_seconds = max(stale_seconds * 2, stale_seconds + 60)
        self._clients: dict[tuple[str, str, str], dict] = {}
        self._pending_actions: dict[tuple[str, str, str], list[dict]] = {}
        self._server_host_name = socket.gethostname().strip().casefold()
        self._session_game_path: str | None = None
        self._session_game_path_source: dict | None = None
        self._asset_plan_output_path = os.path.join(SERVER_ROOT, "ttsl_asset_plan.json")
        self._last_asset_plan_json = ""
        self._asset_catalog_cache_mtime = -1.0
        self._asset_catalog_cache = {"available": False, "jobIcons": {}, "maps": {}, "raceIcons": {}, "tribeIcons": {}, "warnings": []}
        self._last_auto_extract_signature = ""
        self._last_auto_extract_started_unix = 0.0
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
        auto_extract_requested = False
        auto_extract_message = ""
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
                "policy",
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

            if str(payload.get("updateKind") or "full").strip().lower() != "position":
                _, _, auto_extract_requested, auto_extract_message = self._evaluate_auto_extract_locked(now)

        if auto_extract_requested:
            threading.Thread(target=self._run_asset_extract, daemon=True).start()
            log_event(auto_extract_message)

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
            for client in self._clients.values():
                now_unix = time.time()
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
            asset_plan, asset_catalog, auto_extract_requested, auto_extract_message = self._evaluate_auto_extract_locked(now, snapshot_clients)

            snapshot = {
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

        if auto_extract_requested:
            threading.Thread(target=self._run_asset_extract, daemon=True).start()
            log_event(auto_extract_message)

        return snapshot

    def _evaluate_auto_extract_locked(
        self,
        now: datetime,
        snapshot_clients: list[dict] | None = None,
    ) -> tuple[dict, dict, bool, str]:
        now_unix = time.time()
        if snapshot_clients is None:
            snapshot_clients = [deepcopy(client) for client in self._clients.values()]

        asset_plan = self._build_asset_plan_locked(snapshot_clients, now)
        asset_catalog = self._build_asset_catalog_locked()
        missing_map_textures = self._get_missing_map_textures(asset_plan, asset_catalog)
        missing_race_icons = self._get_missing_named_icons(asset_plan.get("raceIds"), (asset_catalog.get("raceIcons") or {}).keys())
        missing_tribe_icons = self._get_missing_named_icons(asset_plan.get("tribeIds"), (asset_catalog.get("tribeIcons") or {}).keys())
        if not missing_map_textures and not missing_race_icons and not missing_tribe_icons:
            self._last_auto_extract_signature = ""
            self._last_auto_extract_started_unix = 0.0
            return asset_plan, asset_catalog, False, ""

        auto_extract_signature = self._build_auto_extract_signature(missing_map_textures, missing_race_icons, missing_tribe_icons)
        if (
            self._session_game_path is None
            or self._asset_extract_state["running"]
            or (
                auto_extract_signature == self._last_auto_extract_signature
                and (now_unix - self._last_auto_extract_started_unix) < AUTO_EXTRACT_RETRY_COOLDOWN_SECONDS
            )
        ):
            return asset_plan, asset_catalog, False, ""

        work_items: list[str] = []
        map_count = len(missing_map_textures)
        if map_count:
            work_items.append(f"{map_count} missing {'map texture' if map_count == 1 else 'map textures'}")

        race_count = len(missing_race_icons)
        if race_count:
            work_items.append(f"{race_count} missing {'race icon' if race_count == 1 else 'race icons'}")

        tribe_count = len(missing_tribe_icons)
        if tribe_count:
            work_items.append(f"{tribe_count} missing {'clan icon' if tribe_count == 1 else 'clan icons'}")

        auto_extract_message = f"Auto-extracting {', '.join(work_items)} for the current session."
        if not self._prepare_asset_extract_locked(now, auto_extract_message):
            return asset_plan, asset_catalog, False, ""

        self._last_auto_extract_signature = auto_extract_signature
        self._last_auto_extract_started_unix = now_unix
        return asset_plan, asset_catalog, True, auto_extract_message

    def trigger_asset_extract(self) -> tuple[bool, str]:
        now = utc_now()
        with self._lock:
            self._build_asset_plan_locked([deepcopy(client) for client in self._clients.values()], now)
            if not self._prepare_asset_extract_locked(now, "Launching extractor with the current session plan."):
                if self._asset_extract_state["running"]:
                    return False, "Asset extraction is already running."
                if self._session_game_path is None:
                    return False, "Same-PC game path not captured yet."
                return False, f"Extractor script not found: {EXTRACT_SCRIPT_PATH}"

        threading.Thread(target=self._run_asset_extract, daemon=True).start()
        log_event("Asset extraction requested from web UI.")
        return True, "Asset extraction started."

    def queue_remote_action(self, payload: dict) -> tuple[bool, str]:
        action_type = str(payload.get("actionType") or "").strip().lower()
        target_key = self._make_key(payload)
        now = utc_now()

        with self._lock:
            client = self._clients.get(target_key)
            if client is None:
                return False, "Target client is not currently tracked."

            policy = client.get("policy") or {}
            if action_type == "echocommand":
                if not bool(policy.get("allowEchoCommands")):
                    return False, "That client does not allow web text or slash commands."

                text = self._sanitize_remote_text(payload.get("text"))
                if not text:
                    return False, "Text is empty."

                queue_item = {
                    "actionId": f"echo-{int(time.time() * 1000)}",
                    "actionType": "echoCommand",
                    "text": text,
                    "queuedAtUtc": utc_iso(now),
                }
                message = "Queued web text/slash command."
            elif action_type == "requestscreenshot":
                if not bool(policy.get("allowScreenshotRequests")):
                    return False, "That client does not allow web screenshot requests."

                queue_item = {
                    "actionId": f"shot-{int(time.time() * 1000)}",
                    "actionType": "requestScreenshot",
                    "queuedAtUtc": utc_iso(now),
                }
                message = "Queued screenshot request."
            else:
                return False, f"Unsupported action type: {action_type or 'missing'}"

            queue = self._pending_actions.setdefault(target_key, [])
            queue.append(queue_item)
            log_event(f"Queued web action {queue_item['actionType']} for {self._format_key(target_key)}")
            return True, message

    def consume_remote_actions(self, payload: dict) -> list[dict]:
        target_key = self._make_key(payload)
        with self._lock:
            queued = self._pending_actions.pop(target_key, [])
            return deepcopy(queued)

    def save_uploaded_screenshot(self, payload: dict) -> tuple[bool, str, dict | None]:
        target_key = self._make_key(payload)
        image_base64 = str(payload.get("imageBase64") or "").strip()
        content_type = str(payload.get("contentType") or "image/png").strip().lower()
        if not image_base64:
            return False, "Screenshot payload is empty.", None
        if content_type != "image/png":
            return False, f"Unsupported screenshot content type: {content_type}", None

        try:
            image_bytes = base64.b64decode(image_base64, validate=True)
        except Exception as exc:
            return False, f"Invalid screenshot base64 payload: {exc}", None

        captured_at = str(payload.get("capturedAtUtc") or utc_iso(utc_now()))
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        safe_stem = self._build_safe_client_stem(target_key)
        file_name = f"{safe_stem}_{timestamp}.png"
        file_path = os.path.join(SCREENSHOT_CACHE_ROOT, file_name)
        os.makedirs(SCREENSHOT_CACHE_ROOT, exist_ok=True)
        with open(file_path, "wb") as handle:
            handle.write(image_bytes)

        screenshot_info = {
            "capturedAtUtc": captured_at,
            "url": build_cache_url(file_path),
            "contentType": "image/png",
            "fileName": file_name,
            "actionId": payload.get("actionId"),
        }

        with self._lock:
            client = self._clients.get(target_key)
            if client is not None:
                client["lastScreenshot"] = screenshot_info

        log_event(f"Stored uploaded screenshot for {self._format_key(target_key)}: {file_name}")
        return True, "Screenshot stored.", deepcopy(screenshot_info)

    def _prepare_asset_extract_locked(self, now: datetime, message: str) -> bool:
        if self._asset_extract_state["running"]:
            return False
        if self._session_game_path is None:
            return False
        if not os.path.isfile(EXTRACT_SCRIPT_PATH):
            return False

        self._asset_extract_state = {
            "running": True,
            "message": message,
            "lastStartedUtc": utc_iso(now),
            "lastCompletedUtc": self._asset_extract_state.get("lastCompletedUtc"),
            "lastExitCode": self._asset_extract_state.get("lastExitCode"),
        }
        return True

    @staticmethod
    def _sanitize_remote_text(value: object) -> str:
        text = str(value or "").replace("\r", " ").replace("\n", " ").strip()
        if len(text) > 220:
            text = text[:220]
        return text

    @staticmethod
    def _build_safe_client_stem(key: tuple[str, str, str]) -> str:
        raw = f"{key[1]}_{key[2]}_{key[0]}"
        sanitized = "".join(ch if ch.isalnum() or ch in ("-", "_") else "_" for ch in raw)
        while "__" in sanitized:
            sanitized = sanitized.replace("__", "_")
        return sanitized.strip("_") or "ttsl_client"

    @staticmethod
    def _get_missing_map_textures(asset_plan: dict, asset_catalog: dict) -> list[dict]:
        available_map_keys = {
            str(key).strip()
            for key in (asset_catalog.get("maps") or {}).keys()
            if str(key).strip()
        }
        missing: list[dict] = []
        for entry in asset_plan.get("mapTextures") or []:
            if not isinstance(entry, dict):
                continue

            texture_candidates: list[str] = []
            primary = str(entry.get("texturePath") or "").strip()
            if primary:
                texture_candidates.append(primary)
            for candidate in entry.get("texturePathCandidates") or []:
                candidate_text = str(candidate or "").strip()
                if candidate_text and candidate_text not in texture_candidates:
                    texture_candidates.append(candidate_text)

            if any(TTSLStateStore._build_map_catalog_key(candidate, entry.get("mapId")) in available_map_keys for candidate in texture_candidates):
                continue

            missing.append(deepcopy(entry))

        return missing

    @staticmethod
    def _get_missing_named_icons(requested_ids: list[int] | None, available_keys) -> list[int]:
        available = {
            int(str(key).strip())
            for key in available_keys
            if str(key).strip().isdigit()
        }
        missing: list[int] = []
        for requested_id in requested_ids or []:
            try:
                value = int(requested_id)
            except (TypeError, ValueError):
                continue
            if value > 0 and value not in available:
                missing.append(value)
        return sorted(set(missing))

    @staticmethod
    def _build_auto_extract_signature(
        missing_map_textures: list[dict],
        missing_race_icons: list[int],
        missing_tribe_icons: list[int],
    ) -> str:
        signature_parts: list[str] = []
        for entry in sorted(
            missing_map_textures,
            key=lambda item: (int(item.get("mapId") or 0), str(item.get("texturePath") or "")),
        ):
            texture_candidates = []
            primary = str(entry.get("texturePath") or "").strip()
            if primary:
                texture_candidates.append(primary)
            for candidate in entry.get("texturePathCandidates") or []:
                candidate_text = str(candidate or "").strip()
                if candidate_text and candidate_text not in texture_candidates:
                    texture_candidates.append(candidate_text)

            signature_parts.append(f"map:{int(entry.get('mapId') or 0)}:{'|'.join(texture_candidates)}")

        if missing_race_icons:
            signature_parts.append(f"race:{'|'.join(str(icon_id) for icon_id in missing_race_icons)}")

        if missing_tribe_icons:
            signature_parts.append(f"tribe:{'|'.join(str(icon_id) for icon_id in missing_tribe_icons)}")

        return ";".join(signature_parts)

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

        self._merge_cached_map_textures(map_ids, map_textures)

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
                "raceIcons": {"status": "ready_to_generate" if (race_ids or tribe_ids) else "waiting_for_data", "count": len(race_ids) + len(tribe_ids)},
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

        catalog = {"available": False, "jobIcons": {}, "maps": {}, "raceIcons": {}, "tribeIcons": {}, "warnings": []}
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
                        texture_path = str(entry.get("relativePath") or entry.get("texturePath") or "").strip()
                        if not texture_path:
                            continue

                        base_name = os.path.splitext(os.path.basename(texture_path or f"map_{map_id_value}.tex"))[0]
                        cache_path = ensure_png_cache(raw_path, f"maps/{map_id_value:06d}_{base_name}.png")
                        map_entry = {
                            "mapId": map_id_value,
                            "pngUrl": build_cache_url(cache_path),
                            "texturePath": texture_path,
                            "texturePathCandidates": entry.get("candidatePaths") or entry.get("texturePathCandidates") or [texture_path],
                            "offsetX": entry.get("offsetX"),
                            "offsetY": entry.get("offsetY"),
                            "sizeFactor": entry.get("sizeFactor"),
                        }
                        catalog["maps"][self._build_map_catalog_key(texture_path, map_id_value)] = map_entry
                        continue

                    if kind == "raceIcon":
                        race_id = entry.get("raceId")
                        if race_id in (None, ""):
                            continue

                        race_id_value = int(race_id)
                        cache_path = ensure_static_cache_copy(raw_path, f"race-icons/race_{race_id_value:03d}.svg")
                        catalog["raceIcons"][str(race_id_value)] = {
                            "raceId": race_id_value,
                            "svgUrl": build_cache_url(cache_path),
                            "masculineName": entry.get("masculineName"),
                            "feminineName": entry.get("feminineName"),
                        }
                        continue

                    if kind == "tribeIcon":
                        tribe_id = entry.get("tribeId")
                        if tribe_id in (None, ""):
                            continue

                        tribe_id_value = int(tribe_id)
                        cache_path = ensure_static_cache_copy(raw_path, f"tribe-icons/tribe_{tribe_id_value:03d}.svg")
                        catalog["tribeIcons"][str(tribe_id_value)] = {
                            "tribeId": tribe_id_value,
                            "raceId": entry.get("raceId"),
                            "svgUrl": build_cache_url(cache_path),
                            "masculineName": entry.get("masculineName"),
                            "feminineName": entry.get("feminineName"),
                            "raceMasculineName": entry.get("raceMasculineName"),
                            "raceFeminineName": entry.get("raceFeminineName"),
                        }
                except Exception as exc:
                    catalog["warnings"].append(f"{kind}: {exc}")

            catalog["available"] = bool(catalog["jobIcons"] or catalog["maps"] or catalog["raceIcons"] or catalog["tribeIcons"])
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

    @staticmethod
    def _build_map_catalog_key(texture_path: object, map_id: object | None) -> str:
        normalized_texture = str(texture_path or "").strip().replace("\\", "/").casefold()
        if normalized_texture:
            return f"texture:{normalized_texture}"

        if map_id not in (None, ""):
            try:
                return f"map:{int(map_id)}"
            except (TypeError, ValueError):
                pass

        return "map:unknown"

    def _merge_cached_map_textures(self, map_ids: set[int], map_textures: dict[str, dict]) -> None:
        if not map_ids or not os.path.isfile(self._asset_plan_output_path):
            return

        try:
            with open(self._asset_plan_output_path, "r", encoding="utf-8") as handle:
                cached_plan = json.load(handle)
        except Exception:
            return

        for entry in cached_plan.get("mapTextures", []):
            if not isinstance(entry, dict):
                continue

            try:
                map_id_value = int(entry.get("mapId"))
            except (TypeError, ValueError):
                continue

            if map_id_value <= 0 or map_id_value not in map_ids:
                continue

            texture_candidates: list[str] = []
            primary = str(entry.get("texturePath") or "").strip()
            if primary:
                texture_candidates.append(primary)

            for candidate in entry.get("texturePathCandidates") or []:
                candidate_text = str(candidate or "").strip()
                if candidate_text and candidate_text not in texture_candidates:
                    texture_candidates.append(candidate_text)

            if not texture_candidates:
                continue

            replacement = {
                "mapId": map_id_value,
                "texturePath": texture_candidates[0],
                "texturePathCandidates": texture_candidates,
                "offsetX": entry.get("offsetX"),
                "offsetY": entry.get("offsetY"),
                "sizeFactor": entry.get("sizeFactor"),
            }
            map_key = self._build_map_catalog_key(texture_candidates[0], map_id_value)
            existing = map_textures.get(map_key)
            if existing is None or len(texture_candidates) > len(existing.get("texturePathCandidates") or []):
                map_textures[map_key] = replacement

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
                map_key = self._build_map_catalog_key(texture_candidates[0], map_id_value if map_id_value > 0 else None)
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
            "sourceAccountId": source_client.get("accountId", ""),
            "sourceCharacterName": source_client.get("characterName", ""),
            "sourceWorldName": source_client.get("worldName", ""),
            "sourceKrangledName": source_client.get("krangledName", ""),
            "sourceConnectedAtUtc": source_client.get("connectedAtUtc", source_client.get("lastSeenUtc", "Unknown")),
            "sourceAgeSeconds": source_client.get("ageSeconds"),
            "sourceHostName": source_client.get("hostName", ""),
            "sourceGameInstallPath": source_client.get("gameInstallPath"),
            "sourceEnumeratePartyMembers": bool(source_client.get("enumeratePartyMembers")),
            "sourcePolicy": deepcopy(source_client.get("policy")),
            "sourceLastScreenshot": deepcopy(source_client.get("lastScreenshot")),
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
            self._pending_actions.pop(key, None)
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
                self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
                self.send_header("Pragma", "no-cache")
                self.send_header("Expires", "0")
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
                    return self._write_json({"ok": True, "actions": state.consume_remote_actions(payload)})
                if self.path == "/api/goodbye":
                    state.goodbye(payload)
                    return self._write_json({"ok": True})
                if self.path == "/api/extract-assets":
                    ok, message = state.trigger_asset_extract()
                    return self._write_json({"ok": ok, "message": message, "error": None if ok else message}, HTTPStatus.OK if ok else HTTPStatus.CONFLICT)
                if self.path == "/api/queue-action":
                    ok, message = state.queue_remote_action(payload)
                    return self._write_json({"ok": ok, "message": message, "error": None if ok else message}, HTTPStatus.OK if ok else HTTPStatus.CONFLICT)
                if self.path == "/api/upload-screenshot":
                    ok, message, screenshot = state.save_uploaded_screenshot(payload)
                    return self._write_json({"ok": ok, "message": message, "error": None if ok else message, "screenshot": screenshot}, HTTPStatus.OK if ok else HTTPStatus.CONFLICT)
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
