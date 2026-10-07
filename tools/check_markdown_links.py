"""Check local file/heading links in every tracked Markdown document."""
import json
import re
import subprocess
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    data = path.read_bytes()
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("cp1252")


def prose(text):
    text = re.sub(r"(?ms)^\s*(`{3,}|~{3,}).*?^\s*\1\s*$", "", text)
    return re.sub(r"(`+).*?\1", "", text, flags=re.S)


def targets(text):
    text = prose(text)
    # Local docs use inline links and reference definitions, with optional titles.
    return re.findall(r"\]\(\s*(<[^>]+>|[^\s)]+)", text) + re.findall(
        r"(?m)^\s*\[[^]]+\]:\s*(<[^>]+>|\S+)", text)


def anchors(text):
    result = set(re.findall(r'(?:id|name)=["\']([^"\']+)', text))
    counts = {}
    for heading in re.findall(r"(?m)^#{1,6}\s+(.+?)\s*#*\s*$", text):
        slug = re.sub(r"[^\w\- ]", "", heading.lower()).replace(" ", "-")
        count = counts.get(slug, 0)
        counts[slug] = count + 1
        result.add(slug + (f"-{count}" if count else ""))
    return result


def check():
    files = subprocess.check_output(
        ["git", "ls-files", "--", "*.md"], cwd=ROOT, text=True).splitlines()
    broken, local, external = [], 0, 0
    for name in files:
        path = ROOT / name
        for target in targets(read(path)):
            target = target.strip("<>")
            url = urlsplit(target)
            if url.scheme or url.netloc:
                external += 1
                continue
            local += 1
            destination = (ROOT / unquote(url.path).lstrip("/") if url.path.startswith("/")
                           else path.parent / unquote(url.path)) if url.path else path
            reason = "missing file" if not destination.exists() else None
            if not reason and url.fragment and destination.suffix.lower() == ".md":
                if unquote(url.fragment) not in anchors(read(destination)):
                    reason = "missing heading/anchor"
            if reason:
                broken.append({"file": name, "target": target, "reason": reason})
    return {"documents": len(files), "local_links": local,
            "external_links_not_checked": external, "broken": broken}


if __name__ == "__main__":
    assert targets("`[x](bad)`\n```\n[x](bad)\n```\n[x](a.md#b)\n[r]: <c.md>") == ["a.md#b", "<c.md>"]
    assert anchors("# Hello, world!\n# Hello, world!\n<a id='custom'>") == {"hello-world", "hello-world-1", "custom"}
    report = check()
    print(json.dumps(report, indent=2))
    raise SystemExit(bool(report["broken"]))
