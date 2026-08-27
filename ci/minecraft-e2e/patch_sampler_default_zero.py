#!/usr/bin/env python3
from pathlib import Path
p = Path('Mithril-Wrapper-cpp/MG_Impl/Program.cpp')
s = p.read_text()
old1 = 'p->samplerUnitMap[(GLuint)db.binding] = -1;'
old2 = 'p->samplerUnitForBinding[(GLuint)db.binding] = -1;'
assert s.count(old1) == 1, s.count(old1)
assert s.count(old2) == 1, s.count(old2)
s = s.replace(old1, 'p->samplerUnitMap[(GLuint)db.binding] = 0;', 1)
s = s.replace(old2, 'p->samplerUnitForBinding[(GLuint)db.binding] = 0;', 1)
p.write_text(s)
