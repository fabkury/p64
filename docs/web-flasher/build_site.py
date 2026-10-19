#!/usr/bin/env python3
"""Assemble the GitHub Pages site that hosts the p64 web flasher.

    python docs/web-flasher/build_site.py --out _site                      # CI (pages.yml)
    python docs/web-flasher/build_site.py --out _site --local v0.2.0=firmware/build_public/release

Writes <out>/web-flasher/ with the page (this folder), the logo, and under firmware/
one folder per GitHub release (flash_args and the files it lists, downloaded with the
gh CLI) plus firmware/index.json, the list the page reads: tag, title, pre-release
flag, publication date, notes and the files with their offsets and sizes. The page
never calls the GitHub API (no rate limit), and release assets never enter git: the
site is rebuilt from the releases on every deployment.

--local adds a release from a folder tools/release_assets.py wrote (for a preview
without a published release); it can be given more than once and needs no gh.
"""

import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LOGO = os.path.join(ROOT, "docs", "images", "logo", "p64-logo-64.png")
PAGE_FILES = ["index.html", "esptool-bundle.js"]
REPO = "fabkury/p64"

REDIRECT = """<!doctype html>
<meta charset="utf-8">
<meta http-equiv="refresh" content="0; url=web-flasher/">
<title>p64</title>
<a href="web-flasher/">p64 web flasher</a>
"""


def gh(*args):
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout


def parse_flash_args(text):
    files = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("--"):
            continue
        offset, name = line.split(None, 1)
        files.append((int(offset, 16), name.strip()))
    files.sort()
    return files


def entry(tag, title, prerelease, published_at, notes, folder):
    files = []
    for offset, name in parse_flash_args(open(os.path.join(folder, "flash_args"), encoding="utf-8").read()):
        path = os.path.join(folder, name)
        if not os.path.exists(path):
            sys.exit("%s: flash_args names %s, which is not there" % (tag, name))
        files.append({"address": offset, "name": name, "size": os.path.getsize(path)})
    return {
        "tag": tag,
        "title": title or "p64 " + tag,
        "prerelease": bool(prerelease),
        "published_at": published_at,
        "notes": notes or "",
        "files": files,
    }


def from_github(firmware_dir):
    releases = json.loads(gh("release", "list", "--repo", REPO, "--limit", "100", "--exclude-drafts", "--json",
                             "tagName,name,isPrerelease,publishedAt"))
    out = []
    for r in releases:
        tag = r["tagName"]
        folder = os.path.join(firmware_dir, tag)
        os.makedirs(folder, exist_ok=True)
        gh("release", "download", tag, "--repo", REPO, "--pattern", "flash_args", "--dir", folder, "--clobber")
        for _, name in parse_flash_args(open(os.path.join(folder, "flash_args"), encoding="utf-8").read()):
            gh("release", "download", tag, "--repo", REPO, "--pattern", name, "--dir", folder, "--clobber")
        notes = json.loads(gh("release", "view", tag, "--repo", REPO, "--json", "body"))["body"]
        out.append(entry(tag, r.get("name"), r["isPrerelease"], r["publishedAt"], notes, folder))
        print("release %s: %d files" % (tag, len(out[-1]["files"])))
    return out


def from_local(spec, firmware_dir):
    tag, _, src = spec.partition("=")
    if not tag or not src:
        sys.exit("--local wants TAG=FOLDER, got %r" % spec)
    folder = os.path.join(firmware_dir, tag)
    if os.path.isdir(folder):
        shutil.rmtree(folder)
    shutil.copytree(src, folder)
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    e = entry(tag, "p64 " + tag + " (local preview)", "-" in tag, now, "Built locally; not a published release.", folder)
    print("local %s: %d files" % (tag, len(e["files"])))
    return e


def version_key(tag):
    core = tag.lstrip("v").split("-")[0].split("+")[0]
    try:
        nums = tuple(int(x) for x in core.split("."))
    except ValueError:
        nums = ()
    return (nums, "-" not in tag)  # a release outranks its own pre-releases


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="site folder to write (replaced)")
    ap.add_argument("--local", action="append", default=[], metavar="TAG=FOLDER",
                    help="add a release from a local release folder instead of GitHub")
    ap.add_argument("--no-github", action="store_true", help="do not list GitHub releases (preview)")
    args = ap.parse_args()

    out = os.path.abspath(args.out)
    if os.path.isdir(out):
        shutil.rmtree(out)
    site = os.path.join(out, "web-flasher")
    firmware_dir = os.path.join(site, "firmware")
    os.makedirs(firmware_dir)
    for name in PAGE_FILES:
        shutil.copyfile(os.path.join(HERE, name), os.path.join(site, name))
    shutil.copyfile(LOGO, os.path.join(site, "p64-logo-64.png"))
    with open(os.path.join(out, "index.html"), "w", newline="\n", encoding="utf-8") as f:
        f.write(REDIRECT)
    with open(os.path.join(out, ".nojekyll"), "w") as f:
        pass

    releases = [] if args.no_github else from_github(firmware_dir)
    releases += [from_local(spec, firmware_dir) for spec in args.local]
    releases.sort(key=lambda e: version_key(e["tag"]), reverse=True)
    with open(os.path.join(firmware_dir, "index.json"), "w", newline="\n", encoding="utf-8") as f:
        json.dump({"generated_at": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
                   "releases": releases}, f, indent=1)
    print("site in %s: %d release(s)" % (out, len(releases)))


if __name__ == "__main__":
    main()
