#!/usr/bin/env python3
"""source.display_guard_toy - A GUARDED TEST BINARY REACHES NO DISPLAY THAT IS NOT A RIG (DEV_ERRORS 37 + 38, 2026-11-22;
tests/support/display_guard.cpp). CPU only, no real display: every run is a clean environment (no inherited DISPLAY, no
inherited Wayland session) whose displays are FAKES of this toy - a Wayland socket in a private XDG_RUNTIME_DIR and an X
server on a free ABSTRACT socket @/tmp/.X11-unix/X<N> (N outside the rig range 60-99, no file made) - that only RECORD a
connect and hold it open (a client blocks, never aborts: an abort would file an apport crash report on this box).
  (1) DISPLAY=:0 literal + the agent shell's Wayland session, --env-only (no Q*Application is ever made): DISPLAY, WAYLAND_*
      unset, QT_QPA_PLATFORM=offscreen, one stderr line naming :0;
  (2) DISPLAY=:0 + QT_QPA_PLATFORM=xcb (a run that demands X), --env-only: REFUSED, exit 2, main never ran;
  (3) THE AGENT SHELL's ENVIRONMENT (R2 of spikes/display-guard/repro): XDG_SESSION_TYPE=wayland, GNOME, WAYLAND_DISPLAY,
      DISPLAY a non-rig X: a QGuiApplication runs on `offscreen`, ZERO connects to either fake;
  (4) R3: WAYLAND_DISPLAY and DISPLAY unset, the session still wayland (libwayland's default wayland-0): offscreen, ZERO;
  (5) THE NEGATIVE CONTROL: the unguarded probe in (3)'s environment CONNECTS (the fakes see it; it is then killed).
usage: display_guard_toy.py <display_guard_probe> <display_guard_probe_unguarded>"""
import os, select, signal, socket, subprocess, sys, tempfile, threading, time

GUARDED, CONTROL = sys.argv[1], sys.argv[2]
fails = 0


def want(ok, what):
    global fails
    print(("  ok   " if ok else "  FAIL ") + what, flush=True)
    fails += 0 if ok else 1


class Fakes:
    """the recording servers: Wayland at <xdg>/wayland-0, X at @/tmp/.X11-unix/X<n>; every connect counted and held open"""
    def __init__(self, xdg):
        self.count, self.held, self.lock = {"wayland": 0, "x11": 0}, [], threading.Lock()
        w = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        w.bind(os.path.join(xdg, "wayland-0"))
        w.listen(16)
        self.socks = {w: "wayland"}
        for n in range(100, 200):   # a free X number: no lock file, and the abstract name binds (else it is taken)
            if os.path.exists("/tmp/.X%d-lock" % n) or os.path.exists("/tmp/.X11-unix/X%d" % n):
                continue
            x = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                x.bind("\0/tmp/.X11-unix/X%d" % n)
            except OSError:
                x.close()
                continue
            x.listen(16)
            self.socks[x], self.display = "x11", ":%d" % n
            break
        else:
            raise SystemExit("display_guard_toy: no free X number in 100-199")
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self):
        while True:
            r, _, _ = select.select(list(self.socks), [], [])
            for s in r:
                c, _ = s.accept()
                with self.lock:
                    self.count[self.socks[s]] += 1
                    self.held.append(c)

    def total(self):
        with self.lock:
            return self.count["wayland"] + self.count["x11"]


def run(exe, env, args=(), kill_on_connect=None):
    """<exe> in a CLEAN environment; returns (exit code, stdout, stderr). kill_on_connect: the Fakes - the process is killed
    (SIGKILL: no core) once they see a NEW connect (a client held open blocks: a red case ends here, never at the cap), or
    after 20 s"""
    p = subprocess.Popen([exe, *args], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         start_new_session=True)
    if kill_on_connect is not None:
        n0 = kill_on_connect.total()
        for _ in range(400):
            if kill_on_connect.total() > n0 or p.poll() is not None:
                break
            time.sleep(0.05)
        if p.poll() is None:
            os.killpg(p.pid, signal.SIGKILL)
    try:
        out, err = p.communicate(timeout=30)
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)
        out, err = p.communicate()
    return p.returncode, out, err


with tempfile.TemporaryDirectory(prefix="display-guard-toy.") as t:
    xdg = os.path.join(t, "xdg")
    os.mkdir(xdg, 0o700)
    os.mkdir(os.path.join(t, "home"))
    fakes = Fakes(xdg)
    base = {"PATH": "/usr/bin:/bin", "HOME": os.path.join(t, "home"), "XDG_RUNTIME_DIR": xdg,
            "ASAN_OPTIONS": "detect_leaks=0"}
    session = dict(base, XDG_SESSION_TYPE="wayland", XDG_CURRENT_DESKTOP="ubuntu:GNOME", WAYLAND_DISPLAY="wayland-0")

    rc, out, err = run(GUARDED, dict(session, DISPLAY=":0"), ["--env-only"])
    want(rc == 0 and "DISPLAY=<unset>\n" in out and "WAYLAND_DISPLAY=<unset>" in out and "WAYLAND_SOCKET=<unset>" in out
         and "QT_QPA_PLATFORM=offscreen" in out and "DISPLAY=:0 is not a rig display" in err,
         "(1) DISPLAY=:0 + the Wayland session: all unset, offscreen, said - got exit %s %r %r"
         % (rc, out.split(), err.strip()[:120]))

    rc, out, err = run(GUARDED, dict(session, DISPLAY=":0", QT_QPA_PLATFORM="xcb"), ["--env-only"])
    want(rc == 2 and out == "" and "REFUSED (exit 2)" in err,
         "(2) DISPLAY=:0 + QT_QPA_PLATFORM=xcb: REFUSED before main - got exit %s, stdout %r" % (rc, out[:60]))

    n0 = fakes.total()
    rc, out, err = run(GUARDED, dict(session, DISPLAY=fakes.display), kill_on_connect=fakes)
    want(rc == 0 and "platform=offscreen" in out and fakes.total() == n0,
         "(3) the agent shell's environment (wayland session, WAYLAND_DISPLAY, DISPLAY=%s): offscreen, %d connects - got "
         "exit %s %r" % (fakes.display, fakes.total() - n0, rc, out.strip()))

    n0 = fakes.total()
    rc, out, err = run(GUARDED, dict(base, XDG_SESSION_TYPE="wayland", XDG_CURRENT_DESKTOP="ubuntu:GNOME"),
                       kill_on_connect=fakes)
    want(rc == 0 and "platform=offscreen" in out and fakes.total() == n0,
         "(4) WAYLAND_DISPLAY and DISPLAY unset, the session wayland (libwayland's wayland-0): offscreen, %d connects - "
         "got exit %s %r" % (fakes.total() - n0, rc, out.strip()))

    n0 = fakes.total()
    rc, out, err = run(CONTROL, dict(session, DISPLAY=fakes.display), kill_on_connect=fakes)
    want(fakes.total() > n0, "(5) negative control, the unguarded probe in (3)'s environment: %d connect(s) (wayland %d, "
         "x11 %d) - the class is real and the fakes see it" % (fakes.total() - n0, fakes.count["wayland"], fakes.count["x11"]))

print("source.display_guard_toy: PASSED" if not fails else "source.display_guard_toy: FAILED (%d)" % fails, flush=True)
sys.exit(1 if fails else 0)
