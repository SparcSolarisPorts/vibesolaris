import os
os.chdir('/Users/macbook/git/vibesolaris')
needles=['vs_chat(','vs_build_system_prompt','history_count','VS_MAX_HISTORY','system_prompt']
for f in sorted(os.listdir('src')):
    if not f.endswith('.c'): continue
    for n,l in enumerate(open('src/'+f,errors='ignore'),1):
        for k in needles:
            if k in l:
                print('%s:%d: %s'%(f,n,l.rstrip()[:140]))
                break
