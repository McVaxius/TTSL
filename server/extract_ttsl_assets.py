#!/usr/bin/env python3
from __future__ import annotations

import argparse
import html
import importlib
import json
import os
import subprocess
import sys
import zlib
from dataclasses import dataclass


SCRIPT_ROOT = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PLAN_PATH = os.path.join(SCRIPT_ROOT, "ttsl_asset_plan.json")
DEFAULT_OUTPUT_ROOT = os.path.join(SCRIPT_ROOT, "extracted")
DEFAULT_SUMMARY_PATH = os.path.join(DEFAULT_OUTPUT_ROOT, "ttsl_asset_extract_summary.json")
LOCAL_PYTHON_DEPS_ROOT = os.path.join(SCRIPT_ROOT, "_pydeps")
EXCEL_HEADER_MAGIC = b"EXHF"
EXCEL_DATA_MAGIC = b"EXDF"
EXCEL_COLUMN_TYPE_STRING = 0x0
MAP_SHEET_NAME = "map"
RACE_SHEET_NAME = "race"
TRIBE_SHEET_NAME = "tribe"
MAP_SHEET_MAP_ID_COLUMN_INDEX = 6
MAP_DIFFUSE_SUFFIXES = ("_m", "_s")
DEFAULT_EXD_LANGUAGE_SUFFIXES = ("en", "ja", "de", "fr", "ko", "chs", "cht", "tc")
SQPACK_FILE_TYPE_EMPTY = 1
SQPACK_FILE_TYPE_STANDARD = 2
SQPACK_FILE_TYPE_MODEL = 3
SQPACK_FILE_TYPE_TEXTURE = 4
SQPACK_BLOCK_PADDING = 128
SQPACK_UNCOMPRESSED_BLOCK_MARKER = 32000
TEX_HEADER_SIZE = 80
STATIC_LUMINAPIE_CANDIDATES = [
    r"Z:\temp\awgil_clientstructs\ida",
    r"D:\temp\awgil_clientstructs\ida",
    r"Y:\temp\awgil_clientstructs\ida",
    r"Z:\_research\FFXIVClientStructs\ida",
    r"Y:\_research\FFXIVClientStructs\ida",
]
ICON_PALETTES = (
    ("#1a3653", "#78c5ff", "#eaf4ff"),
    ("#263d1f", "#93f2a5", "#f4fff8"),
    ("#57331d", "#ffbf74", "#fff7ef"),
    ("#4e2544", "#d5b7ff", "#f8f0ff"),
    ("#4f2633", "#ff9b7a", "#fff2ee"),
    ("#2d4a4d", "#87d7ff", "#eefcff"),
    ("#50441f", "#f5d96b", "#fffbe9"),
    ("#2d2f57", "#9aa9ff", "#f1f3ff"),
)


@dataclass
class LuminapieBindings:
    game_data_type: type
    parsed_file_name_type: type
    source_root: str


@dataclass
class LuminapieCandidateResult:
    candidate: str
    exists: bool
    luminapie_package_exists: bool
    import_error: str = ""


class LuminapieBootstrapError(ModuleNotFoundError):
    def __init__(self, results: list[LuminapieCandidateResult], dependency_hint: str = "") -> None:
        self.results = results
        details = []
        for result in results:
            if not result.exists:
                details.append(f"{result.candidate} [missing]")
                continue

            package_state = "package present" if result.luminapie_package_exists else "package missing"
            if result.import_error:
                details.append(f"{result.candidate} [{package_state}; import failed: {result.import_error}]")
            else:
                details.append(f"{result.candidate} [{package_state}; import failed: unknown error]")

        hint = ""
        if any("No module named 'yaml'" in result.import_error for result in results):
            hint = dependency_hint or "Hint: the Python environment running the TTSL server is missing PyYAML."

        self.hint = hint.strip()
        hint_suffix = f" {hint}" if hint else ""
        message = "Could not import luminapie from the local candidate roots. Checked: " + "; ".join(details) + hint_suffix
        super().__init__(message)


def ensure_local_dependency_root() -> None:
    if LOCAL_PYTHON_DEPS_ROOT not in sys.path:
        sys.path.insert(0, LOCAL_PYTHON_DEPS_ROOT)


def ensure_yaml_dependency() -> str:
    ensure_local_dependency_root()
    importlib.invalidate_caches()

    try:
        import yaml  # type: ignore  # noqa: F401
        return ""
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
        "PyYAML>=6,<7",
    ]

    result = subprocess.run(
        install_command,
        capture_output=True,
        text=True,
        timeout=180,
    )

    importlib.invalidate_caches()
    ensure_local_dependency_root()

    try:
        import yaml  # type: ignore  # noqa: F401
        return "Hint: PyYAML was missing from this Python environment and was installed into TTSL's local _pydeps cache."
    except ModuleNotFoundError:
        stdout = (result.stdout or "").strip()
        stderr = (result.stderr or "").strip()
        output_bits = [bit for bit in [stdout, stderr] if bit]
        output = " | ".join(output_bits)
        if output:
            return f"Hint: automatic PyYAML install failed. pip output: {output}"
        return "Hint: automatic PyYAML install failed and the Python environment still cannot import yaml."


