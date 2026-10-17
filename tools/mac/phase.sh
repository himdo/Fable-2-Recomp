#!/usr/bin/env bash
# phase.sh <gamelog>: 3D entry = first pacer FPS <20 after line 70; mean FPS and ms/frame in [+3,+40] and [+28,+40]
grep 'guest vblank pacer' "$1" | sed -E 's/.*guest frame \(gate\) ~([0-9.]+) Hz.*/\1/' | awk '
{f[NR]=$1} END{for(i=71;i<=NR;i++) if(f[i]<20){e=i;break}
 printf "entry=%d n=%d ", e, NR
 split("3 40 28 40",w," ")
 for(k=1;k<=3;k+=2){s=0;c=0;for(i=e+w[k];i<=e+w[k+1]&&i<=NR;i++){s+=f[i];c++}
  printf "[+%d,+%d] fps=%.2f ms=%.1f (%d s) ", w[k], w[k+1], s/c, 1000*c/s, c}
 print ""}'
