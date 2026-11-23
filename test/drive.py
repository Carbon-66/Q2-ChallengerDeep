#!/usr/bin/env python3
"""Runs the host build of q2-challengerdeep as Q2 Pod would (fake /dev/fb0 from fbshim.so, input as text
datagrams on /tmp/q2-challengerdeep.input.sock, TERM to end it) through one scenario, and prints what it
logged and its last state. Frames are saved as OUT_DIR/<scenario>-<name>.raw (view.py shows them).

The .ipg comes from $PS_IPG (the player's own copy; it is never part of the repository).

Usage: drive.py BUILD_DIR OUT_DIR SCENARIO
Scenarios: menus, sail, fight, pause, sink, seas"""
import os, shutil, signal, socket, subprocess, sys, tempfile, time

build, outdir, scenario = sys.argv[1:4]
build, outdir = os.path.abspath(build), os.path.abspath(outdir)
ipg = os.environ.get('PS_IPG')
if not ipg or not os.path.exists(ipg):
    sys.exit('set PS_IPG to the .ipg')
SOCK = '/tmp/q2-challengerdeep.input.sock'
card = tempfile.mkdtemp()
app = os.path.join(card, '.challengerdeep')
os.mkdir(app)
shutil.copy(os.path.join(build, 'q2-challengerdeep'), app)
os.symlink(os.path.abspath(ipg), os.path.join(app, 'game.ipg'))
dump = os.path.join(card, 'fb.raw')
env = dict(os.environ, LD_PRELOAD=os.path.join(build, 'fbshim.so'), FB_DUMP=dump,
           FB_FINAL=os.path.join(card, 'fb.final'), PS_STATE='1')
if scenario == 'loading':
    env['PS_HOLDLOAD'] = '1'
if scenario in ('sail', 'fight', 'pause', 'sink'):
    env['PS_AUTOPLAY'] = os.environ.get('PS_HULL', '7')
    env['PS_SEA'] = os.environ.get('PS_SEA', '0')
if os.path.exists(SOCK):
    os.unlink(SOCK)
proc = subprocess.Popen([os.path.join(app, 'q2-challengerdeep'), 'plughw:0,0'], env=env, cwd=app)
log = os.path.join(app, 'challengerdeep.log')

def logged(text):
    return os.path.exists(log) and text in open(log).read()

def wait_for(cond, secs, what):
    end = time.time() + secs
    while time.time() < end:
        if cond():
            return
        time.sleep(0.05)
    proc.kill()
    sys.exit(f'FAIL: no {what} after {secs} s\n' + (open(log).read() if os.path.exists(log) else ''))

def state():
    p = os.path.join(app, 'state.txt')
    out = {}
    try:
        for line in open(p):
            k, v = line.rsplit(' ', 1)
            out[k] = int(v)
    except (OSError, ValueError):
        pass
    return out

wait_for(lambda: os.path.exists(SOCK), 5, 'input socket')
if scenario == 'loading':  # the picture shown while the banks load
    time.sleep(0.8)
    data = open(dump, 'rb').read()
    open(os.path.join(outdir, 'loading-start.raw'), 'wb').write(data)
wait_for(lambda: os.path.exists(os.path.join(app, 'state.txt')), 20, 'first frame')
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)

def send(m):
    s.sendto(m.encode(), SOCK)

def key(code, hold=0.06):
    send(f'k d {code}'); time.sleep(hold); send(f'k u {code}'); time.sleep(0.15)

def wheel(total, steps=10, gap=0.03):
    for _ in range(steps):
        send(f'w {total // steps}'); time.sleep(gap)

def tap(x, y):
    send(f't d {x} {y}'); time.sleep(0.05); send(f't u {x} {y}'); time.sleep(0.15)