def get_luminapie_candidates() -> list[str]:
    candidates: list[str] = []

    def add_candidate(candidate: str) -> None:
        if not candidate:
            return

        normalized = os.path.normpath(candidate)
        if normalized not in candidates:
            candidates.append(normalized)

    for candidate in STATIC_LUMINAPIE_CANDIDATES:
        add_candidate(candidate)

    script_drive, _ = os.path.splitdrive(SCRIPT_ROOT)
    if script_drive:
        drive_root = f"{script_drive}\\"
        add_candidate(os.path.join(drive_root, "temp", "awgil_clientstructs", "ida"))
        add_candidate(os.path.join(drive_root, "_research", "FFXIVClientStructs", "ida"))

    add_candidate(os.path.join(SCRIPT_ROOT, "..", "..", "awgil_clientstructs", "ida"))
    add_candidate(os.path.join(SCRIPT_ROOT, "..", "..", "FFXIVClientStructs", "ida"))
    add_candidate(os.path.join(SCRIPT_ROOT, "..", "..", "_research", "FFXIVClientStructs", "ida"))

    return candidates


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Extract raw TTSL assets from a same-PC FFXIV install using luminapie.")
    parser.add_argument("--plan", default=DEFAULT_PLAN_PATH, help="Path to ttsl_asset_plan.json.")
    parser.add_argument("--game-root", default="", help="Override the game root folder that contains sqpack.")
    parser.add_argument("--output-root", default=DEFAULT_OUTPUT_ROOT, help="Output folder for extracted raw files.")
    parser.add_argument("--summary", default=DEFAULT_SUMMARY_PATH, help="Summary JSON output path.")
    return parser.parse_args()


def load_plan(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)
    if not isinstance(payload, dict):
        raise ValueError("Asset plan must be a JSON object.")
    return payload


def resolve_game_root(explicit_root: str, plan: dict) -> str:
    candidates = []
    if explicit_root:
        candidates.append(explicit_root)
    game_install_path = str(plan.get("gameInstallPath") or "").strip()
    if game_install_path:
        candidates.append(game_install_path)

    for candidate in candidates:
        normalized = os.path.normpath(candidate)
        if os.path.isdir(os.path.join(normalized, "sqpack")):
            return normalized
        if os.path.isdir(os.path.join(normalized, "game", "sqpack")):
            return os.path.join(normalized, "game")

    raise FileNotFoundError("Could not resolve a game root that contains sqpack. Capture a same-PC path first or pass --game-root.")


def bootstrap_luminapie() -> LuminapieBindings:
    candidates = get_luminapie_candidates()
    dependency_hint = ensure_yaml_dependency()
    results: list[LuminapieCandidateResult] = []
    for candidate in candidates:
        exists = os.path.isdir(candidate)
        package_exists = os.path.isfile(os.path.join(candidate, "luminapie", "__init__.py"))
        result = LuminapieCandidateResult(
            candidate=candidate,
            exists=exists,
            luminapie_package_exists=package_exists,
        )
        results.append(result)

        if not exists:
            continue

        if candidate not in sys.path:
            sys.path.insert(0, candidate)
        try:
            from luminapie.game_data import GameData, ParsedFileName  # type: ignore

            return LuminapieBindings(GameData, ParsedFileName, candidate)
        except Exception as exc:
            result.import_error = f"{type(exc).__name__}: {exc}"
            if candidate in sys.path:
                sys.path.remove(candidate)

    raise LuminapieBootstrapError(results, dependency_hint)


def normalize_sqpack_payload(payload: object) -> bytes:
    if isinstance(payload, bytes):
        return payload
    if isinstance(payload, bytearray):
        return bytes(payload)
    if isinstance(payload, list):
        return b"".join(bytes(chunk) for chunk in payload)
    raise TypeError(f"Unsupported sqpack payload type: {type(payload)!r}")


def pad(value: int, multiple: int) -> int:
    remainder = value % multiple
    if remainder == 0:
        return value
    return value + (multiple - remainder)


def read_le_int(stream, size: int) -> int:
    data = stream.read(size)
    if len(data) != size:
        raise EOFError("Unexpected end of sqpack stream.")
    return int.from_bytes(data, byteorder="little", signed=False)


def read_sqpack_compressed_block(stream, *, last_in_file: bool = False) -> bytes:
    start = stream.tell()
    sixteen = stream.read(1)
    if len(sixteen) != 1:
        raise EOFError("Unexpected end of sqpack block header.")

    while sixteen != b"\x10":
        if sixteen != b"\x00":
            raise ValueError("Unable to locate valid compressed block header.")
        sixteen = stream.read(1)
        if len(sixteen) != 1:
            raise EOFError("Unexpected end of sqpack block header.")

    zeros = stream.read(3)
    zero = read_le_int(stream, 4)
    if zeros != b"\x00\x00\x00" or zero != 0:
        raise ValueError("Unable to locate valid compressed block header.")

    part_comp_size = read_le_int(stream, 4)
    part_decomp_size = read_le_int(stream, 4)

    if part_comp_size == SQPACK_UNCOMPRESSED_BLOCK_MARKER:
        data = stream.read(part_decomp_size)
        if len(data) != part_decomp_size:
            raise EOFError("Unexpected end of uncompressed sqpack block.")
    else:
        compressed = stream.read(part_comp_size)
        if len(compressed) != part_comp_size:
            raise EOFError("Unexpected end of compressed sqpack block.")
        data = zlib.decompress(compressed, wbits=-15)

    end = stream.tell()
    length = end - start
    target_length = pad(length, SQPACK_BLOCK_PADDING)
    remaining = target_length - length
    padding_data = stream.read(remaining)
    if len(padding_data) != remaining:
        raise EOFError("Unexpected end of sqpack padding.")

    sixteen_index = padding_data.find(b"\x10")
    if sixteen_index != -1:
        rewind = len(padding_data) - sixteen_index
        stream.seek(stream.tell() - rewind)
    elif (not last_in_file) and any(byte != 0 for byte in padding_data):
        raise ValueError("Unexpected real data in compressed data block padding section.")

    return data


