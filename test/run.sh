#!/bin/sh
# Host tests for q2-challengerdeep (x86-64 Linux, clang, python3 with Pillow), with the player's own .ipg
# in $PS_IPG: the real game and platform layer on fbshim.so's fake /dev/fb0 and alsa_fake.c's PCM,
# driven through Q2 Pod's input socket (test/drive.py). Each scenario must end on TERM with status
# 0, the panel back on Q2 Pod's page and the socket gone. Frames land in test/out/*.raw and .png.
set -e
cd "$(dirname "$0")/.."
[ -f "$PS_IPG" ] || { echo "set PS_IPG to your .ipg"; exit 1; }
mkdir -p test/out
./test/hostbuild.sh
for sc in menus sail fight pause; do
    python3 -I test/drive.py build/host test/out $sc > test/out/$sc.log 2>&1 || { cat test/out/$sc.log; exit 1; }
    grep -q "^exit status: 0" test/out/$sc.log || { echo "FAIL: $sc: exit status"; cat test/out/$sc.log; exit 1; }
    grep -q "^panel: yoffset 0 " test/out/$sc.log || { echo "FAIL: $sc: panel not handed back"; exit 1; }
    grep -q "^socket left: False" test/out/$sc.log || { echo "FAIL: $sc: socket left behind"; exit 1; }
    echo "$sc: ok"
done
res() { grep "^result: $2:" test/out/$1.log | sed 's/.*: //'; }
end() { grep "^end: $2:" test/out/$1.log | sed 's/.*: //'; }
# half a turn of the wheel anticlockwise: about 135 degrees to port (-3.93 rad wrapped)
t=$(res sail turn)
{ [ "$t" -gt 2200 ] && [ "$t" -lt 2500 ]; } || { [ "$t" -gt -4100 ] && [ "$t" -lt -3800 ]; } || { echo "FAIL: sail: turned $t mrad"; exit 1; }
[ "$(res sail moved)" -gt 100 ] || { echo "FAIL: sail: didn't move"; exit 1; }
[ "$(res sail stopped_speed)" -lt 5 ] || { echo "FAIL: sail: didn't stop with the sails down"; exit 1; }
[ "$(res fight shots_seen)" -gt 5 ] || { echo "FAIL: fight: no cannon fire"; exit 1; }
[ "$(end menus state)" = 3 ] || { echo "FAIL: menus: Set Sail didn't start a game"; exit 1; }
[ "$(res pause paused)" = 4 ] && [ "$(res pause resumed)" = 3 ] && [ "$(res pause over)" = 5 ] && [ "$(res pause title)" = 0 ] ||
    { echo "FAIL: pause: pause, resume, strike colours, back to the title"; exit 1; }
[ "$(res pause held_return)" = 4 ] && [ "$(res pause held_return_resumed)" = 3 ] ||
    { echo "FAIL: pause: Return held (repeating) didn't open the pause once"; exit 1; }
for f in test/out/*.raw; do python3 -I test/view.py "$f" "${f%.raw}.png"; done
echo "controls: ok (steered, sailed and stopped, fired, paused, a held Return paused once, struck colours, back to port)"
