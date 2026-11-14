"""
Check that text from the MUD split into several packets is drawn as if
it came in one packet, also while the input line is edited between them.

Each scenario runs powwow in a tmux window, connected to a server here
that sends each bytes step as a packet; a list step is typed, a string
as is and a tuple as a tmux key name.  A reference run sends all the
bytes in one packet first, then types all the keys.  The screens, with
their scrollback, colors and cursor, must be the same.

usage: python3 -I test_split.py POWWOW [NAME...]
exit status: 0 if all passed, 1 if any failed, 77 if it cannot run
"""

import concurrent.futures
import itertools
import os
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time

ROWS = 24
SETTLE = 0.2                    # seconds without change: drawn
WORKERS = 4


def utf8_locale():
    try:
        names = subprocess.run(['locale', '-a'], capture_output=True,
                               text=True).stdout.split()
    except OSError:
        return None
    norm = {n.lower().replace('-', ''): n for n in names}
    for want in ('c.utf8', 'en_us.utf8'):
        if want in norm:
            return norm[want]
    return None


class Window:
    """powwow in a tmux window of its own server"""

    counter = itertools.count()
    lock = threading.Lock()

    def __init__(self, powwow, locale, cols):
        with Window.lock:
            n = next(Window.counter)
        self.tmux = ['tmux', '-L', 'powwow-test-%d-%d' % (os.getpid(), n),
                     '-f', '/dev/null']
        self.home = tempfile.TemporaryDirectory()
        self.env = {k: v for k, v in os.environ.items() if k != 'TMUX'}
        command = ('env -i HOME=%s TERM=screen LC_ALL=%s PATH=/usr/bin:/bin'
                   ' %s' % (self.home.name, locale, powwow))
        self.run('new-session', '-d', '-s', 't', '-x', str(cols),
                 '-y', str(ROWS), command)
        self.run('set', '-t', 't', 'history-limit', '10000')

    def run(self, *args):
        return subprocess.run(self.tmux + list(args), capture_output=True,
                              text=True, env=self.env).stdout

    def snapshot(self):
        text = self.run('capture-pane', '-p', '-e', '-t', 't',
                        '-S', '-', '-E', '-')
        cursor = self.run('display', '-p', '-t', 't',
                          '#{cursor_x},#{cursor_y}').strip()
        # the port differs from run to run
        text = re.sub(r'127\.0\.0\.1([ :])\d+', r'127.0.0.1\1PORT', text)
        lines = [line.rstrip() for line in text.split('\n')]
        while lines and lines[-1] == '':
            lines.pop()
        return lines, cursor

    def settle(self, timeout=5.0):
        last, since = None, time.time()
        end = time.time() + timeout
        while time.time() < end:
            now = self.snapshot()
            if now != last:
                last, since = now, time.time()
            elif time.time() - since > SETTLE:
                break
            time.sleep(0.05)
        return last

    def wait_for(self, text, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            if any(text in line for line in self.snapshot()[0]):
                return True
            time.sleep(0.05)
        return False

    def type(self, keys):
        for k in keys:
            if isinstance(k, tuple):
                self.run('send-keys', '-t', 't', k[0])
            else:
                self.run('send-keys', '-t', 't', '-l', k)
            self.settle()

    def close(self):
        self.run('kill-server')
        self.home.cleanup()


def run(powwow, locale, scenario, steps):
    srv = socket.socket()
    srv.bind(('127.0.0.1', 0))
    srv.listen(1)
    srv.settimeout(10)
    port = srv.getsockname()[1]
    win = Window(powwow, locale, scenario.get('cols', 80))
    try:
        if not win.wait_for('#help'):
            raise NotStarted()
        for line in scenario.get('setup', ()):
            win.type([line, ('Enter',)])
        win.type(['#connect m --no-tls 127.0.0.1 %d' % port, ('Enter',)])
        conn, _ = srv.accept()
        with conn:
            conn.sendall(b'Welcome.\r\n')
            win.wait_for('Welcome.')
            win.settle()
            for step in steps:
                if isinstance(step, bytes):
                    conn.sendall(step)
                    win.settle()
                else:
                    win.type(step)
            return win.settle()
    finally:
        win.close()
        srv.close()


def reference(steps):
    data = b''.join(s for s in steps if isinstance(s, bytes))
    keys = [k for s in steps if not isinstance(s, bytes) for k in s]
    return [data] + ([keys] if keys else [])


LEFT, BS, DEL, HOME, END = (('Left',), ('BSpace',), ('DC',), ('Home',),
                            ('End',))
IAC, GA, EOR, NOP = b'\xff', b'\xf9', b'\xef', b'\xf1'
DO, SB, SE = b'\xfd', b'\xfa', b'\xf0'
TTYPE, NAWS = b'\x18', b'\x1f'
EMOJI = '\N{GRINNING FACE}'.encode()            # 4 bytes
WIDE = '\N{CJK UNIFIED IDEOGRAPH-65E5}'.encode()  # 2 columns
ZWJ_SEQ = '\N{WOMAN}‍\N{PERSONAL COMPUTER}'.encode()
HEART_PROMPT = 'HP:\N{BLACK HEART SUIT}'.encode()
LONG = (b'This line is long enough to wrap at eighty columns, so the '
        b'text from the MUD that continues it after a packet ends must '
        b'not be cut.')
COMPACT = ['#option +compact']


def scenario(steps, **options):
    return dict(options, steps=steps)


SCENARIOS = {
    # lines split in the middle
    'line split, typing':
        scenario([b'Hello wo', ['abc'], b'rld\r\nNext line\r\n']),
    'line split, typing, compact':
        scenario([b'Hello wo', ['abc'], b'rld\r\nNext line\r\n'],
                 setup=COMPACT),
    'line in three parts, editing':
        scenario([b'The q', ['look'], b'uick br', [LEFT, LEFT, BS, 'x'],
                  b'own fox\r\n']),
    'cursor in the middle of the input line':
        scenario([b'One ', ['abcdef', LEFT, LEFT, LEFT], b'two ', [DEL],
                  b'three\r\n', ['X']]),
    'two lines split':
        scenario([b'first pa', ['ab'], b'rt\r\nsecond pa', ['c'],
                  b'rt\r\n']),
    'line split before LF':
        scenario([b'line one\r', ['c'], b'\nline two\r\n']),
    'line split before LF, compact':
        scenario([b'line one\r', ['c'], b'\nline two\r\n'],
                 setup=COMPACT),
    'line split before CR LF, compact, with a #prompt':
        scenario([b'line one', ['c'], b'\r\nline two\r\n'],
                 setup=COMPACT + ['#prompt >p ^HP:=#isprompt']),
    'prompt, then output while typing, compact':
        scenario([b'HP:> ', ['say hi'], b'\r\nA rat arrives.\r\nHP:> '],
                 setup=COMPACT),
    'byte by byte, typing':
        scenario([x for i, c in enumerate(b'Typing in\r\n')
                  for x in [bytes([c])] + ([['k']] if i % 3 == 0 else [])]),
    'byte by byte, compact':
        scenario([bytes([c]) for c in b'Byte by byte\r\n'], setup=COMPACT),
    'long line split':
        scenario([LONG[:70], ['abc'], LONG[70:100], [BS],
                  LONG[100:] + b'\r\n']),
    'long input line, editing':
        scenario([['z' * 100, HOME], b'par', ['q', END], b'tial\r\n']),
    # prompts, with and without IAC GA
    'prompt, then output while typing':
        scenario([b'HP:> ', ['say hi'], b'\r\nA rat arrives.\r\nHP:> ']),
    'prompt with GA, then output while typing':
        scenario([b'HP:> ' + IAC + GA, ['say hi'],
                  b'\r\nA rat arrives.\r\nHP:> ' + IAC + GA]),
    'prompt split':
        scenario([b'HP', ['ki'], b':> ', ['ll'], b'\r\nYou hit.\r\nHP:> ']),
    'GA split from IAC':
        scenario([b'HP:> ' + IAC, ['k'], GA, ['l'], b'\r\nYou see.\r\n']),
    'GA, then text in the same packet':
        scenario([b'HP:> ' + IAC + GA + b'You see a ro', ['x'],
                  b'ad.\r\nHP:> ' + IAC + GA]),
    'GA in the middle of a line':
        scenario([b'half a ', IAC + GA, ['q'], b'line\r\n']),
    'EOR split from IAC':
        scenario([b'HP:> ' + IAC, ['k'],
                  EOR + b'\r\nYou see.\r\nHP:> ' + IAC + EOR]),
    # other telnet commands
    'escaped IAC split':
        scenario([b'a' + IAC, ['x'], IAC + b'b\r\n']),
    'NOP split':
        scenario([b'text' + IAC, ['m'], NOP + b' more\r\n']),
    'DO NAWS split':
        scenario([b'start of ' + IAC + DO, ['z'], NAWS + b'a line\r\n']),
    'TTYPE suboption split':
        scenario([b'before ' + IAC + SB + TTYPE, ['a'], b'\x01' + IAC,
                  ['b'], SE + b'after\r\n']),
    # SGR
    'SGR split after ESC':
        scenario([b'plain \x1b', ['a'], b'[1mbold\x1b[0m\r\n']),
    'SGR split in its parameters':
        scenario([b'\x1b[38;5;2', ['a'], b'08morange\x1b[0m\r\n']),
    'RGB SGR split, colored text split':
        scenario([b'\x1b[38;2;10;2', ['a'], b'00;30mrgb te', ['b'],
                  b'xt\x1b[m\r\n']),
    'colored prompt with GA, split':
        scenario([b'\x1b[1;3', ['a'], b'1mHP:\x1b[0m> ' + IAC, ['b'],
                  GA + b'\r\nYou see.\r\n']),
    # UTF-8
    'emoji in four packets':
        scenario([b'x ' + EMOJI[:1], ['a'], EMOJI[1:2], ['b'], EMOJI[2:3],
                  ['c'], EMOJI[3:] + b' y\r\n']),
    'wide characters split':
        scenario([b'\xe6\x97\xa5\xe6', ['b'], b'\x9c\xac\xe8\xaa\x9e\r\n']),
    'combining accent split from its letter':
        scenario([b'cafe', ['a'],
                  '\N{COMBINING ACUTE ACCENT}'.encode() + b'!\r\n']),
    'emoji ZWJ sequence split':
        scenario([b'dev ' + ZWJ_SEQ[:7], ['a'], ZWJ_SEQ[7:] + b' ok\r\n']),
    'UTF-8 split in a prompt':
        scenario([HEART_PROMPT[:4], ['a'], HEART_PROMPT[4:] + b'> ', ['b'],
                  b'\r\nYou see.\r\n']),
    # text that reaches the last column, or wraps
    'line split at the last column':
        scenario([LONG[:80], LONG[80:] + b'\r\n']),
    'line split at the last column, typing':
        scenario([LONG[:80], ['abc'], LONG[80:] + b'\r\n']),
    'line split after the last column':
        scenario([LONG[:81], LONG[81:] + b'\r\n']),
    'prompt up to the last column, then output':
        scenario([LONG[:80], ['abc'], b'\r\nYou see.\r\n']),
    'prompt longer than a row, then output':
        scenario([LONG[:81], ['abc'], b'\r\nYou see.\r\n']),
    'prompt of two rows in a wider window, then output':
        scenario([(LONG * 2)[:200], ['abc'], b'\r\nYou see.\r\n'],
                 cols=100),
    'prompt with a wide character in the last column':
        scenario([b'x' * 78 + WIDE, ['abc'], b'\r\nYou see.\r\n']),
    'prompt with a wide character that does not fit':
        scenario([b'x' * 79 + WIDE, ['abc', LEFT, 'q'],
                  b'\r\nYou see.\r\n']),
    'wide character that does not fit, split inside it':
        scenario([b'x' * 79 + WIDE[:1], ['a'], WIDE[1:], ['bc'],
                  b'\r\nYou see.\r\n']),
}


class NotStarted(Exception):
    pass


def check(powwow, locale, name, sc):
    try:
        got = run(powwow, locale, sc, sc['steps'])
        want = run(powwow, locale, sc, reference(sc['steps']))
    except (NotStarted, OSError) as e:
        return 'FAIL %s: %s\n' % (name, e or 'powwow did not start'), False
    if got == want:
        return 'ok   %s\n' % name, True
    out = ['FAIL %s\n' % name,
           '  cursor: split %s, reference %s\n' % (got[1], want[1])]
    for tag, (lines, _) in (('split', got), ('reference', want)):
        out.append('  --- %s\n' % tag)
        out += ['  |%s\n' % line.replace('\x1b', '<ESC>') for line in lines]
    return ''.join(out), False


def main():
    powwow = os.path.abspath(sys.argv[1])
    only = sys.argv[2:]
    if not os.access(powwow, os.X_OK):
        print('FAIL: cannot run %s' % powwow)
        return 1
    locale = utf8_locale()
    if locale is None:
        print('SKIP: no UTF-8 locale')
        return 77
    names = [n for n in SCENARIOS if not only or n in only]
    with concurrent.futures.ThreadPoolExecutor(WORKERS) as pool:
        results = pool.map(lambda n: check(powwow, locale, n, SCENARIOS[n]),
                           names)
        failed = 0
        for text, ok in results:
            sys.stdout.write(text)
            failed += not ok
    print('%d of %d failed' % (failed, len(names)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