def snap(name):
    time.sleep(0.12)
    for _ in range(50):  # fbshim rewrites the file each frame: take a whole one
        data = open(dump, 'rb').read()
        if len(data) == 320 * 375 * 4:
            break
        time.sleep(0.01)
    open(os.path.join(outdir, f'{scenario}-{name}.raw'), 'wb').write(data)

CENTRE, PLAY, PREV, NEXT, RETURN = 218, 171, 222, 223, 170
results = {}
start = state()
if scenario == 'menus':
    time.sleep(0.5); snap('title')
    wheel(-40, 2); snap('title-ship')
    key(CENTRE); time.sleep(0.3); snap('ships')
    wheel(-80, 4); time.sleep(0.4); snap('ships-2')
    key(CENTRE); wheel(-40, 2); key(CENTRE); time.sleep(0.3); snap('seas')
    wheel(-80, 4); time.sleep(0.3); snap('seas-2')
    key(CENTRE); wheel(120, 6); time.sleep(0.2); snap('title-back')
    key(CENTRE); time.sleep(2.5); snap('sea')
elif scenario == 'seas':
    for i in range(5):
        time.sleep(0.3); snap(f'sea{i}')
elif scenario == 'sail':
    time.sleep(1.0); snap('start')
    a0 = state()
    key(NEXT); time.sleep(2.0)
    wheel(100, 10); time.sleep(4.5); snap('turned')  # a slowed ship turns slower: time to finish
    results['turn'] = state()['ang'] - a0['ang']
    results['moved'] = abs(state()['x'] - a0['x']) + abs(state()['y'] - a0['y'])
    key(PREV); key(PREV); key(PREV); time.sleep(3.0)
    results['stopped_speed'] = state()['speed']
elif scenario == 'fight':
    time.sleep(1.0)
    shots = 0
    for i in range(70):
        st = state()
        key(CENTRE, 0.04)
        if i % 3 == 0:
            wheel(30 if (i // 9) % 2 else -30, 3)
        time.sleep(0.25)
        if i in (8, 30, 60):
            snap(f'battle{i}')
        if st.get('balls', 0):
            shots += 1
    results['shots_seen'] = shots
elif scenario == 'pause':
    time.sleep(1.0)
    # Return held: Q2 Pod passes its repeats on as more downs, which must open the pause once
    send(f'k d {RETURN}')
    for _ in range(7):  # 8 downs: toggling on each would end where it began
        time.sleep(0.07); send(f'k d {RETURN}')
    time.sleep(0.2)
    results['held_return'] = state()['state']
    send(f'k u {RETURN}'); time.sleep(0.2)
    key(PLAY); time.sleep(0.3)
    results['held_return_resumed'] = state()['state']
    key(PLAY); time.sleep(0.3); snap('paused')
    results['paused'] = state()['state']
    key(PLAY); time.sleep(0.3)
    results['resumed'] = state()['state']
    key(RETURN); time.sleep(0.2); wheel(-40, 2); key(CENTRE); time.sleep(4.0); snap('over')
    results['over'] = state()['state']
    key(CENTRE); time.sleep(0.4); snap('title')
    results['title'] = state()['state']
elif scenario == 'sink':
    # sit still and let the waves come
    time.sleep(1.0)
    key(PREV); key(PREV)
    for i in range(120):
        time.sleep(0.5)
        if state().get('state') == 5:
            break
        if i == 30:
            snap('beset')
    snap('end')
    results['end_state'] = state()['state']

end = state()
proc.send_signal(signal.SIGTERM)
try:
    status = proc.wait(5)
except subprocess.TimeoutExpired:
    proc.kill()
    status = 'killed'
print(open(log).read())
print('exit status:', status)
print('panel:', open(os.path.join(card, 'fb.final')).read().strip() if os.path.exists(os.path.join(card, 'fb.final')) else '-')
print('socket left:', os.path.exists(SOCK))
for k, v in end.items():
    print(f'end: {k}: {v}')
for k, v in results.items():
    print(f'result: {k}: {v}')
shutil.rmtree(card)
