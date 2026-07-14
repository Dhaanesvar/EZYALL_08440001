Import("env")
from SCons.Script import COMMAND_LINE_TARGETS

import errno
import os
import subprocess


def fail_with_hint(message):
    print("[UPLOAD-GUARD][ERROR] " + message)
    print("[UPLOAD-GUARD][HINT] Close CuteCom/YAT/serial monitor and retry.")
    env.Exit(1)


is_upload_target = any(t in ("upload", "program", "buildfs", "uploadfs") for t in COMMAND_LINE_TARGETS)

if not is_upload_target:
    print("[UPLOAD-GUARD] Skipped (non-upload target): " + ",".join(COMMAND_LINE_TARGETS))
else:
    port = env.subst("$UPLOAD_PORT")

    if not port:
        fail_with_hint("UPLOAD_PORT is not configured.")

    if not os.path.exists(port):
        fail_with_hint("Upload port not found: " + port)

    try:
        # If another app opened the TTY exclusively, this open call returns EBUSY.
        fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        os.close(fd)
    except OSError as exc:
        if exc.errno in (errno.EBUSY, errno.EACCES, errno.EPERM):
            try:
                out = subprocess.check_output(["fuser", "-v", port], stderr=subprocess.STDOUT, text=True)
                print("[UPLOAD-GUARD] Port in use details:\n" + out)
            except Exception:
                pass
            fail_with_hint("Upload port is busy: " + port)
        fail_with_hint("Cannot open upload port " + port + ": " + str(exc))

    print("[UPLOAD-GUARD] Port ready: " + port)
