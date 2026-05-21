#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

import json
import os
import sys
import threading
import time
import traceback

os.environ.setdefault("GRPC_VERBOSITY", "ERROR")
os.environ.setdefault("GLOG_minloglevel", "2")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

def log(message):
    print("flameshot-marker-route-worker: " + str(message), file=sys.stderr, flush=True)

def send(payload):
    print(json.dumps(payload, ensure_ascii=False), flush=True)

COMMON_SCRIPT = sys.argv[1] if len(sys.argv) > 1 else ""
if not COMMON_SCRIPT:
    raise RuntimeError("marker OCR common worker script is unavailable")

def start_parent_monitor():
    try:
        parent_pid = int(os.environ.get("FLAMESHOT_MARKER_PARENT_PID", "0"))
    except Exception:
        return
    if parent_pid <= 0:
        return

    def monitor():
        while True:
            time.sleep(1.0)
            if os.getppid() == 1:
                os._exit(0)
            try:
                os.kill(parent_pid, 0)
            except OSError:
                os._exit(0)

    threading.Thread(target=monitor, daemon=True).start()

start_parent_monitor()

try:
    import torch
    route_threads = int(os.environ.get("FLAMESHOT_MARKER_ROUTE_THREADS", "4"))
    route_threads = max(1, route_threads)
    torch.set_num_threads(route_threads)
    try:
        torch.set_num_interop_threads(1)
    except RuntimeError:
        pass
    log("torch threads configured: num_threads=" + str(torch.get_num_threads()))
except Exception as error:
    log("torch thread configuration failed: " + str(error))

log("import marker")
from marker.converters.pdf import PdfConverter
from marker.models import create_model_dict
from marker.output import text_from_rendered

RENDERER = "marker.renderers.markdown.MarkdownRenderer"

log("load models begin")
MODELS = create_model_dict()
log("load models done")
exec(COMMON_SCRIPT, globals())
send({"type": "ready"})

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    request = {}
    try:
        request = json.loads(line)
        if request.get("cmd") == "quit":
            break
        request_id = request.get("id")
        image_path = request.get("image") or ""
        label = request.get("label") or "Marker Formula"
        force_layout_block = request.get("force_layout_block") or None
        if not image_path:
            send({
                "type": "result",
                "id": request_id,
                "ok": False,
                "page": error_page(label, "empty image path"),
            })
            continue
        try:
            page = convert_marker(image_path, label, force_layout_block)
            send({"type": "result", "id": request_id, "ok": True, "page": page})
        except Exception as error:
            send({
                "type": "result",
                "id": request_id,
                "ok": False,
                "page": error_page(label, error),
                "traceback": traceback.format_exc(),
            })
    except Exception as error:
        send({
            "type": "result",
            "id": request.get("id"),
            "ok": False,
            "page": error_page("Marker Formula", error),
            "traceback": traceback.format_exc(),
        })
log("route worker exiting")
