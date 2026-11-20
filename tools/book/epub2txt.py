#!/usr/bin/env python3
"""EPUB -> plain text for Nihil (one paragraph per line, UTF-8).

usage: epub2txt.py book.epub book.txt
Stop-gap until Nihil can open EPUB files itself. Standard library only.
"""
import html, posixpath, re, sys, zipfile
from html.parser import HTMLParser
from xml.etree import ElementTree as ET

BLOCK = {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "li", "br", "tr", "blockquote", "section", "title", "pre", "hr"}
SKIP = {"script", "style", "head"}

class Text(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.out, self.skip = [], 0
    def handle_starttag(self, tag, attrs):
        if tag in SKIP: self.skip += 1
        if tag in BLOCK: self.out.append("\n")
    def handle_endtag(self, tag):
        if tag in SKIP and self.skip: self.skip -= 1
        if tag in BLOCK: self.out.append("\n")
    def handle_data(self, data):
        if not self.skip: self.out.append(data.replace("\n", " "))

def main(src, dst):
    z = zipfile.ZipFile(src)
    container = ET.fromstring(z.read("META-INF/container.xml"))
    opf_path = next(e.attrib["full-path"] for e in container.iter() if e.tag.endswith("rootfile"))
    opf = ET.fromstring(z.read(opf_path))
    items = {e.attrib["id"]: e.attrib["href"] for e in opf.iter() if e.tag.endswith("}item")}
    spine = [e.attrib["idref"] for e in opf.iter() if e.tag.endswith("}itemref")]
    lines = []
    for idref in spine:
        href = posixpath.normpath(posixpath.join(posixpath.dirname(opf_path), items[idref].split("#")[0]))
        from urllib.parse import unquote
        p = Text()
        p.feed(z.read(unquote(href)).decode("utf-8", "replace"))
        for line in "".join(p.out).split("\n"):
            line = re.sub(r"\s+", " ", line).strip()
            if line:
                lines.append(line)
    text = "\n".join(lines) + "\n"
    open(dst, "w", encoding="utf-8").write(text)
    print("%d paragraphs, %d bytes" % (len(lines), len(text.encode("utf-8"))))

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
