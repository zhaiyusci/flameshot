#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

import json
import os
import subprocess
import sys
import traceback
from concurrent.futures import ThreadPoolExecutor

os.environ.setdefault("GRPC_VERBOSITY", "ERROR")
os.environ.setdefault("GLOG_minloglevel", "2")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

def log(message):
    print("flameshot-marker-worker: " + str(message), file=sys.stderr, flush=True)

def send(payload):
    print(json.dumps(payload, ensure_ascii=False), flush=True)

def truthy_env(name, default):
    value = os.environ.get(name)
    if value is None:
        return default
    return value.strip().lower() in ("1", "true", "yes", "on")

ROUTE_WORKER_SCRIPT = sys.argv[1] if len(sys.argv) > 1 else ""
COMMON_SCRIPT = sys.argv[2] if len(sys.argv) > 2 else ""
if not COMMON_SCRIPT:
    raise RuntimeError("marker OCR common worker script is unavailable")

def image_size(image_path):
    try:
        from PIL import Image
        with Image.open(image_path) as image:
            return image.size
    except Exception as error:
        log("image size probe failed: " + str(error))
        return None

def is_small_fallback_image(size):
    if not size:
        return False
    width, height = size
    return height <= 280 or width * height <= 180000

def should_run_formula_fallback(image_path, size=None):
    mode = os.environ.get("FLAMESHOT_MARKER_OCR_FORMULA_FALLBACK", "off").strip().lower()
    if mode in ("0", "false", "no", "off", "never"):
        return False
    if mode in ("1", "true", "yes", "on", "always"):
        return True
    return is_small_fallback_image(size if size is not None else image_size(image_path))

