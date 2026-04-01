#!/usr/bin/env python3
from __future__ import annotations

import argparse
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
    os.makedirs(os.path.dirname(summary_path), exist_ok=True)
    with open(summary_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")


def main() -> int:
    args = parse_args()
    plan = load_plan(args.plan)
    candidates = get_luminapie_candidates()

    try:
        bindings = bootstrap_luminapie()
        game_root = resolve_game_root(args.game_root, plan)
        os.makedirs(args.output_root, exist_ok=True)

        game_data = bindings.game_data_type(game_root, load_schema=False)
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
                "status": "needs_sheet_mapping",
                "raceIds": plan.get("raceIds", []),
                "tribeIds": plan.get("tribeIds", []),
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

        extracted_map_entries = [entry for entry in summary["extractedFiles"] if entry.get("kind") == "mapTexture"]
        failed_map_entries = [entry for entry in summary["failedFiles"] if entry.get("kind") == "mapTexture"]
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

        summary["counts"] = {
            "requestedTextureFiles": len(plan.get("jobIconTexPaths", [])) + len(map_textures),
            "extractedTextureFiles": len(summary["extractedFiles"]),
            "failedTextureFiles": len(summary["failedFiles"]),
            "requestedJobIconFiles": len(plan.get("jobIconTexPaths", [])),
            "extractedJobIconFiles": sum(1 for entry in summary["extractedFiles"] if entry.get("kind") == "jobIcon"),
            "failedJobIconFiles": sum(1 for entry in summary["failedFiles"] if entry.get("kind") == "jobIcon"),
            "requestedMapTextureFiles": len(map_textures),
            "extractedMapTextureFiles": len(extracted_map_entries),
            "failedMapTextureFiles": len(failed_map_entries),
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
            f"{summary['counts']['extractedMapTextureFiles']} / {summary['counts']['requestedMapTextureFiles']} requested map texture(s)."
        )
        if summary["failedFiles"]:
            print(f"Failed {summary['counts']['failedTextureFiles']} file(s). See {args.summary}.")
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
