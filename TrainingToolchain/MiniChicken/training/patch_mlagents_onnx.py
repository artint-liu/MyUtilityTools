"""Fix ml-agents 1.0.0 ONNX export on torch >= 2.9 (idempotent).

Problem
-------
PyTorch 2.9+ made ``dynamo=True`` the default for ``torch.onnx.export``. The new
exporter imports ``onnxscript`` (not installable here: it drags in protobuf>=4.25
and numpy 2.x, which breaks mlagents-envs 1.0.0) and it does not support the
opset 9 graph that Unity Barracuda consumes. ml-agents 1.0.0 calls
``torch.onnx.export(...)`` without ``dynamo=``, so every checkpoint throws
``ModuleNotFoundError: No module named 'onnxscript'``.

Because exporting runs on the threaded trainer, that exception kills
``trainer_update_func`` while the main loop keeps waiting on Unity -> training
hangs forever with no error on the console.

Fix
---
1. Force the legacy TorchScript exporter (``dynamo=False``) - skipped on torch
   versions that have no ``dynamo`` parameter (e.g. 2.0.1, already legacy-only).
2. Make ONNX export failures non-fatal, so a serialization problem can never
   silently freeze a long run again (the .pt checkpoint is already on disk).

Usage
-----
    python training/patch_mlagents_onnx.py            # patch
    python training/patch_mlagents_onnx.py --check    # report only, exit 1 if unpatched

Re-run after recreating the venv or reinstalling mlagents.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import List

REPO_ROOT = Path(__file__).resolve().parent.parent


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _write(path: Path, text: str) -> None:
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def _find(pkg: str, rel_path: str) -> Path:
    """Locate a module inside the interpreter's site-packages."""
    for base in sys.path:
        candidate = Path(base) / pkg
        if (candidate / "__init__.py").is_file():
            target = candidate / rel_path
            if target.is_file():
                return target
    raise FileNotFoundError(f"Could not find {pkg}/{rel_path} in {sys.path}")


def _torch_needs_dynamo_patch() -> bool:
    """True only when the installed torch accepts ``dynamo=`` (2.6+).

    torch 2.0.1 (the non-Blackwell cu118 path) has no ``dynamo`` parameter, so
    adding it would raise ``TypeError`` at export time.
    """
    try:
        import inspect

        from mlagents.torch_utils import torch
    except Exception:
        return False
    try:
        return "dynamo" in inspect.signature(torch.onnx.export).parameters
    except (TypeError, ValueError):
        return False


def patch_model_serialization(apply: bool) -> List[str]:
    path = _find("mlagents", "trainers/torch_entities/model_serialization.py")
    src = _read(path)
    actions: List[str] = []

    if "dynamo=False" in src:
        return [f"{path}: already patched"]

    if not _torch_needs_dynamo_patch():
        # Legacy exporter is the only one available -> nothing to force.
        return [f"{path}: torch exporter needs no patch"]

    if "import warnings" not in src:
        if not apply:
            return [f"{path}: missing 'import warnings'"]
        src = src.replace(
            "import threading\n",
            "import threading\nimport warnings\n",
            1,
        )
        actions.append("added 'import warnings'")

    old_call = (
        "        with exporting_to_onnx():\n"
        "            torch.onnx.export(\n"
        "                self.policy.actor,\n"
        "                self.dummy_input,\n"
        "                onnx_output_path,\n"
        "                opset_version=SerializationSettings.onnx_opset,\n"
        "                input_names=self.input_names,\n"
        "                output_names=self.output_names,\n"
        "                dynamic_axes=self.dynamic_axes,\n"
        "            )\n"
    )
    new_call = (
        "        with exporting_to_onnx(), warnings.catch_warnings():\n"
        "            # PyTorch >= 2.9 defaults to the new torch.export-based ONNX\n"
        "            # exporter (dynamo=True), which needs `onnxscript` and cannot\n"
        "            # emit the opset 9 graph Unity Barracuda expects. Force the\n"
        "            # legacy TorchScript exporter.\n"
        "            warnings.simplefilter(\"ignore\", DeprecationWarning)\n"
        "            torch.onnx.export(\n"
        "                self.policy.actor,\n"
        "                self.dummy_input,\n"
        "                onnx_output_path,\n"
        "                opset_version=SerializationSettings.onnx_opset,\n"
        "                input_names=self.input_names,\n"
        "                output_names=self.output_names,\n"
        "                dynamic_axes=self.dynamic_axes,\n"
        "                dynamo=False,\n"
        "            )\n"
    )

    if old_call in src:
        if not apply:
            return [f"{path}: torch.onnx.export not patched"]
        src = src.replace(old_call, new_call, 1)
        actions.append("forced legacy ONNX exporter (dynamo=False)")
    else:
        # Unknown shape (different mlagents version) - patch the export call only.
        marker = "                dynamic_axes=self.dynamic_axes,\n"
        if marker in src:
            if not apply:
                return [f"{path}: torch.onnx.export not patched"]
            src = src.replace(marker, marker + "                dynamo=False,\n", 1)
            actions.append("forced legacy ONNX exporter (dynamo=False)")
        else:
            return [f"ERROR: could not locate the ONNX export call in {path}"]

    if apply and actions:
        _write(path, src)
    return [f"{path}: " + (", ".join(actions) if actions else "already patched")]


def patch_model_saver(apply: bool) -> List[str]:
    path = _find("mlagents", "trainers/model_saver/torch_model_saver.py")
    src = _read(path)
    old = (
        "    def export(self, output_filepath: str, behavior_name: str) -> None:\n"
        "        if self.exporter is not None:\n"
        "            self.exporter.export_policy_model(output_filepath)\n"
    )
    new = (
        "    def export(self, output_filepath: str, behavior_name: str) -> None:\n"
        "        if self.exporter is not None:\n"
        "            try:\n"
        "                self.exporter.export_policy_model(output_filepath)\n"
        "            except Exception as exc:  # pragma: no cover - safety net\n"
        "                # The .pt checkpoint is already written at this point. Never\n"
        "                # let a serialization failure propagate: it runs on the threaded\n"
        "                # trainer and an unhandled exception there kills the trainer\n"
        "                # thread while the main loop keeps waiting on Unity.\n"
        "                logger.error(\n"
        "                    \"Failed to export ONNX model to %s.onnx. The .pt checkpoint \"\n"
        "                    \"is still valid and can be exported later. Error: %s\",\n"
        "                    output_filepath,\n"
        "                    exc,\n"
        "                    exc_info=True,\n"
        "                )\n"
    )

    if "Failed to export ONNX model" in src:
        return [f"{path}: already patched"]
    if old not in src:
        return [f"ERROR: could not locate TorchModelSaver.export in {path}"]
    if not apply:
        return [f"{path}: export failure guard missing"]
    _write(path, src.replace(old, new, 1))
    return [f"{path}: ONNX export failures are now non-fatal"]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="only report status, exit with code 1 if a patch is missing",
    )
    args = parser.parse_args()

    apply = not args.check
    results: List[str] = []
    for patcher in (patch_model_serialization, patch_model_saver):
        try:
            results.extend(patcher(apply))
        except FileNotFoundError as exc:
            results.append(f"ERROR: {exc}")

    verb = "Would patch" if args.check else "Patched"
    print(f"{verb} mlagents ONNX export (python: {sys.executable})")
    for line in results:
        print(f"  - {line}")

    failed = any(line.startswith("ERROR") or "not patched" in line or "missing" in line for line in results)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