try:
    import torch
    marker_threads = int(os.environ.get("FLAMESHOT_MARKER_OCR_THREADS", "8"))
    marker_threads = max(1, marker_threads)
    marker_parallel_threads = int(
        os.environ.get(
            "FLAMESHOT_MARKER_OCR_PARALLEL_THREADS",
            str(max(1, marker_threads // 2)),
        )
    )
    marker_parallel_threads = max(1, marker_parallel_threads)
    torch.set_num_threads(marker_threads)
    try:
        torch.set_num_interop_threads(1)
    except RuntimeError:
        pass
    log("torch threads configured: num_threads=" + str(torch.get_num_threads()))
except Exception as error:
    torch = None
    marker_threads = int(os.environ.get("FLAMESHOT_MARKER_OCR_THREADS", "8") or "8")
    marker_parallel_threads = max(1, marker_threads // 2)
    log("torch thread configuration failed: " + str(error))

MARKER_PARALLEL_SMALL_IMAGES = truthy_env(
    "FLAMESHOT_MARKER_OCR_PARALLEL_SMALL_IMAGES", False
)

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

def set_torch_threads(threads, reason):
    if torch is None:
        return None
    previous = torch.get_num_threads()
    if previous != threads:
        torch.set_num_threads(threads)
        log(reason + ": torch num_threads=" + str(torch.get_num_threads()))
    return previous

def restore_torch_threads(previous, reason):
    if torch is None or previous is None:
        return
    if torch.get_num_threads() != previous:
        torch.set_num_threads(previous)
        log(reason + ": torch num_threads=" + str(torch.get_num_threads()))

def convert_marker_safely(image_path, label, force_layout_block=None):
    try:
        return convert_marker(image_path, label, force_layout_block)
    except Exception as error:
        log(label + " failed: " + str(error))
        return error_page(label, error)

class FormulaRouteWorker:
    def __init__(self):
        self.process = None
        self.next_id = 1

    def ensure_started(self):
        if self.process is not None and self.process.poll() is None:
            return
        if self.process is not None:
            log("formula route worker exited: code=" + str(self.process.poll()))
        self.stop()
        if not ROUTE_WORKER_SCRIPT:
            raise RuntimeError("formula route worker script is unavailable")
        environment = os.environ.copy()
        environment["FLAMESHOT_MARKER_ROUTE_THREADS"] = str(marker_parallel_threads)
        environment["FLAMESHOT_MARKER_PARENT_PID"] = str(os.getpid())
        self.process = subprocess.Popen(
            [sys.executable, "-u", "-c", ROUTE_WORKER_SCRIPT, COMMON_SCRIPT],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=sys.stderr,
            text=True,
            env=environment,
        )
        while True:
            message = self.read_message()
            if message.get("type") == "ready":
                return

    def read_message(self):
        if self.process is None or self.process.stdout is None:
            raise RuntimeError("formula route worker is not running")
        while True:
            line = self.process.stdout.readline()
            if not line:
                raise RuntimeError("formula route worker exited before replying")
            line = line.strip()
            if not line:
                continue
            try:
                return json.loads(line)
            except json.JSONDecodeError:
                log("formula route worker returned non-JSON output: " + line[:240])

    def recognize(self, image_path):
        self.ensure_started()
        request_id = self.next_id
        self.next_id += 1
        request = {
            "cmd": "recognize",
            "id": request_id,
            "image": image_path,
            "label": "Marker Formula",
            "force_layout_block": "Equation",
        }
        try:
            self.process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
            self.process.stdin.flush()
        except Exception:
            self.stop()
            self.ensure_started()
            self.process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
            self.process.stdin.flush()

        while True:
            message = self.read_message()
            if message.get("type") != "result":
                continue
            if message.get("id") != request_id:
                log("formula route worker returned unexpected id: " + str(message.get("id")))
                continue
            page = message.get("page") or {}
            if not message.get("ok", False):
                log("formula route worker failed")
            return page

    def stop(self):
        process = self.process
        self.process = None
        if process is None:
            return
        try:
            if process.poll() is None and process.stdin is not None:
                process.stdin.write('{"cmd":"quit"}\n')
                process.stdin.flush()
                process.stdin.close()
        except Exception:
            pass
        try:
            process.wait(timeout=1.5)
        except Exception:
            process.kill()
            process.wait(timeout=1.0)

FORMULA_WORKER = FormulaRouteWorker()

def recognize_parallel_formula_fallback(image_path):
    pages = []
    formula_page = None
    previous_threads = set_torch_threads(
        marker_parallel_threads, "parallel markdown route begin"
    )
    try:
        with ThreadPoolExecutor(max_workers=1) as executor:
            formula_future = executor.submit(FORMULA_WORKER.recognize, image_path)
            pages.append(convert_marker_safely(image_path, "Marker Markdown"))
            try:
                formula_page = formula_future.result()
            except Exception as error:
                log("formula route worker failed: " + str(error))
                formula_page = error_page("Marker Formula", error)
    finally:
        restore_torch_threads(previous_threads, "parallel markdown route end")
    if formula_page is not None:
        pages.append(formula_page)
    return pages

def recognize(image_path):
    pages = []
    size = image_size(image_path)
    run_formula_fallback = should_run_formula_fallback(image_path, size)
    if (
        run_formula_fallback
        and MARKER_PARALLEL_SMALL_IMAGES
        and is_small_fallback_image(size)
    ):
        log(
            "parallel small-image fallback begin: "
            + image_path
            + ", threads="
            + str(marker_parallel_threads)
        )
        return choose_pages(recognize_parallel_formula_fallback(image_path))

    try:
        log("document predict begin: " + image_path)
        pages.append(convert_marker(image_path, "Marker Markdown"))
    except Exception as error:
        log("document predict failed: " + str(error))
        pages.append(error_page("Marker Markdown", error))

    if run_formula_fallback:
        try:
            log("formula fallback begin: " + image_path)
            pages.append(convert_marker(image_path, "Marker Formula", "Equation"))
        except Exception as error:
            log("formula fallback failed: " + str(error))
            pages.append(error_page("Marker Formula", error))

    return choose_pages(pages)

def recognize_formula(image_path):
    try:
        return FORMULA_WORKER.recognize(image_path)
    except Exception as error:
        log("manual formula route failed: " + str(error))
        return error_page("Marker Formula", error)

for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    request = {}
    try:
        request = json.loads(line)
        if request.get("cmd") == "quit":
            break
        command = request.get("cmd") or "recognize"
        request_id = request.get("id")
        image_path = request.get("image") or ""
        if not image_path:
            send({
                "type": "result",
                "id": request_id,
                "ok": False,
                "error": "empty image path",
            })
            continue
        if command == "recognize_formula":
            page = recognize_formula(image_path)
            send({
                "type": "result",
                "id": request_id,
                "ok": page_has_content(page) and not bool(page.get("error")),
                "text": page.get("text", ""),
                "latex": page.get("latex", ""),
                "result_info": route_label(page),
                "fallback_text": "",
                "fallback_latex": "",
                "fallback_info": "",
                "extra_text": "",
                "extra_latex": "",
                "extra_info": "",
                "error": page.get("error", ""),
            })
            continue
        if command != "recognize":
            send({
                "type": "result",
                "id": request_id,
                "ok": False,
                "error": "unknown command: " + str(command),
            })
            continue
        primary, fallback, extra = recognize(image_path)
        send({
            "type": "result",
            "id": request_id,
            "ok": page_has_content(primary),
            "text": primary.get("text", ""),
            "latex": primary.get("latex", ""),
            "result_info": route_label(primary),
            "fallback_text": fallback.get("text", ""),
            "fallback_latex": fallback.get("latex", ""),
            "fallback_info": route_label(fallback),
            "extra_text": extra.get("text", ""),
            "extra_latex": extra.get("latex", ""),
            "extra_info": route_label(extra),
            "error": primary.get("error", ""),
        })
    except Exception as error:
        send({
            "type": "result",
            "id": request.get("id"),
            "ok": False,
            "error": str(error),
            "traceback": traceback.format_exc(),
        })
