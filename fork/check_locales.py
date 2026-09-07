#!/usr/bin/env python3
"""Validate SeeGram's complete locale set and interpolation tokens."""
import json
import re
from pathlib import Path
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
source = (root / 'Telegram/SourceFiles/fork/fork_lang.cpp').read_text()
keys_block = source.split('constexpr auto Keys = std::array{', 1)[1].split('};', 1)[0]
keys = re.findall(r'"(\w+)"', keys_block)
locales_block = source.split('constexpr Locale Locales[] = {', 1)[1].split('};', 1)[0]
locales = re.findall(r'\{ "([a-z]+)",', locales_block)
folder = root / 'Telegram/Resources/fork/langs'
assert set(p.stem for p in folder.glob('*.json')) == set(locales)
qrc = root / 'Telegram/Resources/qrc/telegram/seegram_langs.qrc'
assert {p.attrib['alias'] for p in ET.parse(qrc).findall('.//file')} == {f'{code}.json' for code in locales}
english = json.loads((folder / 'en.json').read_text())
tokens = lambda s: sorted(re.findall(r'%\d+|\{\w+\}', s))
for code in locales:
    strings = json.loads((folder / f'{code}.json').read_text())
    assert set(strings) == set(keys), f'{code}: missing or extra keys'
    for key, value in strings.items():
        assert isinstance(value, str) and value.strip(), f'{code}/{key}: empty text'
        assert tokens(value) == tokens(english[key]), f'{code}/{key}: changed interpolation tokens'
print(f'{len(locales)} languages, {len(keys)} strings each: completeness and placeholders verified')
