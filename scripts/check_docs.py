"""Check local file links in readme.md, AGENTS.md and docs/**/*.md.

Uses only Python 3.9's standard library. Anchors are deliberately not checked.
Internet/scheme URLs, paths outside the repository and ignored work/build targets
are skipped; the checker never reads those targets. --check-indexes additionally
requires every docs Markdown page (including archives) to be reachable through
document links from docs/README.md and every data file to have a collection link.
This is a file/link checker, not a complete
Markdown renderer; it supports ordinary inline, image and reference-style links.
Existing directory links are valid, but are not document graph edges.
"""
import argparse
import json
import os
from pathlib import Path
import re
from urllib.parse import unquote


ROOT = Path(__file__).resolve().parents[1]
IGNORED = {'.git', '.vs', '.codex', '.agents', '__pycache__', 'work', 'build', 'obj', 'bin', '.tmp', 'artifacts'}
FENCE = re.compile(r'^ {0,3}(`{3,}|~{3,})(.*)$')
DESTINATION = r'(?:<(?P<angle>[^>\n]+)>|(?P<bare>(?:\\.|[^\s()\\]|\([^()\n]*\))+))'
INLINE = re.compile(r'(?P<image>!?)\[[^\]\n]*\]\(\s*' + DESTINATION +
                    r'(?:\s+(?:"[^"\n]*"|\'[^\'\n]*\'|\([^()\n]*\)))?\s*\)')
DEFINITION = re.compile(r'^ {0,3}\[(?P<id>[^\]\n]+)\]:\s*' + DESTINATION,
                        re.MULTILINE)
REFERENCE = re.compile(r'(?P<image>!?)\[(?P<label>[^\]\n]+)\]'
                       r'(?:\[(?P<id>[^\]\n]*)\])?(?!\()')
SCHEME = re.compile(r'^[a-zA-Z][a-zA-Z0-9+.-]*:')


def _blank(match):
    return ''.join('\n' if char == '\n' else ' ' for char in match.group())


def prose(text):
    """Mask code and comments without changing positions or line numbers."""
    lines = text.splitlines(keepends=True)
    fence = None
    for index, line in enumerate(lines):
        match = FENCE.match(line)
        if fence is not None:
            marker, length = fence
            if (match and match[1][0] == marker and len(match[1]) >= length
                    and not match[2].strip()):
                fence = None
            lines[index] = ''.join('\n' if char == '\n' else ' ' for char in line)
        elif match:
            fence = (match[1][0], len(match[1]))
            lines[index] = ''.join('\n' if char == '\n' else ' ' for char in line)
    text = ''.join(lines)
    text = re.sub(r'<!--.*?-->', _blank, text, flags=re.DOTALL)
    return re.sub(r'(`+)(?!`)(.*?)\1(?!`)', _blank, text, flags=re.DOTALL)


def _reference_id(value):
    return ' '.join(value.casefold().split())


def links(text):
    """Yield (line, destination, image) for links outside code/comments."""
    text = prose(text)
    definitions = {}
    for match in DEFINITION.finditer(text):
        definitions.setdefault(_reference_id(match['id']),
                               match['angle'] or match['bare'])
    for match in INLINE.finditer(text):
        yield text.count('\n', 0, match.start()) + 1, match['angle'] or match['bare'], bool(match['image'])
    # Mask definitions and inline links before finding reference/shortcut links.
    references = DEFINITION.sub(_blank, INLINE.sub(_blank, text))
    for match in REFERENCE.finditer(references):
        reference = _reference_id(match['id'] or match['label'])
        if reference in definitions:
            yield references.count('\n', 0, match.start()) + 1, definitions[reference], bool(match['image'])


def documents(root):
    """Enumerate only the explicitly supported documentation source scope."""
    result = [root / name for name in ('readme.md', 'AGENTS.md')
              if (root / name).is_file() and not (root / name).is_symlink()]
    if (root / 'docs').is_symlink():
        return sorted(result)
    for directory, children, files in os.walk(root / 'docs', followlinks=False):
        children[:] = sorted(name for name in children
                             if name.casefold() not in IGNORED
                             and not (Path(directory) / name).is_symlink())
        result.extend(Path(directory) / name for name in files
                      if name.lower().endswith('.md')
                      and not (Path(directory) / name).is_symlink())
    return sorted(result)


def local_target(root, source, destination):
    """Resolve only in-repository paths; return None for excluded targets."""
    destination = destination.strip()
    if destination.startswith(('//', '\\\\')) or SCHEME.match(destination):
        return None
    # Strip query/fragment before decoding, so encoded # and ? remain filenames.
    path = unquote(destination.split('#', 1)[0].split('?', 1)[0])
    path = re.sub(r'\\([\\`*{}\[\]()#+.!_<> -])', r'\1', path).replace('\\', '/')
    if not path:
        return None
    if SCHEME.match(path) or path.startswith('//'):
        return None
    target = ((root / path.lstrip('/')) if path.startswith('/')
              else (source.parent / path)).resolve()
    if not target.is_relative_to(root):
        return None
    relative = target.relative_to(root)
    if any(part.casefold() in IGNORED for part in relative.parts):
        return None
    return target


