#!/usr/bin/env python3
"""engine-open.py - opens the package the way Chimera does, through its engine.

usage: engine-open.py <libchimera.so> <srb2.chimeraCore> <data folder> [steps] [settings JSON] [--ppm FILE]

Chimera's engine (libchimera, a bundle's dll/) opens the package with the four
pk3s as firmware and the settings as overrides - every check the frontend's
session makes: the required exports, the declaration, Init - then steps it,
pressing Select (Enter) every 50 steps (through the intro, the title, the menus, into a new game), and
reports each failure the engine names. With --ppm, the last picture."""
import ctypes
import hashlib
import json
import os
import sys
import zipfile

args = [a for a in sys.argv[1:]]
ppm = None
if "--ppm" in args:
    i = args.index("--ppm")
    ppm = args[i + 1]
    del args[i:i + 2]
lib_path, package, data = args[0], args[1], args[2]
steps = int(args[3]) if len(args) > 3 else 300
overrides = args[4] if len(args) > 4 else None

ce = ctypes.CDLL(lib_path)
ce.ce_session_open.restype = ctypes.c_void_p
ce.ce_session_open.argtypes = [
    ctypes.c_char_p, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_char_p, ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint64), ctypes.c_int32,
    ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint64),
    ctypes.POINTER(ctypes.c_char_p), ctypes.c_int32, ctypes.POINTER(ctypes.c_char_p)]
ce.ce_session_frame_advance.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_int32]
ce.ce_session_frame_advance.restype = ctypes.c_int32
ce.ce_session_last_error.argtypes = [ctypes.c_void_p]
ce.ce_session_last_error.restype = ctypes.c_char_p
ce.ce_session_video.argtypes = [ctypes.c_void_p]
ce.ce_session_video.restype = ctypes.POINTER(ctypes.c_uint32)
for f in ("ce_session_video_width", "ce_session_video_height"):
    getattr(ce, f).argtypes = [ctypes.c_void_p]
    getattr(ce, f).restype = ctypes.c_int32
ce.ce_session_audio.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int32)]
ce.ce_session_audio.restype = ctypes.POINTER(ctypes.c_int16)
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
err = ctypes.c_char_p()
s = ce.ce_session_open(package.encode(), None, 0, None, overrides.encode() if overrides else None,
                       ids, datas, lens, n, None, None, None, None, 0, ctypes.byref(err))
if not s:
    sys.exit("open failed: " + (err.value.decode() if err.value else "(no message)"))
print("opened:", cfg["coreName"], cfg.get("version", "?"))

enter = 1 << cfg["input"]["buttons"].index("Select")  # the Enter key
run = hashlib.sha1()
lag = 0
for step in range(1, steps + 1):
    held = enter if step % 50 == 20 else 0
    # 1: a lag frame, 0: the frame read input, -1: the guest died
    r = ce.ce_session_frame_advance(s, held, 1)
    if r < 0:
        e = ce.ce_session_last_error(s)
        sys.exit(f"step {step}: the core stopped: {e.decode() if e else '(no message)'}")
    lag += r
    w, h = ce.ce_session_video_width(s), ce.ce_session_video_height(s)
    run.update(ctypes.string_at(ce.ce_session_video(s), w * h * 4))
cnt = ctypes.c_int32()
ce.ce_session_audio(s, ctypes.byref(cnt))
print(f"{steps} steps ({lag} lag): picture {w}x{h}, audio {cnt.value} frames a step, run {run.hexdigest()[:16]}")
if ppm:
    px = ctypes.string_at(ce.ce_session_video(s), w * h * 4)
    with open(ppm, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (w, h))
        f.write(bytes(px[i + c] for i in range(0, len(px), 4) for c in (2, 1, 0)))
ce.ce_session_free(s)
