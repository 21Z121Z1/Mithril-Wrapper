#!/usr/bin/env python3
from pathlib import Path

wrapper = Path("scripts/apply_directmetal_uniform_snapshots_phase6_v4.py")
source = wrapper.read_text()

# v4 executes the base patcher after rewriting two ambiguous carrier anchors.
# Add one more rewrite to the base-patcher source before that execution: the
# TranslateStage block is nested in @autoreleasepool and therefore has 8/12
# spaces, not the 4/8 spaces assumed by the first draft.
needle = '''exec(compile(script, str(base), "exec"),
     {"__name__":"__main__", "__file__":str(base), "Path":Path})
'''
if source.count(needle) != 1:
    raise SystemExit("v4 execution anchor drifted")
insert = r'''
old_translate = ''' + "'''" + r'''exact("src/metal/engine.mm",
''' + "'''" + r'''    if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
        !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment))
        return 0;
''' + "'''" + r''',
''' + "'''" + r'''    if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
        !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment) ||
        !ResolveUniformMemberSlots(&program.vertex, uniform_names) ||
        !ResolveUniformMemberSlots(&program.fragment, uniform_names))
        return 0;
''' + "'''" + r''')''' + "'''" + r'''
new_translate = ''' + "'''" + r'''exact("src/metal/engine.mm",
''' + "'''" + r'''        if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
            !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment))
            return 0;
''' + "'''" + r''',
''' + "'''" + r'''        if (!TranslateStage(vs, spv::ExecutionModelVertex, &program.vertex) ||
            !TranslateStage(fs, spv::ExecutionModelFragment, &program.fragment) ||
            !ResolveUniformMemberSlots(&program.vertex, uniform_names) ||
            !ResolveUniformMemberSlots(&program.fragment, uniform_names))
            return 0;
''' + "'''" + r''')''' + "'''" + r'''
if script.count(old_translate) != 1:
    raise SystemExit("TranslateStage patch anchor drifted")
script = script.replace(old_translate, new_translate, 1)

'''
source = source.replace(needle, insert + needle, 1)
exec(compile(source, str(wrapper), "exec"),
     {"__name__":"__main__", "__file__":str(wrapper), "Path":Path})