def check(root, check_indexes=False):
    root = Path(root).resolve()
    pages = documents(root)
    page_set = set(pages)
    graph = {page: set() for page in pages}
    catalog_targets = set()
    broken = []
    legacy_encoding_docs = []
    read_errors = []
    checked = skipped = 0
    for page in pages:
        relative = page.relative_to(root).as_posix()
        try:
            raw = page.read_bytes()
            try:
                text = raw.decode('utf-8-sig')
            except UnicodeDecodeError:
                text = raw.decode('cp1252')
                legacy_encoding_docs.append(relative)
        except (OSError, UnicodeError) as error:
            read_errors.append({'source': relative, 'error': str(error)})
            continue
        for line, destination, image in links(text):
            target = local_target(root, page, destination)
            if target is None:
                skipped += 1
                continue
            checked += 1
            if not (target.is_file() or target.is_dir()):
                broken.append({'source': page.relative_to(root).as_posix(),
                               'line': line, 'destination': destination,
                               'target': target.relative_to(root).as_posix()})
            elif not image and target in page_set:
                graph[page].add(target)
            if page.name == 'README.md' and target.parent == page.parent:
                catalog_targets.add(target)
    docs_pages = {page for page in pages if page.is_relative_to(root / 'docs')}
    index = root / 'docs/README.md'
    missing_index = check_indexes and index not in docs_pages
    reached = set()
    if check_indexes and not missing_index:
        pending = [index]
        while pending:
            page = pending.pop()
            if page in reached:
                continue
            reached.add(page)
            pending.extend(graph[page] - reached)
    unreachable = sorted(page.relative_to(root).as_posix()
                         for page in docs_pages - reached) if check_indexes else []
    unindexed_data = []
    if check_indexes:
        for directory, children, files in os.walk(root / 'docs', followlinks=False):
            children[:] = sorted(name for name in children
                                 if name.casefold() not in IGNORED
                                 and not (Path(directory) / name).is_symlink())
            for name in files:
                path = Path(directory) / name
                if path.suffix.lower() != '.md' and not path.is_symlink() and path not in catalog_targets:
                    unindexed_data.append(path.relative_to(root).as_posix())
    return {'scope': ['readme.md', 'AGENTS.md', 'docs/**/*.md (including archives)'],
            'anchors_checked': False, 'indexes_checked': check_indexes,
            'document_count': len(pages), 'checked_links': checked,
            'skipped_links': skipped, 'broken_links': sorted(broken, key=lambda item: (item['source'], item['line'])),
            'legacy_encoding_docs': legacy_encoding_docs, 'read_errors': read_errors,
            'missing_index': missing_index, 'unreachable_docs': unreachable,
            'unindexed_data': sorted(unindexed_data)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT, help='repository root (default: script parent)')
    parser.add_argument('--check-indexes', action='store_true',
                        help='require all docs Markdown, including archives, reachable from docs/README.md')
    parser.add_argument('--out', type=Path, help='optional JSON report under repository .tmp/ only')
    args = parser.parse_args(argv)
    root = args.root.resolve()
    output = None
    if args.out is not None:
        output = (root / args.out).resolve()
        if not output.is_relative_to(root / '.tmp'):
            parser.error('--out must be inside the repository ignored .tmp/ directory')
    report = check(root, args.check_indexes)
    for issue in report['read_errors']:
        print(f"{issue['source']}: cannot read Markdown: {issue['error']}")
    for issue in report['broken_links']:
        print(f"{issue['source']}:{issue['line']}: missing target {issue['destination']} (resolved {issue['target']})")
    if report['missing_index']:
        print('docs/README.md: missing documentation library index')
    for page in report['unreachable_docs']:
        print(f'{page}: unreachable from docs/README.md')
    for path in report['unindexed_data']:
        print(f'{path}: not linked from its collection README.md')
    print(f"Checked {report['document_count']} Markdown files, {report['checked_links']} local links; "
          f"{len(report['broken_links'])} broken links, {len(report['unreachable_docs'])} unreachable docs, "
          f"{len(report['unindexed_data'])} unindexed data files.")
    if report['legacy_encoding_docs']:
        print(f"Read {len(report['legacy_encoding_docs'])} legacy Markdown files as Windows CP1252.")
    if output is not None:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return int(bool(report['broken_links'] or report['missing_index']
                    or report['unreachable_docs'] or report['unindexed_data'] or report['read_errors']))


if __name__ == '__main__':
    raise SystemExit(main())
