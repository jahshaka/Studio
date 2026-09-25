# gi.gather_plane_weight (test_gi_gather_findings.cpp): the Monte Carlo its two-sided bar is
# predicted from — the same geometry, run by hand: python3 fence_occlusion_mc.py
# gi.gather_plane_weight's geometry: does the fence (1-3 cm proud of the wall, x <= 0) hide the
# red panel (x -4..-1, y 0.5..2.5, 0.55 m in front of the wall) from a wall point just right of
# the fence's edge? Cosine-weighted Monte Carlo of the panel's projected solid angle.
import math,random
random.seed(1)
def E(px,fence,N=400000):
    p=(px,1.5,-2.9+1e-4); acc=0
    for i in range(N):
        u=random.random();v=random.random(); r=math.sqrt(u);th=2*math.pi*v
        d=(r*math.cos(th),r*math.sin(th),math.sqrt(1-u))
        t=(-2.35-p[2])/d[2]; x=p[0]+t*d[0];y=p[1]+t*d[1]
        if not(-4<=x<=-1 and 0.5<=y<=2.5): continue
        if fence:
            t1=(-2.89-p[2])/d[2]; t2=(-2.87-p[2])/d[2]
            if min(p[0]+t1*d[0],p[0]+t2*d[0])<=0.0: continue
        acc+=1
    return acc/N
for x in [0.02,0.05,0.1,0.2,0.3]:
    b=E(x,False);f=E(x,True); print(x,"bare %.4f fence %.4f ratio %.3f"%(b,f,f/b))
# output 2026-09-24: 0.02 -> 0.000, 0.05 -> 0.000, 0.1 -> 0.742, 0.2 -> 0.959, 0.3 -> 0.994
