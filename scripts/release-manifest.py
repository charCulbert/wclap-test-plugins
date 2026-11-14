#!/usr/bin/env python3
"""Write manifest.json, SHA256SUMS and index.html for a directory of builds.

usage: release-manifest.py PLUGINS_JSON DIST_DIR [--sha SHA] [--tag TAG]

PLUGINS_JSON is the plugins.json a CMake configure writes. DIST_DIR holds the
archives, named <plug-in>.wclap.tar.gz or <plug-in>-<platform>.clap.zip.
Each plug-in's "proves" sentence is the first paragraph under the
"## Proves" heading of plugins/<name>/README.md.
"""

import argparse
import hashlib
import html
import json
import re
import sys
from pathlib import Path

FORMAT_VERSION = 1
ROOT = Path(__file__).resolve().parent.parent


def ids(name, first):
    """Every plug-in ID the bundle holds: its own, and any the README lists that extend it (`<id>.<suffix>`)."""
    readme = (ROOT / "plugins" / name / "README.md").read_text(encoding="utf-8")
    more = re.findall(r"`(" + re.escape(first) + r"\.[A-Za-z0-9._-]+)`", readme)
    return [first] + sorted(set(more))


def proves(name):
    readme = ROOT / "plugins" / name / "README.md"
    lines = readme.read_text(encoding="utf-8").splitlines()
    try:
        start = lines.index("## Proves") + 1
    except ValueError:
        sys.exit(f"{readme}: no '## Proves' heading")
    paragraph = []
    for line in lines[start:]:
        if line.startswith("#"):
            break
        if not line.strip():
            if paragraph:
                break
            continue
        paragraph.append(line.strip())
    if not paragraph:
        sys.exit(f"{readme}: '## Proves' is empty")
    return " ".join(paragraph)


def platform_of(name, file):
    if file == f"{name}.wclap.tar.gz":
        return "wclap"
    match = re.fullmatch(re.escape(name) + r"-(.+)\.clap\.zip", file)
    return match.group(1) if match else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("plugins_json", type=Path)
    parser.add_argument("dist", type=Path)
    parser.add_argument("--sha", default="")
    parser.add_argument("--tag", default="")
    args = parser.parse_args()

    listing = json.loads(args.plugins_json.read_text(encoding="utf-8"))
    files = sorted(p for p in args.dist.iterdir() if p.is_file()
                   and (p.name.endswith(".wclap.tar.gz") or p.name.endswith(".clap.zip")))
    digests = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in files}

    plugins = []
    claimed = set()
    for entry in listing["plugins"]:
        name = entry["name"]
        artifacts = []
        for p in files:
            platform = platform_of(name, p.name)
            if platform:
                claimed.add(p.name)
                artifacts.append({"file": p.name, "platform": platform,
                                  "sha256": digests[p.name], "size": p.stat().st_size})
        if not artifacts:
            sys.exit(f"no artifacts for {name}")
        plugins.append({"id": entry["id"], "ids": ids(name, entry["id"]), "name": name, "version": listing["version"],
                        "proves": proves(name), "artifacts": artifacts})
    stray = set(digests) - claimed
    if stray:
        sys.exit(f"artifacts with no plug-in: {', '.join(sorted(stray))}")

    manifest = {"format_version": FORMAT_VERSION, "git_sha": args.sha,
                "tag": args.tag or None, "plugins": plugins}
    (args.dist / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n",
                                             encoding="utf-8")
    (args.dist / "SHA256SUMS").write_text(
        "".join(f"{digests[n]}  {n}\n" for n in sorted(digests)), encoding="utf-8")

    rows = []
    for plugin in plugins:
        links = " ".join(f'<a href="{html.escape(a["file"])}">{html.escape(a["platform"])}</a>'
                         for a in plugin["artifacts"])
        text = re.sub(r"`([^`]+)`", r"<code>\1</code>", html.escape(plugin["proves"]))
        rows.append(f"<li><code>{html.escape(plugin['name'])}</code> {links}<br>{text}</li>")
    build = html.escape(args.tag or args.sha[:12] or "local build")
    (args.dist / "index.html").write_text(f"""<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WCLAP test plug-ins</title>
<style>body{{font:16px/1.5 system-ui,sans-serif;max-width:46rem;margin:2rem auto;padding:0 1rem}}li{{margin:.75rem 0}}</style>
<h1>WCLAP test plug-ins</h1>
<p>Build {build}. <a href="manifest.json">manifest.json</a> · <a href="SHA256SUMS">SHA256SUMS</a> ·
<a href="https://github.com/charCulbert/wclap-test-plugins">source</a></p>
<ul>
{chr(10).join(rows)}
</ul>
""", encoding="utf-8")


if __name__ == "__main__":
    main()