def read_sqpack_compressed_blocks(stream, block_count: int, *, last_in_file: bool = False) -> bytes:
    blocks: list[bytes] = []
    for index in range(block_count):
        blocks.append(read_sqpack_compressed_block(stream, last_in_file=last_in_file and index == block_count - 1))
    return b"".join(blocks)


def read_sqpack_texture_file(dat_path: str, offset: int) -> bytes:
    with open(dat_path, "rb") as handle:
        handle.seek(offset)
        header_length = read_le_int(handle, 4)
        file_type = read_le_int(handle, 4)
        uncompressed_file_size = read_le_int(handle, 4)
        _ = read_le_int(handle, 4)
        _ = read_le_int(handle, 4)
        mip_count = read_le_int(handle, 4)

        if file_type == SQPACK_FILE_TYPE_EMPTY:
            raise ValueError(f"Sqpack file at 0x{offset:X} is empty.")
        if file_type != SQPACK_FILE_TYPE_TEXTURE:
            raise ValueError(f"Sqpack file at 0x{offset:X} is type {file_type}, not texture.")

        end_of_header = offset + header_length
        mip_map_info_offset = offset + 24

        handle.seek(end_of_header)
        tex_header = handle.read(TEX_HEADER_SIZE)
        if len(tex_header) != TEX_HEADER_SIZE:
            raise EOFError("Unexpected end of texture header.")

        decompressed_chunks: list[bytes] = [tex_header]
        for mip_index in range(mip_count):
            handle.seek(mip_map_info_offset + (20 * mip_index))
            offset_from_header_end = read_le_int(handle, 4)
            _ = read_le_int(handle, 4)
            _ = read_le_int(handle, 4)
            _ = read_le_int(handle, 4)
            mip_map_parts = read_le_int(handle, 4)

            handle.seek(end_of_header + offset_from_header_end)
            decompressed_chunks.append(
                read_sqpack_compressed_blocks(handle, mip_map_parts, last_in_file=mip_index == mip_count - 1)
            )

    decompressed_data = b"".join(decompressed_chunks)
    if len(decompressed_data) < uncompressed_file_size:
        decompressed_data += b"\x00" * (uncompressed_file_size - len(decompressed_data))
    elif len(decompressed_data) > uncompressed_file_size:
        decompressed_data = decompressed_data[:uncompressed_file_size]

    return decompressed_data


def extract_raw_file_with_sqpack_fallback(game_data: object, parsed_file: object, relative_path: str) -> bytes:
    repo_index = game_data.get_repo_index(parsed_file.repo)
    repository = game_data.repositories[repo_index]
    index_entry, sqpack = repository.get_index(parsed_file.index)
    data_file_id = index_entry.data_file_id()
    offset = index_entry.data_file_offset()
    dat_path = sqpack.data_files[data_file_id]

    with open(dat_path, "rb") as handle:
        handle.seek(offset + 4)
        file_type = read_le_int(handle, 4)

    if file_type == SQPACK_FILE_TYPE_TEXTURE:
        return read_sqpack_texture_file(dat_path, offset)

    raise ValueError(f"Sqpack fallback is not implemented for file type {file_type} at {relative_path}.")


def extract_raw_file(game_data: object, parsed_file_name_type: type, relative_path: str) -> bytes:
    parsed = parsed_file_name_type(relative_path)
    try:
        payload = game_data.get_file(parsed)
        return normalize_sqpack_payload(payload)
    except Exception as exc:
        if "Type: 4 not implemented." not in str(exc):
            raise
        return extract_raw_file_with_sqpack_fallback(game_data, parsed, relative_path)


def write_file(output_root: str, relative_path: str, data: bytes) -> str:
    normalized_relative = relative_path.replace("/", os.sep)
    destination = os.path.join(output_root, "raw", normalized_relative)
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    with open(destination, "wb") as handle:
        handle.write(data)
    return destination


def get_map_texture_candidates(map_texture: dict) -> list[str]:
    candidates: list[str] = []

    primary = str(map_texture.get("texturePath") or "").strip()
    if primary:
        candidates.append(primary)

    for candidate in map_texture.get("texturePathCandidates") or []:
        value = str(candidate or "").strip()
        if value and value not in candidates:
            candidates.append(value)

    return candidates


def read_be_int(data: bytes, offset: int, size: int, *, signed: bool = False) -> int:
    end = offset + size
    if offset < 0 or end > len(data):
        raise ValueError("Unexpected end of EXD/EXH payload.")
    return int.from_bytes(data[offset:end], byteorder="big", signed=signed)


