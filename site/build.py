#!/usr/bin/env python3
"""
The web page for https://psp2dc.pspexp.dev, built into site/out/:
  python3 site/build.py
- index.html: the page, from site/index.html, written by hand. Its {{REPO}} becomes the
  repository's address, and {{style.css}} and {{icon.svg}} the names those files are published
  under (below);
- style.css and icon.svg, each under a name that carries its content's hash, so a changed file
  has a new address and no cache answers with the old one;
- images/: the pictures the page names, from docs/images;
- _headers: which addresses tell search engines to stay away.
Python's standard library only. Cloudflare Pages: build command "python3 site/build.py", build
output directory "site/out".
The build fails when the page names a picture that is not in docs/images, or links to a part of
itself (#name) that is not there.
"""
import hashlib, os, re, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE = os.path.join(ROOT, "site")
OUT = os.path.join(SITE, "out")
REPO = "https://github.com/pleft/PSP2DC"
LAUNCHED = False   # False: every address says "do not index" (the repository the page links to is
                   # still private). True: the custom domain is indexed; the pages.dev addresses
                   # never are, so search results show one address


def fingerprint(name):
    stem, ext = os.path.splitext(name)
    digest = hashlib.sha256(open(os.path.join(SITE, name), "rb").read()).hexdigest()[:10]
    return "%s.%s%s" % (stem, digest, ext)


def main():
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(os.path.join(OUT, "images"))
    page = open(os.path.join(SITE, "index.html"), encoding="utf-8").read()
    for name in ("style.css", "icon.svg"):
        shutil.copy(os.path.join(SITE, name), os.path.join(OUT, fingerprint(name)))
        page = page.replace("{{%s}}" % name, fingerprint(name))
    page = page.replace("{{REPO}}", REPO)

    wrong = ["unfilled %s" % m for m in sorted(set(re.findall(r"\{\{[^}]*\}\}", page)))]
    for image in sorted(set(re.findall(r'src="images/([^"]+)"', page))):
        source = os.path.join(ROOT, "docs/images", image)
        if os.path.exists(source):
            shutil.copy(source, os.path.join(OUT, "images", image))
        else:
            wrong.append("no docs/images/%s" % image)
    ids = set(re.findall(r'\bid="([^"]+)"', page))
    wrong += ["no part named #%s" % a for a in sorted(set(re.findall(r'href="#([^"]+)"', page)) - ids)]
    if wrong:
        sys.exit("site/index.html: " + "; ".join(wrong))

    open(os.path.join(OUT, "index.html"), "w", encoding="utf-8").write(page)
    away = "  X-Robots-Tag: noindex, nofollow\n"
    hosts = ["https://:project.pages.dev/*", "https://:version.:project.pages.dev/*"] if LAUNCHED else ["/*"]
    open(os.path.join(OUT, "_headers"), "w").write("".join("%s\n%s" % (h, away) for h in hosts))
    print("site/out: %d files, %s" % (sum(len(f) for _, _, f in os.walk(OUT)),
                                      "indexed on its own domain" if LAUNCHED else "not indexed anywhere"))


if __name__ == "__main__":
    main()
