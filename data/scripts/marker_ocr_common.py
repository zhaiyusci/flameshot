#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

import re
import time


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
