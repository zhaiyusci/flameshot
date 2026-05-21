#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

import json
import os
import re
import subprocess
import sys
import time
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

def result_page(label, text="", latex="", score=0.0, error=""):
    return {
        "label": label,
        "text": str(text or "").strip(),
        "latex": str(latex or "").strip(),
        "score": float(score or 0.0),
        "error": str(error or "").strip(),
    }

def page_has_content(page):
    return bool(page.get("text") or page.get("latex"))

def route_label(page):
    label = page.get("label") or "OCR"
    error = page.get("error") or ""
    if error:
        return label + " failed"
    return label

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

def combined_page_text(page):
    text = page.get("text") or ""
    latex = page.get("latex") or ""
    if text and latex:
        return text + "\n\nLaTeX:\n" + latex
    return text or latex

def choose_pages(pages):
    usable = [page for page in pages if page_has_content(page)]
    if not usable:
        return result_page("OCR"), result_page("Fallback"), result_page("Text OCR")
    usable.sort(
        key=lambda page: (
            page.get("score", 0.0),
            len(combined_page_text(page)),
        ),
        reverse=True,
    )
    primary = usable[0]
    fallback = usable[1] if len(usable) > 1 else result_page("Fallback")
    extra = usable[2] if len(usable) > 2 else result_page("Text OCR")
    return primary, fallback, extra

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

ROUTE_WORKER_SCRIPT = sys.argv[1] if len(sys.argv) > 1 else ""


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
            [sys.executable, "-u", "-c", ROUTE_WORKER_SCRIPT],
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
