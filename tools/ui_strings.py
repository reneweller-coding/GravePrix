"""Lists the interface's German texts in the code (string literals that read like words) and keeps
data/lang/en.txt in step with them: every text already translated keeps its line, new texts are added as
"German = " lines at the end for the translator, texts no longer in the code are reported.

    python tools/ui_strings.py           (writes work/ui_literals.txt and reports what en.txt lacks)
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = ['src/App.cpp', 'src/AppRace.cpp', 'src/AppCareer.cpp', 'src/Input.cpp', 'src/Career.cpp']
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
WORDY = re.compile(r'[A-Za-zÄÖÜäöüß]{3,}')
SKIP = re.compile(r'^(#|%|\.|/|[a-z_0-9]+$|[a-z_0-9]+\.[a-z]+$)')


def literals():
    found = []
    for fn in FILES:
        text = open(os.path.join(ROOT, fn), encoding='utf-8').read()
        for line in text.split('\n'):
            s = line.strip()
            if s.startswith('//') or s.startswith('*') or s.startswith('#include'):
                continue
            for m in LIT.finditer(line):
                t = m.group(1)
                if WORDY.search(t) and not SKIP.match(t) and t not in found:
                    found.append(t)
    return found


def main():
    lits = literals()
    with open(os.path.join(ROOT, 'work', 'ui_literals.txt'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(lits) + '\n')
    en = {}
    path = os.path.join(ROOT, 'data', 'lang', 'en.txt')
    if os.path.exists(path):
        for line in open(path, encoding='utf-8'):
            if ' = ' in line and not line.startswith('#'):
                de, e = line.rstrip('\n').split(' = ', 1)
                en[de.strip()] = e.strip()
    missing = [t for t in lits if t not in en or not en[t]]
    print('%d literals, %d without English' % (len(lits), len(missing)))
    for t in missing:
        print('  ' + t)


if __name__ == '__main__':
    main()
