#!/usr/bin/env python3
"""Write the offline smoke fixtures to OUT_DIR/*.txt (deterministic: fixed seed).
tests/smoke/run.sh calls this into build/smoke/fixtures/ (not committed: ~1.5 MB).

Each fixture is an A3 pipe-mode input (MODE START DEPTH SIZE / P / url k links...).
The TUI reads it through WC_FIXTURE=<file>; the header is ignored there (the smoke
runner types start/depth/size at the prompts), but it keeps every fixture valid for
`build/wc < fixture` too.

    python3 tests/smoke/gen_fixtures.py OUT_DIR
"""
import os
import random
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "build/smoke/fixtures"


def write(name, start, web, depth=3, size=50):
    path = os.path.join(OUT, name + ".txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"BFS {start} {depth} {size}\n{len(web)}\n")
        for url, links in web.items():
            if links is None:
                f.write(f"{url} -1\n")
            else:
                f.write(" ".join([url, str(len(links))] + links) + "\n")


def main():
    os.makedirs(OUT, exist_ok=True)
    rng = random.Random(26)

    # 1 node, no links.
    write("one", "https://solo.example/", {"https://solo.example/": []})

    # Chain of 11 pages: depth 0..10.
    chain = [f"https://chain.example/p{i}" for i in range(11)]
    write("chain10", chain[0], {u: ([chain[i + 1]] if i + 1 < len(chain) else []) for i, u in enumerate(chain)})

    # Star: root -> 100 leaves.
    leaves = [f"https://star.example/leaf/{i}" for i in range(100)]
    web = {"https://star.example/": leaves}
    web.update({u: [] for u in leaves})
    write("star100", "https://star.example/", web)

    # Dense clique-ish: 100 pages, each links to 30 random others (+ back to root).
    dense = [f"https://dense.example/n{i}" for i in range(100)]
    web = {u: rng.sample(dense, 30) + [dense[0]] for u in dense}
    write("dense100", dense[0], web)

    # Wikipedia-like: 600 pages, 60 links each, heavy cross-linking, a few failures.
    wiki = [f"https://wiki.example/wiki/Article_{i}" for i in range(600)]
    web = {}
    for i, u in enumerate(wiki):
        web[u] = None if i % 37 == 5 else rng.sample(wiki, 60)
    write("wikilike600", wiki[0], web)

    # Broad tree for size 1000: branching 10, depth 3 (1111 pages).
    tree = {}
    def grow(url, d):
        kids = [] if d == 3 else [f"{url.rstrip('/')}/{k}" for k in range(10)]
        tree[url] = kids
        for k in kids:
            grow(k, d + 1)
    grow("https://tree.example/", 0)
    write("tree1111", "https://tree.example/", tree)

    # Long / unicode / percent-encoded labels.
    root = "https://unicode.example/"
    odd = [
        "https://例え.テスト/パス/ページ",
        "https://xn--r8jz45g.xn--zckzah/%E3%83%91%E3%82%B9",
        "https://emoji.example/🕷️/web/🕸️",
        "https://ελληνικά.example/κατάλογος",
        "https://unicode.example/" + "very-long-segment-" * 20 + "end",
        "https://unicode.example/search?q=" + "%20".join(["word"] * 40),
        "https://" + "sub." * 15 + "deep.example/",
        "https://unicode.example/ünïcödé/ñ/ß/ø",
        "https://rtl.example/مرحبا/שלום",
        "https://cjk.example/中文/汉字/长标签" * 3,
    ]
    web = {root: odd}
    web.update({u: [root] + odd[:3] for u in odd})
    write("unicode_long", root, web)

    # Many sites (> 6 shapes in the legend): 12 hosts, 5 pages each, cross-site links.
    hosts = [f"https://site{h}.example" for h in range(12)]
    pages = [f"{h}/p{p}" for h in hosts for p in range(5)]
    root = "https://hub.example/"
    web = {root: [f"{h}/p0" for h in hosts]}
    for u in pages:
        web[u] = rng.sample(pages, 6)
    write("many_sites", root, web)

    # Failures: start links to failed (-1) and unlisted pages.
    root = "https://broken.example/"
    web = {root: [f"https://broken.example/dead{i}" for i in range(10)] + [f"https://missing.example/{i}" for i in range(10)]}
    web.update({f"https://broken.example/dead{i}": None for i in range(10)})
    write("failures", root, web)


if __name__ == "__main__":
    main()
