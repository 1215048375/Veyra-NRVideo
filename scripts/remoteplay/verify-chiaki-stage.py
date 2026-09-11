"""Verify the complete staged dependency against the pin plus reviewed patches."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

PIN = "0e16950165f06e5c3291537c2eeba6e852be7120"
PATCHES = ("0001-chiaki-msvc-vla-compat.patch", "0002-chiaki-video-metadata.patch")

def git(tree, *args, env=None):
    return subprocess.check_output(["git", "-C", str(tree), *args], env=env, stderr=subprocess.STDOUT).decode().strip()

def verify(stage, clean):
    for tree in (clean, stage):
        if git(tree, "rev-parse", "HEAD") != PIN:
            raise RuntimeError("Chiaki HEAD mismatch")
        if git(tree, "diff", "--cached", "--name-only"):
            raise RuntimeError("Chiaki index must be unchanged")
        modules = git(tree, "submodule", "status", "--recursive")
        # Do not strip each line: its leading marker is part of the contract.
        raw = subprocess.check_output(["git", "-C", str(tree), "submodule", "status", "--recursive"]).decode()
        if any(line and line[0] != " " for line in raw.splitlines()):
            raise RuntimeError("Chiaki submodule absent or pin mismatch")
        git(tree, "submodule", "foreach", "--quiet", "--recursive", "git diff --exit-code && git diff --cached --exit-code && test -z \"$(git ls-files --others --exclude-standard)\"")
        if git(tree, "ls-files", "--others", "--exclude-standard"):
            raise RuntimeError("Unexpected untracked dependency source files")
    if git(clean, "status", "--porcelain", "--untracked-files=no"):
        raise RuntimeError("Verification checkout must be clean")
    with tempfile.TemporaryDirectory(prefix="veyra-chiaki-index-") as folder:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(folder) / "index"))
        git(stage, "read-tree", PIN, env=env)
        for patch in PATCHES:
            git(stage, "apply", "--cached", str(Path(__file__).resolve().parent / "patches" / patch), env=env)
        expected = git(stage, "write-tree", env=env)
        # Compare the actual worktree contents against the reconstructed tree,
        # not just filenames or whether reversing a patch would succeed.
        git(stage, "diff", "--exit-code", "--ignore-submodules=none", expected)
    print("CHIAKI_STAGE_EXACT_PATCHES_VERIFIED")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--clean", type=Path, required=True)
    args = parser.parse_args()
    verify(args.stage.resolve(), args.clean.resolve())
