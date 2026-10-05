#!/usr/bin/env python3
"""engine-gpu-states.py - the GPU bridge across savestates, through Chimera's engine.

usage: engine-gpu-states.py <libchimera.so> <srb2.chimeraCore> <data folder> [settings JSON]

The renderer opengl-hw draws on the machine's GPU through Chimera's bridge, and
the GL there is not the machine's: a state brings back object names the driver
no longer means. The core rebuilds when the context moves (ogl_chimera.c's
chimera_gl_step). This checks that it does, three ways, on Greenflower:

  straight   300 steps, every step's picture
  rewind     150 steps, a state saved, 100 more, the state loaded, then on to
             300: steps 151-300 must be the straight run's pictures
  reopen     the state loaded into a second session in the same process
             (the bridge's context outlives a session, as a project closed and
             opened again): steps 151-300 again

The pictures are the GPU's, so they are compared run to run on one machine
(the driver draws the same picture for the same calls), never to a
reference. Each load must also have made the renderer again (the core says
so on standard error, which this reads back through a pipe): a rewind inside
one level can draw right by luck, its stale names still naming textures of
the same level. Prints one line per check and exits non-zero on a
difference."""
import ctypes
import hashlib
import tempfile
import json
import os
import sys
import time
import zipfile

lib_path, package, data = sys.argv[1], sys.argv[2], sys.argv[3]
overrides = sys.argv[4] if len(sys.argv) > 4 else '{"renderer": "opengl-hw", "warp": "1", "resolution": "640x400"}'

ce = ctypes.CDLL(lib_path)
ce.ce_gl_request.argtypes = [ctypes.c_int32]
ce.ce_gl_request(1)
ce.ce_session_open.restype = ctypes.c_void_p
ce.ce_session_open.argtypes = [
    ctypes.c_char_p, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_char_p, ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint64), ctypes.c_int32,
    ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint64),
    ctypes.POINTER(ctypes.c_char_p), ctypes.c_int32, ctypes.POINTER(ctypes.c_char_p)]
ce.ce_session_frame_advance.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_int32]
ce.ce_session_frame_advance.restype = ctypes.c_int32
ce.ce_session_video.argtypes = [ctypes.c_void_p]
ce.ce_session_video.restype = ctypes.POINTER(ctypes.c_uint32)
for f in ("ce_session_video_width", "ce_session_video_height"):
    getattr(ce, f).argtypes = [ctypes.c_void_p]
    getattr(ce, f).restype = ctypes.c_int32
ce.ce_session_save_state.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint64)]
ce.ce_session_save_state.restype = ctypes.POINTER(ctypes.c_uint8)
ce.ce_session_load_state.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint64]
ce.ce_session_load_state.restype = ctypes.c_int32
ce.ce_session_deterministic.argtypes = [ctypes.c_void_p]
ce.ce_session_deterministic.restype = ctypes.c_int32
ce.ce_session_free.argtypes = [ctypes.c_void_p]

with zipfile.ZipFile(package) as z:
    cfg = json.loads(z.read("waterbox.config"))
fw_ids, fw_blobs = [], []
for fw in cfg["firmware"]:
    with open(os.path.join(data, fw["name"]), "rb") as f:
        fw_ids.append(fw["id"].encode())
        fw_blobs.append(f.read())
n = len(fw_ids)
ids = (ctypes.c_char_p * n)(*fw_ids)
bufs = [ctypes.create_string_buffer(b, len(b)) for b in fw_blobs]
datas = (ctypes.c_void_p * n)(*[ctypes.cast(b, ctypes.c_void_p) for b in bufs])
lens = (ctypes.c_uint64 * n)(*[len(b) for b in fw_blobs])
forward = 1 << cfg["input"]["buttons"].index("Forward")


def open_session():
    err = ctypes.c_char_p()
    s = ce.ce_session_open(package.encode(), None, 0, None, overrides.encode(),
                           ids, datas, lens, n, None, None, None, None, 0, ctypes.byref(err))
    if not s:
        sys.exit("open failed: " + (err.value.decode() if err.value else "(no message)"))
    return s


def step(s, i):
    held = forward if 80 <= i <= 260 else 0
    if ce.ce_session_frame_advance(s, held, 1) < 0:
        sys.exit(f"step {i}: the core stopped")
    w, h = ce.ce_session_video_width(s), ce.ce_session_video_height(s)
    px = ctypes.string_at(ce.ce_session_video(s), w * h * 4)
    lit = sum(1 for i in range(0, len(px), 4 * 97) if px[i] | px[i + 1] | px[i + 2])
    return hashlib.sha1(px).hexdigest()[:16], lit * 97 * 100 // (w * h)


def save(s):
    ln = ctypes.c_uint64()
    p = ce.ce_session_save_state(s, ctypes.byref(ln))
    return ctypes.string_at(p, ln.value)


def load(s, blob):
    buf = (ctypes.c_uint8 * len(blob)).from_buffer_copy(blob)
    if ce.ce_session_load_state(s, buf, len(blob)) != 0:  # 0 is success
        sys.exit("the state did not load")


# the core's standard error, to count its rebuilds
err_file = tempfile.TemporaryFile()
saved_err = os.dup(2)
os.dup2(err_file.fileno(), 2)


def rebuilds():
    err_file.flush()
    err_file.seek(0)
    return err_file.read().decode(errors="replace").count("the renderer is made again")


ok = True
s = open_session()
print(f"deterministic: {ce.ce_session_deterministic(s)} (a GPU drew)")
t = time.time()
straight = {}
for i in range(1, 301):
    straight[i] = step(s, i)
dt = time.time() - t
lit = straight[300][1]
print(f"straight: 300 steps in {dt:.1f} s ({300 / dt:.0f} steps/s), {lit}% of the last picture lit")
if lit < 50:
    ok = False
    print("FAIL straight: the picture is dark")
ce.ce_session_free(s)

s = open_session()
for i in range(1, 151):
    step(s, i)
state = save(s)
for i in range(151, 251):
    step(s, i)
before = rebuilds()
load(s, state)
diff = [i for i in range(151, 301) if step(s, i) != straight[i]]
made = rebuilds() - before
if made < 1:
    diff = diff or ["no rebuild"]
print(("FAIL" if diff else "PASS") + f" rewind ({made} rebuild): steps 151-300 after a load {'differ at ' + str(diff[:5]) if diff else 'are the straight run'}")
ok &= not diff
ce.ce_session_free(s)

s = open_session()
step(s, 1)
before = rebuilds()
load(s, state)
diff = [i for i in range(151, 301) if step(s, i) != straight[i]]
made = rebuilds() - before
if made < 1:
    diff = diff or ["no rebuild"]
print(("FAIL" if diff else "PASS") + f" reopen ({made} rebuild): the state in a second session, steps 151-300 {'differ at ' + str(diff[:5]) if diff else 'are the straight run'}")
ok &= not diff
ce.ce_session_free(s)
os.dup2(saved_err, 2)
sys.exit(0 if ok else 1)