def read_null_terminated_utf8(data: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(data):
        return ""

    terminator = data.find(b"\x00", offset)
    if terminator < 0:
        terminator = len(data)
    return data[offset:terminator].decode("utf-8", errors="ignore").strip()


def normalize_map_path_like(raw_path: object) -> str:
    value = str(raw_path or "").replace("\\", "/").strip().strip("\0")
    if not value:
        return ""

    marker = "ui/map/"
    marker_index = value.lower().find(marker)
    if marker_index >= 0:
        value = value[marker_index + len(marker):]

    return value.strip("/")


def build_map_texture_candidates_from_path_like(raw_path: object) -> list[str]:
    normalized = normalize_map_path_like(raw_path)
    if not normalized:
        return []

    if normalized.lower().endswith(".tex"):
        candidate = normalized if normalized.lower().startswith("ui/map/") else f"ui/map/{normalized}"
        return [candidate]

    file_stem = normalized.replace("/", "")
    if not file_stem:
        return []

    return [f"ui/map/{normalized}/{file_stem}{suffix}.tex" for suffix in MAP_DIFFUSE_SUFFIXES]


def load_excel_sheet_header(game_data: object, parsed_file_name_type: type, sheet_name: str) -> dict:
    relative_path = f"exd/{sheet_name}.exh"
    raw_data = extract_raw_file(game_data, parsed_file_name_type, relative_path)
    if raw_data[:4] != EXCEL_HEADER_MAGIC:
        raise ValueError(f"Invalid EXH header for {relative_path}.")

    data_offset = read_be_int(raw_data, 6, 2)
    column_count = read_be_int(raw_data, 8, 2)
    page_count = read_be_int(raw_data, 10, 2)
    language_count = read_be_int(raw_data, 12, 2)

    columns: list[tuple[int, int]] = []
    for index in range(column_count):
        base_offset = 32 + (index * 4)
        column_type = read_be_int(raw_data, base_offset, 2)
        column_offset = read_be_int(raw_data, base_offset + 2, 2)
        columns.append((column_type, column_offset))

    page_base = 32 + (column_count * 4)
    pages: list[int] = []
    for index in range(page_count):
        base_offset = page_base + (index * 8)
        start_row_id = read_be_int(raw_data, base_offset, 4)
        pages.append(start_row_id)

    language_base = page_base + (page_count * 8)
    languages: list[int] = []
    for index in range(language_count):
        language_code = read_be_int(raw_data, language_base + (index * 2), 2)
        if language_code != 0:
            languages.append(language_code)

    return {
        "data_offset": data_offset,
        "columns": columns,
        "pages": pages,
        "languages": languages,
    }


def build_excel_data_path_candidates(sheet_name: str, page_start_row_id: int) -> list[str]:
    candidates = [f"exd/{sheet_name}_{page_start_row_id}.exd"]
    for suffix in DEFAULT_EXD_LANGUAGE_SUFFIXES:
        candidates.append(f"exd/{sheet_name}_{page_start_row_id}_{suffix}.exd")
    return candidates


def load_first_existing_excel_data_file(
    game_data: object,
    parsed_file_name_type: type,
    sheet_name: str,
    page_start_row_id: int,
) -> bytes | None:
    for relative_path in build_excel_data_path_candidates(sheet_name, page_start_row_id):
        try:
            return extract_raw_file(game_data, parsed_file_name_type, relative_path)
        except Exception:
            continue
    return None


def read_excel_string_column_from_row(row_data: bytes, column_offset: int, data_offset: int) -> str:
    string_offset = read_be_int(row_data, column_offset, 4, signed=True)
    return read_null_terminated_utf8(row_data, data_offset + string_offset)


def resolve_map_id_paths_from_sheet(
    game_data: object,
    parsed_file_name_type: type,
    requested_map_ids: set[int],
) -> dict[int, str]:
    if not requested_map_ids:
        return {}

    header = load_excel_sheet_header(game_data, parsed_file_name_type, MAP_SHEET_NAME)
    columns = header["columns"]
    if MAP_SHEET_MAP_ID_COLUMN_INDEX >= len(columns):
        raise ValueError(f"Map sheet is missing column index {MAP_SHEET_MAP_ID_COLUMN_INDEX}.")

    map_id_column_type, map_id_column_offset = columns[MAP_SHEET_MAP_ID_COLUMN_INDEX]
    if map_id_column_type != EXCEL_COLUMN_TYPE_STRING:
        raise ValueError(f"Map sheet column {MAP_SHEET_MAP_ID_COLUMN_INDEX} is not a string column.")

    resolved: dict[int, str] = {}
    remaining = {int(map_id) for map_id in requested_map_ids if int(map_id) > 0}
    for page_start_row_id in header["pages"]:
        if not remaining:
            break

        raw_page = load_first_existing_excel_data_file(game_data, parsed_file_name_type, MAP_SHEET_NAME, page_start_row_id)
        if raw_page is None or raw_page[:4] != EXCEL_DATA_MAGIC:
            continue

        row_table_size = read_be_int(raw_page, 8, 4)
        row_table_base = 32
        for row_table_offset in range(0, row_table_size, 8):
            row_base = row_table_base + row_table_offset
            row_id = read_be_int(raw_page, row_base, 4)
            if row_id not in remaining:
                continue

            data_offset = read_be_int(raw_page, row_base + 4, 4)
            if data_offset < 0 or data_offset + 6 > len(raw_page):
                continue

            entry_size = read_be_int(raw_page, data_offset, 4)
            row_data_start = data_offset + 6
            row_data_end = row_data_start + entry_size
            if row_data_end > len(raw_page):
                continue

            row_data = raw_page[row_data_start:row_data_end]
            map_path = read_excel_string_column_from_row(row_data, map_id_column_offset, header["data_offset"])
            if not map_path:
                continue

            resolved[row_id] = map_path
            remaining.remove(row_id)

    return resolved


def enrich_plan_map_textures_from_map_ids(plan: dict, game_data: object, parsed_file_name_type: type) -> bool:
    map_ids = {
        int(value)
        for value in plan.get("mapIds", [])
        if value not in (None, "") and str(value).strip().isdigit() and int(value) > 0
    }
    if not map_ids:
        return False

    existing_entries: list[dict] = []
    existing_map_ids: set[int] = set()
    for entry in plan.get("mapTextures", []):
        if not isinstance(entry, dict):
            continue

        candidate_paths = get_map_texture_candidates(entry)
        if not candidate_paths:
            continue

        normalized_entry = {
            "mapId": entry.get("mapId"),
            "texturePath": candidate_paths[0],
            "texturePathCandidates": candidate_paths,
            "offsetX": entry.get("offsetX"),
            "offsetY": entry.get("offsetY"),
            "sizeFactor": entry.get("sizeFactor"),
        }
        existing_entries.append(normalized_entry)

        try:
            map_id_value = int(entry.get("mapId"))
        except (TypeError, ValueError):
            continue
        if map_id_value > 0:
            existing_map_ids.add(map_id_value)

    missing_map_ids = map_ids - existing_map_ids
    if not missing_map_ids:
        return False

    resolved_paths = resolve_map_id_paths_from_sheet(game_data, parsed_file_name_type, missing_map_ids)
    if not resolved_paths:
        return False

    changed = False
    for map_id in sorted(missing_map_ids):
        map_path = resolved_paths.get(map_id, "")
        candidate_paths = build_map_texture_candidates_from_path_like(map_path)
        if not candidate_paths:
            continue

        existing_entries.append(
            {
                "mapId": map_id,
                "texturePath": candidate_paths[0],
                "texturePathCandidates": candidate_paths,
            }
        )
        changed = True

    if not changed:
        return False

    existing_entries.sort(key=lambda entry: (int(entry.get("mapId") or 0), str(entry.get("texturePath") or "")))
    plan["mapTextures"] = existing_entries

    goals = plan.setdefault("goals", {})
    map_goal = goals.setdefault("mapTiles", {})
    map_goal["count"] = len(existing_entries)
    map_goal["status"] = "ready_to_extract" if existing_entries else "waiting_for_data"

    summary = plan.setdefault("summary", {})
    summary["maps"] = len(existing_entries)
    return True


def get_string_column_offsets(header: dict) -> list[int]:
    offsets: list[int] = []
    for column_type, column_offset in header.get("columns", []):
        if column_type == EXCEL_COLUMN_TYPE_STRING:
            offsets.append(column_offset)
    return offsets


def resolve_named_sheet_rows(
    game_data: object,
    parsed_file_name_type: type,
    sheet_name: str,
    requested_row_ids: set[int],
) -> dict[int, dict[str, str]]:
    if not requested_row_ids:
        return {}

    header = load_excel_sheet_header(game_data, parsed_file_name_type, sheet_name)
    string_offsets = get_string_column_offsets(header)
    if not string_offsets:
        return {}

    resolved: dict[int, dict[str, str]] = {}
    remaining = {int(row_id) for row_id in requested_row_ids if int(row_id) > 0}
    for page_start_row_id in header["pages"]:
        if not remaining:
            break

        raw_page = load_first_existing_excel_data_file(game_data, parsed_file_name_type, sheet_name, page_start_row_id)
        if raw_page is None or raw_page[:4] != EXCEL_DATA_MAGIC:
            continue

        row_table_size = read_be_int(raw_page, 8, 4)
        row_table_base = 32
        for row_table_offset in range(0, row_table_size, 8):
            row_base = row_table_base + row_table_offset
            row_id = read_be_int(raw_page, row_base, 4)
            if row_id not in remaining:
                continue

            data_offset = read_be_int(raw_page, row_base + 4, 4)
            if data_offset < 0 or data_offset + 6 > len(raw_page):
                continue

            entry_size = read_be_int(raw_page, data_offset, 4)
            row_data_start = data_offset + 6
            row_data_end = row_data_start + entry_size
            if row_data_end > len(raw_page):
                continue

            row_data = raw_page[row_data_start:row_data_end]
            labels: list[str] = []
            for string_offset in string_offsets:
                label = read_excel_string_column_from_row(row_data, string_offset, header["data_offset"])
                if label:
                    labels.append(label)

            if not labels:
                continue

            resolved[row_id] = {
                "masculineName": labels[0],
                "feminineName": labels[1] if len(labels) > 1 and labels[1] else labels[0],
            }
            remaining.remove(row_id)

    return resolved


def build_monogram(label: str, fallback: str) -> str:
    sanitized = "".join(ch if ch.isalnum() or ch.isspace() else " " for ch in str(label or "").upper())
    tokens = [token for token in sanitized.split() if token]
    if not tokens:
        return fallback

    if len(tokens) >= 2:
        joined = "".join(token[0] for token in tokens[:2])
        return joined[:3] or fallback

    token = tokens[0]
    return token[:3] if len(token) >= 3 else token[:2] or fallback


def build_icon_palette(seed: int) -> tuple[str, str, str]:
    return ICON_PALETTES[(max(1, seed) - 1) % len(ICON_PALETTES)]


def guess_race_id_from_tribe_id(tribe_id: int) -> int:
    return max(1, ((max(1, tribe_id) - 1) // 2) + 1)


def build_sheet_icon_svg(monogram: str, accent_label: str, palette: tuple[str, str, str], title: str) -> bytes:
    background, accent, foreground = palette
    safe_monogram = html.escape(monogram[:3])
    safe_accent = html.escape((accent_label or "").upper()[:8])
    safe_title = html.escape(title or monogram)
    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" role="img" aria-label="{safe_title}">
  <title>{safe_title}</title>
  <defs>
    <linearGradient id="g" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="{background}"/>
      <stop offset="100%" stop-color="{accent}"/>
    </linearGradient>
  </defs>
  <rect x="2" y="2" width="60" height="60" rx="16" fill="url(#g)"/>
  <rect x="6" y="6" width="52" height="13" rx="8" fill="rgba(7,16,24,0.34)"/>
  <rect x="6" y="48" width="52" height="10" rx="6" fill="rgba(7,16,24,0.20)"/>
  <text x="32" y="15" text-anchor="middle" font-family="Segoe UI,Tahoma,sans-serif" font-size="8" font-weight="700" fill="{foreground}" letter-spacing="1.1">{safe_accent}</text>
  <text x="32" y="40" text-anchor="middle" font-family="Segoe UI,Tahoma,sans-serif" font-size="21" font-weight="800" fill="{foreground}" letter-spacing="0.8">{safe_monogram}</text>
  <circle cx="13" cy="51" r="3" fill="{foreground}" opacity="0.82"/>
  <circle cx="51" cy="51" r="3" fill="{foreground}" opacity="0.82"/>
</svg>
"""
    return svg.encode("utf-8")


def write_generated_file(output_root: str, relative_path: str, data: bytes) -> str:
    normalized_relative = relative_path.replace("/", os.sep)
    destination = os.path.join(output_root, "generated", normalized_relative)
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    with open(destination, "wb") as handle:
        handle.write(data)
    return destination


def write_json_file(path: str, payload: dict) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")


def build_summary(plan: dict, game_root: str, luminapie_root: str) -> dict:
    return {
        "planGeneratedAtUtc": plan.get("generatedAtUtc"),
        "samePcCaptured": bool(plan.get("samePcCaptured")),
        "gameRoot": game_root,
        "luminapieRoot": luminapie_root,
        "territoryIds": plan.get("territoryIds", []),
        "mapIds": plan.get("mapIds", []),
        "raceIds": plan.get("raceIds", []),
        "tribeIds": plan.get("tribeIds", []),
        "jobIds": plan.get("jobIds", []),
        "jobIconIds": plan.get("jobIconIds", []),
        "enemyDataIds": plan.get("enemyDataIds", []),
        "jobIconTexPaths": plan.get("jobIconTexPaths", []),
        "mapTextures": plan.get("mapTextures", []),
    }


def write_summary(summary_path: str, payload: dict) -> None:
    write_json_file(summary_path, payload)


def main() -> int:
    args = parse_args()
    plan = load_plan(args.plan)
    candidates = get_luminapie_candidates()

    try:
        bindings = bootstrap_luminapie()
        game_root = resolve_game_root(args.game_root, plan)
        os.makedirs(args.output_root, exist_ok=True)

        game_data = bindings.game_data_type(game_root, load_schema=False)
        enrich_plan_map_textures_from_map_ids(plan, game_data, bindings.parsed_file_name_type)
        race_ids = {
            int(value)
            for value in plan.get("raceIds", [])
            if value not in (None, "") and str(value).strip().isdigit() and int(value) > 0
        }
        tribe_ids = {
            int(value)
            for value in plan.get("tribeIds", [])
            if value not in (None, "") and str(value).strip().isdigit() and int(value) > 0
        }
        supplemental_race_ids = {guess_race_id_from_tribe_id(tribe_id) for tribe_id in tribe_ids}
        race_rows = resolve_named_sheet_rows(
            game_data,
            bindings.parsed_file_name_type,
            RACE_SHEET_NAME,
            race_ids | supplemental_race_ids,
        )
        tribe_rows = resolve_named_sheet_rows(
            game_data,
            bindings.parsed_file_name_type,
            TRIBE_SHEET_NAME,
            tribe_ids,
        )
        write_json_file(args.plan, plan)
        summary = build_summary(plan, game_root, bindings.source_root)
        summary["status"] = "ok"
        summary["extractedFiles"] = []
        summary["failedFiles"] = []
        map_textures = [entry for entry in plan.get("mapTextures", []) if isinstance(entry, dict) and get_map_texture_candidates(entry)]
        summary["unresolvedTargets"] = {
            "mapTiles": {
                "status": "needs_live_map_texture_paths" if not map_textures else "extracting",
                "mapIds": plan.get("mapIds", []),
                "mapTextures": map_textures,
            },
            "raceIcons": {
                "status": "generating" if (race_ids or tribe_ids) else "waiting_for_data",
                "raceIds": sorted(race_ids),
                "tribeIds": sorted(tribe_ids),
            },
        }

        for relative_path in plan.get("jobIconTexPaths", []):
            try:
                raw_data = extract_raw_file(game_data, bindings.parsed_file_name_type, relative_path)
                destination = write_file(args.output_root, relative_path, raw_data)
                summary["extractedFiles"].append(
                    {
                        "kind": "jobIcon",
                        "jobIconId": int(os.path.splitext(os.path.basename(relative_path))[0].split("_", 1)[0]),
                        "relativePath": relative_path,
                        "outputPath": destination,
                        "size": len(raw_data),
                    }
                )
            except Exception as exc:
                summary["failedFiles"].append(
                    {
                        "kind": "jobIcon",
                        "jobIconId": int(os.path.splitext(os.path.basename(relative_path))[0].split("_", 1)[0]),
                        "relativePath": relative_path,
                        "error": str(exc),
                    }
                )

        for map_texture in map_textures:
            candidate_paths = get_map_texture_candidates(map_texture)
            if not candidate_paths:
                continue

            extracted = False
            candidate_failures: list[dict[str, str]] = []

            for relative_path in candidate_paths:
                try:
                    raw_data = extract_raw_file(game_data, bindings.parsed_file_name_type, relative_path)
                    destination = write_file(args.output_root, relative_path, raw_data)
                    summary["extractedFiles"].append(
                        {
                            "kind": "mapTexture",
                            "mapId": map_texture.get("mapId"),
                            "relativePath": relative_path,
                            "candidatePaths": candidate_paths,
                            "outputPath": destination,
                            "size": len(raw_data),
                            "offsetX": map_texture.get("offsetX"),
                            "offsetY": map_texture.get("offsetY"),
                            "sizeFactor": map_texture.get("sizeFactor"),
                        }
                    )
                    extracted = True
                    break
                except Exception as exc:
                    candidate_failures.append(
                        {
                            "relativePath": relative_path,
                            "error": str(exc),
                        }
                    )

            if not extracted:
                summary["failedFiles"].append(
                    {
                        "kind": "mapTexture",
                        "mapId": map_texture.get("mapId"),
                        "relativePath": candidate_paths[0],
                        "candidatePaths": candidate_paths,
                        "candidateFailures": candidate_failures,
                        "error": "; ".join(f"{entry['relativePath']}: {entry['error']}" for entry in candidate_failures),
                    }
                )

        for race_id in sorted(race_ids):
            row = race_rows.get(race_id)
            if row is None:
                summary["failedFiles"].append(
                    {
                        "kind": "raceIcon",
                        "raceId": race_id,
                        "error": f"Race sheet row {race_id} could not be resolved.",
                    }
                )
                continue

            race_name = row.get("masculineName") or row.get("feminineName") or f"Race {race_id}"
            svg_data = build_sheet_icon_svg(
                monogram=build_monogram(race_name, f"R{race_id}"),
                accent_label="RACE",
                palette=build_icon_palette(race_id),
                title=race_name,
            )
            destination = write_generated_file(args.output_root, f"race-icons/race_{race_id:03d}.svg", svg_data)
            summary["extractedFiles"].append(
                {
                    "kind": "raceIcon",
                    "raceId": race_id,
                    "outputPath": destination,
                    "masculineName": row.get("masculineName"),
                    "feminineName": row.get("feminineName"),
                    "size": len(svg_data),
                }
            )

        for tribe_id in sorted(tribe_ids):
            row = tribe_rows.get(tribe_id)
            if row is None:
                summary["failedFiles"].append(
                    {
                        "kind": "tribeIcon",
                        "tribeId": tribe_id,
                        "error": f"Tribe sheet row {tribe_id} could not be resolved.",
                    }
                )
                continue

            tribe_name = row.get("masculineName") or row.get("feminineName") or f"Tribe {tribe_id}"
            race_id = guess_race_id_from_tribe_id(tribe_id)
            race_name = race_rows.get(race_id, {}).get("masculineName") or "CLAN"
            svg_data = build_sheet_icon_svg(
                monogram=build_monogram(tribe_name, f"T{tribe_id}"),
                accent_label=build_monogram(race_name, "CLN"),
                palette=build_icon_palette(race_id),
                title=tribe_name,
            )
            destination = write_generated_file(args.output_root, f"tribe-icons/tribe_{tribe_id:03d}.svg", svg_data)
            summary["extractedFiles"].append(
                {
                    "kind": "tribeIcon",
                    "tribeId": tribe_id,
                    "raceId": race_id,
                    "outputPath": destination,
                    "masculineName": row.get("masculineName"),
                    "feminineName": row.get("feminineName"),
                    "raceMasculineName": race_rows.get(race_id, {}).get("masculineName"),
                    "raceFeminineName": race_rows.get(race_id, {}).get("feminineName"),
                    "size": len(svg_data),
                }
            )

        extracted_map_entries = [entry for entry in summary["extractedFiles"] if entry.get("kind") == "mapTexture"]
        failed_map_entries = [entry for entry in summary["failedFiles"] if entry.get("kind") == "mapTexture"]
        extracted_race_entries = [
            entry for entry in summary["extractedFiles"] if entry.get("kind") in {"raceIcon", "tribeIcon"}
        ]
        failed_race_entries = [
            entry for entry in summary["failedFiles"] if entry.get("kind") in {"raceIcon", "tribeIcon"}
        ]
        summary["unresolvedTargets"]["mapTiles"] = {
            "status": (
                "needs_live_map_texture_paths"
                if not map_textures
                else "partial_failure"
                if failed_map_entries
                else "ready_from_extracted_textures"
            ),
            "mapIds": plan.get("mapIds", []),
            "mapTextures": map_textures,
            "failedMapIds": [entry.get("mapId") for entry in failed_map_entries if entry.get("mapId") not in (None, "")],
            "extractedMapIds": [entry.get("mapId") for entry in extracted_map_entries if entry.get("mapId") not in (None, "")],
        }
        summary["unresolvedTargets"]["raceIcons"] = {
            "status": (
                "waiting_for_data"
                if not race_ids and not tribe_ids
                else "partial_failure"
                if failed_race_entries
                else "ready_from_generated_sheet_icons"
            ),
            "raceIds": sorted(race_ids),
            "tribeIds": sorted(tribe_ids),
            "extractedRaceIds": [entry.get("raceId") for entry in summary["extractedFiles"] if entry.get("kind") == "raceIcon"],
            "extractedTribeIds": [entry.get("tribeId") for entry in summary["extractedFiles"] if entry.get("kind") == "tribeIcon"],
            "failedRaceIds": [entry.get("raceId") for entry in summary["failedFiles"] if entry.get("kind") == "raceIcon"],
            "failedTribeIds": [entry.get("tribeId") for entry in summary["failedFiles"] if entry.get("kind") == "tribeIcon"],
        }

        summary["counts"] = {
            "requestedTextureFiles": len(plan.get("jobIconTexPaths", [])) + len(map_textures),
            "extractedTextureFiles": len([entry for entry in summary["extractedFiles"] if entry.get("kind") in {"jobIcon", "mapTexture"}]),
            "failedTextureFiles": len([entry for entry in summary["failedFiles"] if entry.get("kind") in {"jobIcon", "mapTexture"}]),
            "requestedJobIconFiles": len(plan.get("jobIconTexPaths", [])),
            "extractedJobIconFiles": sum(1 for entry in summary["extractedFiles"] if entry.get("kind") == "jobIcon"),
            "failedJobIconFiles": sum(1 for entry in summary["failedFiles"] if entry.get("kind") == "jobIcon"),
            "requestedMapTextureFiles": len(map_textures),
            "extractedMapTextureFiles": len(extracted_map_entries),
            "failedMapTextureFiles": len(failed_map_entries),
            "requestedRaceIconFiles": len(race_ids),
            "extractedRaceIconFiles": sum(1 for entry in summary["extractedFiles"] if entry.get("kind") == "raceIcon"),
            "failedRaceIconFiles": sum(1 for entry in summary["failedFiles"] if entry.get("kind") == "raceIcon"),
            "requestedTribeIconFiles": len(tribe_ids),
            "extractedTribeIconFiles": sum(1 for entry in summary["extractedFiles"] if entry.get("kind") == "tribeIcon"),
            "failedTribeIconFiles": sum(1 for entry in summary["failedFiles"] if entry.get("kind") == "tribeIcon"),
            "requestedGeneratedIconFiles": len(race_ids) + len(tribe_ids),
            "extractedGeneratedIconFiles": len(extracted_race_entries),
            "failedGeneratedIconFiles": len(failed_race_entries),
            "mapIds": len(plan.get("mapIds", [])),
            "raceIds": len(plan.get("raceIds", [])),
            "tribeIds": len(plan.get("tribeIds", [])),
            "enemyDataIds": len(plan.get("enemyDataIds", [])),
        }

        write_summary(args.summary, summary)

        print(f"Game root: {game_root}")
        print(f"Luminapie root: {bindings.source_root}")
        print(f"Checked luminapie candidates: {', '.join(candidates)}")
        print(
            "Extracted "
            f"{summary['counts']['extractedJobIconFiles']} / {summary['counts']['requestedJobIconFiles']} requested job icon texture(s) "
            "and "
            f"{summary['counts']['extractedMapTextureFiles']} / {summary['counts']['requestedMapTextureFiles']} requested map texture(s), "
            f"{summary['counts']['extractedRaceIconFiles']} / {summary['counts']['requestedRaceIconFiles']} race icon(s), and "
            f"{summary['counts']['extractedTribeIconFiles']} / {summary['counts']['requestedTribeIconFiles']} tribe icon(s)."
        )
        if summary["failedFiles"]:
            print(f"Failed {len(summary['failedFiles'])} file(s). See {args.summary}.")
        else:
            print(f"Summary written to {args.summary}")
        return 0
    except Exception as exc:
        failure_summary = {
            "status": "failed",
            "generatedAtUtc": plan.get("generatedAtUtc"),
            "samePcCaptured": bool(plan.get("samePcCaptured")),
            "gameRoot": args.game_root or str(plan.get("gameInstallPath") or ""),
            "scriptRoot": SCRIPT_ROOT,
            "luminapieCandidates": candidates,
            "errorType": type(exc).__name__,
            "error": str(exc),
            "planSummary": {
                "territoryIds": plan.get("territoryIds", []),
                "mapIds": plan.get("mapIds", []),
                "raceIds": plan.get("raceIds", []),
                "tribeIds": plan.get("tribeIds", []),
                "jobIds": plan.get("jobIds", []),
                "jobIconIds": plan.get("jobIconIds", []),
                "jobIconTexPaths": plan.get("jobIconTexPaths", []),
                "mapTextures": plan.get("mapTextures", []),
                "enemyDataIds": plan.get("enemyDataIds", []),
            },
        }

        if isinstance(exc, LuminapieBootstrapError):
            failure_summary["hint"] = exc.hint
            failure_summary["luminapieProbeResults"] = [
                {
                    "candidate": result.candidate,
                    "exists": result.exists,
                    "luminapiePackageExists": result.luminapie_package_exists,
                    "importError": result.import_error,
                }
                for result in exc.results
            ]

        write_summary(args.summary, failure_summary)
        print(f"Extractor failed: {type(exc).__name__}: {exc} | Summary: {args.summary}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
