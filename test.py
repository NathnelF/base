"""
Test runner for FASTbuild output at bin/{config}/{test}/{test}.exe.

Usage:
    python test.py                   run every test in every config
    python test.py all               same
    python test.py <config>          run every test in one config
    python test.py <test>            run one test across all configs (exact, else substring match)
    python test.py <config> <test>   run one test in one config

Finds the repo root by walking up from the cwd to the nearest fbuild.bff.
Tests run with the repo root as cwd. Exit code 0 means pass. Does not build.
"""

import os
import subprocess
import sys


def find_root():
    path = os.path.abspath(os.getcwd())
    while True:
        if os.path.isfile(os.path.join(path, "fbuild.bff")):
            return path
        parent = os.path.dirname(path)
        if parent == path:
            return None
        path = parent


def discover(root):
    """Returns a sorted list of (config, name, exe_path)."""
    found = []
    bin_dir = os.path.join(root, "bin")
    if not os.path.isdir(bin_dir):
        return found
    for config in sorted(os.listdir(bin_dir)):
        config_dir = os.path.join(bin_dir, config)
        if not os.path.isdir(config_dir):
            continue
        for name in sorted(os.listdir(config_dir)):
            exe = os.path.join(config_dir, name, name + ".exe")
            if os.path.isfile(exe):
                found.append((config, name, exe))
    return found


def match_name(tests, query):
    exact = [t for t in tests if t[1] == query]
    if exact:
        return exact
    return [t for t in tests if query in t[1]]


def main():
    args = sys.argv[1:]
    if len(args) > 2:
        print(__doc__)
        return 2

    root = find_root()
    if root is None:
        print("error: no fbuild.bff found in cwd or any parent")
        return 2

    tests = discover(root)
    configs = sorted({t[0] for t in tests})
    if not tests:
        print("error: no tests found under " + os.path.join(root, "bin"))
        return 2

    if len(args) == 0 or args == ["all"]:
        selected = tests
    elif len(args) == 1 and args[0] in configs:
        selected = [t for t in tests if t[0] == args[0]]
    elif len(args) == 1:
        selected = match_name(tests, args[0])
    else:
        if args[0] not in configs:
            print("error: unknown config '%s' (have: %s)" % (args[0], ", ".join(configs)))
            return 2
        selected = match_name([t for t in tests if t[0] == args[0]], args[1])

    if not selected:
        print("error: no tests match %s" % " ".join(args))
        print("configs: " + ", ".join(configs))
        print("tests:   " + ", ".join(sorted({t[1] for t in tests})))
        return 2

    results = []
    for config, name, exe in selected:
        label = "%s/%s" % (config, name)
        print("=== %s ===" % label, flush=True)
        code = subprocess.call([exe], cwd=root)
        results.append((label, code))
        print("--- %s: %s ---\n" % (label, "PASS" if code == 0 else "FAIL (exit %d)" % code), flush=True)

    failed = [r for r in results if r[1] != 0]
    print("%d/%d passed" % (len(results) - len(failed), len(results)))
    for label, code in failed:
        print("  FAIL %s (exit %d)" % (label, code))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
