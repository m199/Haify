#!/usr/bin/env python3
"""Check source/include/build/test paths without invoking a compiler (Python 3)."""
import re
import shlex
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"\n]+)"', re.M)


def text(path):
    return path.read_text(encoding='utf-8')


def make_value(source, name):
    joined = re.sub(r'\\\r?\n', ' ', source)
    match = re.search(r'^' + re.escape(name) + r'\s*=\s*(.*)$', joined, re.M)
    assert match, 'Missing Makefile variable: ' + name
    return shlex.split(match[1])


def resolve_include(source, name, directories):
    for directory in [source.parent] + directories:
        candidate = (directory / name).resolve()
        if candidate.is_file() and candidate.is_relative_to(ROOT):
            return candidate
    raise AssertionError(f'Unresolved include: {source.relative_to(ROOT)} -> {name}')


def check_includes(sources, directories):
    seen, pending = set(), list(sources)
    while pending:
        source = pending.pop()
        if source in seen:
            continue
        seen.add(source)
        pending.extend(resolve_include(source, name, directories)
                       for name in INCLUDE.findall(text(source)))
    return len(seen)


def source_files():
    return [p for p in ROOT.rglob('*') if p.suffix in ('.cpp', '.h')
            and not any(part.startswith('.') or part.startswith('objects')
                        or part == 'graphify-out' for part in p.relative_to(ROOT).parts)]


def check_runner():
    runner = text(ROOT / 'tests/run-discover-tests.sh')
    variables = dict(re.findall(r'^(\w+)="([^"\n]*)"$', runner, re.M))
    programs, compiled = re.findall(r'^"\$test_output/([^"\n]+)"$', runner, re.M), []
    for line in runner.splitlines():
        if not line.startswith('"$cxx" '):
            continue
        # Source-list variables expand to shell words in the existing runner.
        line = re.sub(r'\$(\w+)', lambda m: variables.get(m[1], m[0]), line)
        args = shlex.split(line)
        sources = [(ROOT / arg).resolve() for arg in args if arg.endswith('.cpp')]
        assert sources and all(path.is_file() for path in sources), line
        directories = [(ROOT / arg[2:]).resolve() for arg in args if arg.startswith('-I')]
        check_includes(sources, directories)
        compiled.append(Path(args[args.index('-o') + 1]).name)
    assert len(programs) == len(set(programs)), 'Duplicate test invocation'
    assert compiled == programs, 'Test compile/run order differs'
    return len(programs)


def main():
    assert not [p for p in ROOT.iterdir() if p.suffix in ('.h', '.cpp', '.rdef')], 'Source left in root'
    makefile = text(ROOT / 'Makefile')
    sources = [(ROOT / path).resolve() for path in make_value(makefile, 'SRCS')]
    assert len(sources) == len(set(sources)), 'Duplicate Makefile source'
    assert all(path.is_file() for path in sources), 'Missing Makefile source'
    all_code = source_files()
    production = {p.resolve() for p in all_code if p.suffix == '.cpp'
                  and p.relative_to(ROOT).parts[0] != 'tests'}
    assert set(sources) == production, 'Makefile must list every production cpp exactly once'
    for name in make_value(makefile, 'RDEFS'):
        assert (ROOT / name).is_file(), 'Missing resource: ' + name
    directories = [(ROOT / p).resolve() for p in make_value(makefile, 'LOCAL_INCLUDE_PATHS')]
    # Check unused headers too, not only the current build dependency closure.
    count = check_includes([p.resolve() for p in all_code], directories)
    programs = check_runner()
    print(f'Layout OK: {len(sources)} app sources, {count} C++/header files, {programs} test programs; no build.')


if __name__ == '__main__':
    main()
