#!/usr/bin/env python3
"""
The web page for https://psp2dc.pspexp.dev, built into site/out/:
  python3 site/build.py
- index.html and download.html: the pages, from site/, written by hand. Their {{REPO}} becomes
  the repository's address, and {{style.css}} and {{icon.svg}} the names those files are
  published under (below);
- download.html's version, date, file, size, count and SHA-256: the latest release's, asked of
  GitHub at the build (and again by the page's own script, so they stay true between builds);
- style.css and icon.svg, each under a name that carries its content's hash, so a changed file
  has a new address and no cache answers with the old one;
- images/: the pictures the page names, from docs/images;
- _headers: which addresses tell search engines to stay away.
Python's standard library only. Cloudflare Pages: build command "python3 site/build.py", build
output directory "site/out".
The build fails when a page names a picture that is not in docs/images, or links to a page or
to a part of one (#name) that is not there.
"""
import datetime, hashlib, html, json, os, re, shutil, sys, urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE = os.path.join(ROOT, "site")
OUT = os.path.join(SITE, "out")
REPO = "https://github.com/pleft/PSP2DC"
PAGES = ["index.html", "download.html"]
ZIP = r"^PSP2DC-.*\.zip$"                       # a release's one file
LAUNCHED = True     # False: every address says "do not index" (the repository the page links to is
                   # still private). True: the custom domain is indexed; the pages.dev addresses
                   # never are, so search results show one address


def fingerprint(name):
    stem, ext = os.path.splitext(name)
    digest = hashlib.sha256(open(os.path.join(SITE, name), "rb").read()).hexdigest()[:10]
    return "%s.%s%s" % (stem, digest, ext)


def release():
    """What the download page says of the latest release: from GitHub when its API answers, and
    otherwise the version and date of CHANGELOG.md's newest release with the zip's own address,
    which needs no API (the page's script then fills in the size, the count and the SHA-256)."""
    v = {"VERSION": "", "DATE": "", "TOTAL": "–", "TOTAL_UNIT": "downloads", "RELEASE_PAGE": REPO + "/releases",
         "ZIP_URL": REPO + "/releases/latest", "ZIP_SIZE": "", "ZIP_NAME": "", "ZIP_SHA": "in the .sha256 file beside the zip"}
    day = lambda d: "%d %s" % (d.day, d.strftime("%B %Y"))
    rels = []
    try:
        hdr = {"Accept": "application/json", "User-Agent": "psp2dc-site"}
        if os.environ.get("GITHUB_TOKEN"):      # (build machines share addresses, and GitHub's 60 an hour with them)
            hdr["Authorization"] = "Bearer " + os.environ["GITHUB_TOKEN"]
        api = REPO.replace("github.com", "api.github.com/repos") + "/releases?per_page=100"
        got = json.load(urllib.request.urlopen(urllib.request.Request(api, headers=hdr), timeout=20))
        rels = [r for r in got if not r["draft"] and not r["prerelease"]]
        print("download page: GitHub listed %d published releases" % len(rels))
    except Exception as e:
        print("download page: no release information from GitHub (%s)" % e)
    if rels:
        latest = rels[0]
        total = sum(a["download_count"] for r in rels for a in r["assets"] if re.match(ZIP, a["name"]))
        v.update({"VERSION": latest["tag_name"].lstrip("v"), "RELEASE_PAGE": latest["html_url"],
                  "DATE": day(datetime.date.fromisoformat(latest["published_at"][:10])),
                  "TOTAL": "{:,}".format(total), "TOTAL_UNIT": "download" if total == 1 else "downloads"})
        for a in latest["assets"]:
            if re.match(ZIP, a["name"]):
                v.update({"ZIP_URL": a["browser_download_url"], "ZIP_NAME": a["name"],
                          "ZIP_SIZE": "%.1f MB" % (a["size"] / 1e6)})
                if a.get("digest"):
                    v["ZIP_SHA"] = a["digest"].replace("sha256:", "")
    else:
        m = re.search(r"^## (\d+\.\d+\.\d+) \((\d{4}-\d\d-\d\d)\)",
                      open(os.path.join(ROOT, "CHANGELOG.md"), encoding="utf-8").read(), re.M)
        print("download page: the version from CHANGELOG.md: %s" % (m.group(1) if m else "none"))
        if m:
            ver = m.group(1)
            v.update({"VERSION": ver, "DATE": day(datetime.date.fromisoformat(m.group(2))),
                      "RELEASE_PAGE": "%s/releases/tag/v%s" % (REPO, ver), "ZIP_NAME": "PSP2DC-%s.zip" % ver,
                      "ZIP_URL": "%s/releases/download/v%s/PSP2DC-%s.zip" % (REPO, ver, ver)})
    return v


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(os.path.join(OUT, "images"))
    pages = {name: open(os.path.join(SITE, name), encoding="utf-8").read() for name in PAGES}
    for name in ("style.css", "icon.svg"):
        shutil.copy(os.path.join(SITE, name), os.path.join(OUT, fingerprint(name)))
    for k, x in release().items():
        pages["download.html"] = pages["download.html"].replace("{{%s}}" % k, html.escape(x))

    wrong = []
    ids = {name: set(re.findall(r'\bid="([^"]+)"', page)) for name, page in pages.items()}
    for name, page in pages.items():
        for asset in ("style.css", "icon.svg"):
            page = page.replace("{{%s}}" % asset, fingerprint(asset))
        page = pages[name] = page.replace("{{REPO}}", REPO)
        wrong += ["%s: unfilled %s" % (name, m) for m in sorted(set(re.findall(r"\{\{[^}]*\}\}", page)))]
        for image in sorted(set(re.findall(r'src="images/([^"]+)"', page))):
            source = os.path.join(ROOT, "docs/images", image)
            if os.path.exists(source):
                shutil.copy(source, os.path.join(OUT, "images", image))
            else:
                wrong.append("%s: no docs/images/%s" % (name, image))
        # links to parts of this page (#name), of the front page (./#name) and to the other pages
        wrong += ["%s: no part named #%s" % (name, a) for a in sorted(set(re.findall(r'href="#([^"]+)"', page)) - ids[name])]
        wrong += ["%s: the front page has no part named #%s" % (name, a)
                  for a in sorted(set(re.findall(r'href="\./#([^"]+)"', page)) - ids["index.html"])]
        wrong += ["%s: no page %s" % (name, a) for a in sorted(set(re.findall(r'href="([a-z-]+\.html)"', page)) - set(pages))]
    if wrong:
        sys.exit("site: " + "; ".join(wrong))

    for name, page in pages.items():
        open(os.path.join(OUT, name), "w", encoding="utf-8").write(page)
    away = "  X-Robots-Tag: noindex, nofollow\n"
    hosts = ["https://:project.pages.dev/*", "https://:version.:project.pages.dev/*"] if LAUNCHED else ["/*"]
    open(os.path.join(OUT, "_headers"), "w").write("".join("%s\n%s" % (h, away) for h in hosts))
    print("site/out: %d files, %s" % (sum(len(f) for _, _, f in os.walk(OUT)),
                                      "indexed on its own domain" if LAUNCHED else "not indexed anywhere"))


if __name__ == "__main__":
    main()
