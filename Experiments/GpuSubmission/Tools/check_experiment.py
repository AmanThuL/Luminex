#!/usr/bin/env python3
"""Apply production compiler-backed conventions explicitly to the frozen experiment."""

from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
EXPERIMENT = ROOT / "Experiments/GpuSubmission"
sys.path.insert(0, str(ROOT / "Tools"))
import check_cpp_comments as comments
import check_cpp_layout as layout


def main():
    files = sorted(p for p in EXPERIMENT.rglob("*") if p.suffix in {".h", ".cpp"})
    errors = []
    database = ROOT / "compile_commands.json"
    client = None
    try:
        formatter = shutil.which("clang-format")
        clangd = shutil.which("clangd")
        if not formatter or not clangd:
            raise RuntimeError("clang-format and clangd must be on PATH")
        result = subprocess.run([formatter, "--dry-run", "--Werror", *map(str, files)], cwd=ROOT)
        if result.returncode:
            errors.append("experiment formatting failed")
        implementations = [p for p in files if p.suffix == ".cpp"]
        layout.ensure_database_covers(implementations, database, ROOT)
        entries = comments._compile_entries(database)
        client = layout.ClangdClient(clangd, ROOT, database)
        client.initialize()
        definitions = 0
        for path in files:
            relative = path.relative_to(ROOT)
            source = path.read_text(encoding="utf-8")
            errors.extend(comments.check_file_header(relative, source))
            if path.suffix == ".h":
                ast, error = comments._ast_for_header(path, entries, ROOT)
                if error:
                    errors.append(f"{relative}: {error}")
                else:
                    errors.extend(comments.public_api_findings_from_ast(relative, source, ast))
            else:
                starts = client.definition_lines(path, source)
                definitions += len(starts)
                errors.extend(layout.check_layout(relative, source, starts))
    except (OSError, RuntimeError, ValueError) as error:
        errors.append(str(error))
    finally:
        if client:
            client.close()
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"Experiment checks passed ({len(files)} files, {definitions} parsed definitions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
