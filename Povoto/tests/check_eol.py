"""Line endings of the tracked text files of Povoto/.

Rule: CRLF, except the files listed in tests/eol_lf_files.txt (they stay LF). A file with
mixed endings always fails. The git index is always LF (core.autocrlf=true); this checks the
working copy, where `sed -i` of Git Bash and the Write tool of Claude Code leave LF.

  python tests/check_eol.py          list the files out of the rule (exit code 1 if any)
  python tests/check_eol.py --fix    convert them (to CRLF; a mixed file in the list is made LF)
  python tests/check_eol.py --init   rewrite eol_lf_files.txt from the files that are LF now
"""
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
allow_file = root / "tests" / "eol_lf_files.txt"


def tracked():
    """(path relative to Povoto/, working-copy ending) of the tracked text files."""
    out = subprocess.run(["git", "-C", str(root), "ls-files", "--eol", "--", "."],
                         capture_output=True, text=True, check=True).stdout
    files = []
    for line in out.splitlines():
        info, path = line.split("\t", 1)
        index, work = info.split()[:2]
        if index == "i/-text" or work in ("w/-text", "w/none", "w/"):
            continue  # binary or empty
        files.append((path, work[2:]))
    return files


def convert(path, ending):
    data = (root / path).read_bytes().replace(b"\r\n", b"\n")
    if ending == "crlf":
        data = data.replace(b"\n", b"\r\n")
    (root / path).write_bytes(data)


def main():
    files = tracked()
    if "--init" in sys.argv:
        names = sorted(p for p, w in files if w == "lf" and p != "tests/eol_lf_files.txt")
        allow_file.write_bytes(("\r\n".join(names) + "\r\n").encode())
        print(f"{len(names)} files listed in tests/eol_lf_files.txt")
        return 0
    allowed = set()
    if allow_file.exists():
        allowed = {l.strip() for l in allow_file.read_text(encoding="utf-8").splitlines() if l.strip()}
    bad = []
    for path, work in files:
        want = "lf" if path in allowed else "crlf"
        if work != want:
            bad.append((path, work, want))
    for path, work, want in bad:
        print(f"{path}: {work}, expected {want}")
    stale = sorted(p for p in allowed if p not in {f[0] for f in files})
    for path in stale:
        print(f"note: {path} is in eol_lf_files.txt but is not tracked (remove it from the list)")
    if bad and "--fix" in sys.argv:
        for path, _, want in bad:
            convert(path, want)
        print(f"{len(bad)} files converted")
        return 0
    print("RESULT:", "FAIL" if bad else "PASS")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
