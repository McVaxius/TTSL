#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass


DEFAULT_PLAN_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ttsl_asset_plan.json")
DEFAULT_OUTPUT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "extracted")
DEFAULT_SUMMARY_PATH = os.path.join(DEFAULT_OUTPUT_ROOT, "ttsl_asset_extract_summary.json")
LUMINAPIE_CANDIDATES = [
    r"Z:\temp\awgil_clientstructs\ida",
    r"D:\temp\awgil_clientstructs\ida",
    r"Z:\_research\FFXIVClientStructs\ida",
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
    def __init__(self, results: list[LuminapieCandidateResult]) -> None:
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

        message = "Could not import luminapie from the local candidate roots. Checked: " + "; ".join(details)
        super().__init__(message)


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
    results: list[LuminapieCandidateResult] = []
    for candidate in LUMINAPIE_CANDIDATES:
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

    raise LuminapieBootstrapError(results)


def normalize_sqpack_payload(payload: object) -> bytes:
    if isinstance(payload, bytes):
        return payload
    if isinstance(payload, bytearray):
        return bytes(payload)
    if isinstance(payload, list):
        return b"".join(bytes(chunk) for chunk in payload)
    raise TypeError(f"Unsupported sqpack payload type: {type(payload)!r}")


def extract_raw_file(game_data: object, parsed_file_name_type: type, relative_path: str) -> bytes:
    parsed = parsed_file_name_type(relative_path)
    payload = game_data.get_file(parsed)
    return normalize_sqpack_payload(payload)


def write_file(output_root: str, relative_path: str, data: bytes) -> str:
    normalized_relative = relative_path.replace("/", os.sep)
    destination = os.path.join(output_root, "raw", normalized_relative)
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    with open(destination, "wb") as handle:
        handle.write(data)
    return destination


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
    }


def write_summary(summary_path: str, payload: dict) -> None:
    os.makedirs(os.path.dirname(summary_path), exist_ok=True)
    with open(summary_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2, sort_keys=True)
        handle.write("\n")


def main() -> int:
    args = parse_args()
    plan = load_plan(args.plan)

    try:
        bindings = bootstrap_luminapie()
        game_root = resolve_game_root(args.game_root, plan)
        os.makedirs(args.output_root, exist_ok=True)

        game_data = bindings.game_data_type(game_root, load_schema=False)
        summary = build_summary(plan, game_root, bindings.source_root)
        summary["status"] = "ok"
        summary["extractedFiles"] = []
        summary["failedFiles"] = []
        summary["unresolvedTargets"] = {
            "mapTiles": {
                "status": "needs_sheet_mapping",
                "mapIds": plan.get("mapIds", []),
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
                        "relativePath": relative_path,
                        "outputPath": destination,
                        "size": len(raw_data),
                    }
                )
            except Exception as exc:
                summary["failedFiles"].append(
                    {
                        "relativePath": relative_path,
                        "error": str(exc),
                    }
                )

        summary["counts"] = {
            "requestedJobIconFiles": len(plan.get("jobIconTexPaths", [])),
            "extractedJobIconFiles": len(summary["extractedFiles"]),
            "failedJobIconFiles": len(summary["failedFiles"]),
            "mapIds": len(plan.get("mapIds", [])),
            "raceIds": len(plan.get("raceIds", [])),
            "tribeIds": len(plan.get("tribeIds", [])),
            "enemyDataIds": len(plan.get("enemyDataIds", [])),
        }

        write_summary(args.summary, summary)

        print(f"Game root: {game_root}")
        print(f"Luminapie root: {bindings.source_root}")
        print(f"Checked luminapie candidates: {', '.join(LUMINAPIE_CANDIDATES)}")
        print(f"Extracted {summary['counts']['extractedJobIconFiles']} / {summary['counts']['requestedJobIconFiles']} requested job icon texture(s).")
        if summary["failedFiles"]:
            print(f"Failed {summary['counts']['failedJobIconFiles']} file(s). See {args.summary}.")
        else:
            print(f"Summary written to {args.summary}")
        return 0
    except Exception as exc:
        failure_summary = {
            "status": "failed",
            "generatedAtUtc": plan.get("generatedAtUtc"),
            "samePcCaptured": bool(plan.get("samePcCaptured")),
            "gameRoot": args.game_root or str(plan.get("gameInstallPath") or ""),
            "luminapieCandidates": LUMINAPIE_CANDIDATES,
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
                "enemyDataIds": plan.get("enemyDataIds", []),
            },
        }

        if isinstance(exc, LuminapieBootstrapError):
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
