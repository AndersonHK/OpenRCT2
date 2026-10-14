"""Bound scratch retention without changing qualification or historical receipts."""
import hashlib
import json
import os
from pathlib import Path
import stat


def regular_files(directory):
    """Never follow Windows junctions, file symlinks or other reparse points."""
    if not directory.exists():
        return
    pending = [directory]
    while pending:
        parent = pending.pop()
        for path in parent.iterdir():
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                continue
            if stat.S_ISDIR(info.st_mode):
                pending.append(path)
            elif stat.S_ISREG(info.st_mode):
                yield path


def checked_output(output):
    namespace = Path(__file__).resolve().parents[2] / "obj/vulkan-parity"
    output = Path(os.path.abspath(output))
    if output == namespace or namespace not in output.parents:
        raise ValueError("Automatic cleanup is restricted to a child of obj/vulkan-parity")
    # Check lexical ancestors BEFORE resolving, so an in-tree redirect cannot
    # cause deletion through a link, even when it points elsewhere in the tree.
    for path in [output, *output.parents]:
        if path.exists():
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError("Refusing cleanup through a reparse point: " + str(path))
        if path == namespace:
            break
    return output


def compact(output, *, passed, retain=False, kind="terrain", directories=(), journal_root=None):
    """Hash and journal leaf deletions only after the caller finishes auditing inputs.

    Failures retain all outputs. Images, columns/status, reports, shaders, runnable
    artifacts and library reuse inputs remain. No sibling run is ever removed.
    """
    if not passed or retain:
        return {"policy": "full-failure" if not passed else "full-requested", "removedBytes": 0}
    output = checked_output(output)
    selected = []
    for name in directories:
        if Path(name).is_absolute() or ".." in Path(name).parts:
            raise ValueError("Scratch subtree must be a contained relative path")
        directory = Path(os.path.abspath(output / name))
        if directory == output or output not in directory.parents:
            raise ValueError("Invalid scratch subtree")
        # Validate the selected subtree as well as its output ancestors.
        checked_output(directory)
        for path in regular_files(directory):
            if kind == "terrain" and path.name not in {"before.bin", "parents.bin", "commands.bin"}:
                continue
            digest = hashlib.sha256()
            with path.open("rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(block)
            info = path.stat()
            selected.append({"path": path.relative_to(output).as_posix(), "bytes": info.st_size,
                             "mtimeNs": info.st_mtime_ns,
                             "sha256": digest.hexdigest()})
    journal = checked_output(journal_root or output) / ("scratch-retention-" + kind + ".json")
    if journal.exists():
        raise ValueError("Retention journal already exists")
    receipt = {"schema": 1, "policy": "compact-success", "kind": kind, "status": "pending",
               "artifactRoot": str(output),
               "note": "Historical artifact hashes describe outputs before pruning; removed payloads are unavailable.",
               "removedBytes": 0, "discarded": selected}
    journal.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    try:
        for entry in selected:
            path = output / entry["path"]
            checked_output(path.parent)
            info = path.lstat()
            if not stat.S_ISREG(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError("Scratch file became a link")
            if info.st_size != entry["bytes"] or info.st_mtime_ns != entry["mtimeNs"]:
                raise ValueError("Scratch file changed during cleanup")
            path.unlink()
            receipt["removedBytes"] += entry["bytes"]
        receipt["status"] = "complete"
    except BaseException:
        receipt["status"] = "incomplete"
        raise
    finally:
        journal.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return {key: receipt[key] for key in ("policy", "status", "removedBytes")}
