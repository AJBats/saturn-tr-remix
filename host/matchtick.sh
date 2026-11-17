#!/bin/sh
# The host ticks (1 to MAX) after which every item slot matches a dump of
# host/satitems.py: how many ticks the Saturn had run. Prints each tick whose
# slots all match, then the closest.
#   host/matchtick.sh SAT_ITEMS.txt LEVEL [MAX]     (in WSL)
cd "$(dirname "$0")/.."
sat=$1 level=$2 max=${3:-200}
grep '^item' "$sat" | tr -d '\r' > /tmp/matchtick_sat.txt
best=999 bestt=0
t=1
while [ $t -le $max ]; do
    EVERY=1000000 qemu-ppc build/host/logic build/data "$level" $t --items-end 2>/dev/null | grep '^item' > /tmp/matchtick_host.txt
    n=$(diff /tmp/matchtick_host.txt /tmp/matchtick_sat.txt | grep -c '^>')
    if [ "$n" -eq 0 ]; then
        echo "after $t ticks: every slot matches"
    fi
    if [ "$n" -lt "$best" ]; then
        best=$n bestt=$t
    fi
    t=$((t + 1))
done
echo "closest: after $bestt ticks, $best slots differ"
