#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

import json
import os
import re
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

def result_page(label, text="", latex="", score=0.0, error=""):
    return {
        "label": label,
        "text": str(text or "").strip(),
        "latex": str(latex or "").strip(),
        "score": float(score or 0.0),
        "error": str(error or "").strip(),
    }

def error_page(label, error):
    return result_page(label, text=label + " failed:\n" + str(error), score=0.0, error=str(error))

def math_marker_count(text):
    text = str(text or "")
    return len(
        re.findall(
            r"\$\$[\s\S]+?\$\$|\\\[[\s\S]+?\\\]|\\\([\s\S]+?\\\)|"
            r"(?<!\$)\$[^$\n]{1,500}\$(?!\$)",
            text,
        )
    )

def has_markdown_table(text):
    text = str(text or "")
    return bool(re.search(r"^\|.*\|\s*$\n^\|[-:\s|]+\|\s*$", text, re.M))

def score_markdown(text, label):
    text = str(text or "").strip()
    if not text:
        return 0.0
    score = min(1.0, len(text) / 80.0)
    markers = math_marker_count(text)
    if markers:
        score += 0.4
    if has_markdown_table(text):
        score -= 0.7
    if label == "Marker Formula" and markers:
        score += 0.3
    return max(0.0, score)

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
send({"type": "ready"})

def convert_marker(image_path, label, force_layout_block=None):
    config = {
        "pdftext_workers": 1,
        "disable_tqdm": True,
        "extract_images": False,
    }
    if force_layout_block:
        config["force_layout_block"] = force_layout_block
    converter = PdfConverter(
        config=config,
        artifact_dict=MODELS.copy(),
        renderer=RENDERER,
    )
    start = time.time()
    rendered = converter(image_path)
    markdown, _, _ = text_from_rendered(rendered)
    elapsed = time.time() - start
    markdown = str(markdown or "").strip()
    log(label + " done: seconds=%.2f, chars=%d, math=%d" % (
        elapsed,
        len(markdown),
        math_marker_count(markdown),
    ))
    return result_page(
        label,
        text=markdown,
        score=score_markdown(markdown, label),
    )

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
