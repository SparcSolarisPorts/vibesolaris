import sys,os
os.chdir('/Users/macbook/git/vibesolaris')
src=sys.argv[1]; out=sys.argv[2]
nl=chr(10)
lines=open(src,errors='ignore').read().split(nl)
with open(out,'w') as g:
    for i,l in enumerate(lines,1):
        g.write('%4d| %s'%(i,l) + nl)
print('wrote',out,len(lines))
