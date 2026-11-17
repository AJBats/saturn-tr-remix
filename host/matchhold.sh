#!/bin/sh
# Which held ticks reproduce a Saturn dump made with HOLD (host/satitems.py):
# KEY held from tick A for L ticks, for A in A0..A1 and L in L0..L1; prints
# each (A, L, ticks) after which every item slot matches.
#   host/matchhold.sh SAT_ITEMS.txt LEVEL KEY A0 A1 L0 L1 MAXTICKS    (in WSL)
cd "$(dirname "$0")/.."
sat=$1 level=$2 key=$3 a0=$4 a1=$5 l0=$6 l1=$7 max=$8
grep '^item' "$sat" | tr -d '\r' > /tmp/matchhold_sat.txt
lara=$(grep '^item   0' /tmp/matchhold_sat.txt)
a=$a0
while [ $a -le $a1 ]; do
    l=$l0
    while [ $l -le $l1 ]; do
        last=$((a + l - 1))
        t=$((a + l))
        while [ $t -le $max ]; do
            EVERY=1000000 qemu-ppc build/host/logic build/data "$level" $t "$key@$a-$last" --items-end 2>/dev/null | grep '^item' > /tmp/matchhold_host.txt
            if [ "$(grep '^item   0' /tmp/matchhold_host.txt)" = "$lara" ]; then
                n=$(diff /tmp/matchhold_host.txt /tmp/matchhold_sat.txt | grep -c '^>')
                echo "held from tick $a for $l, after $t ticks: Lara matches, $n slots differ"
            fi
            t=$((t + 1))
        done
        l=$((l + 1))
    done
    a=$((a + 1))
done
echo done
