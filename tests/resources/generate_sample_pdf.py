#!/usr/bin/env python3
"""Generate tests/resources/sample.pdf (docs/20 §5.2, workflow C2).

Hand-built minimal PDF with two pages of plain ASCII text drawn with the
standard Helvetica font - no external dependencies, byte offsets computed
here so the xref table stays valid. The extracted text is asserted in
tests/test_rag.cpp (extractPdfTextFromSamplePages).

Run from the repository root:
    python tests/resources/generate_sample_pdf.py
"""

import os

PAGE_TEXTS = [
    [
        "NexusAI PDF import sample",
        "page one alpha beta gamma delta epsilon zeta",
        "knowledge base document text extraction check",
    ],
    [
        "NexusAI PDF import sample",
        "page two eta theta iota kappa lambda mu",
        "second page proves the per page loop runs",
    ],
]

MEDIA_BOX = b"[0 0 612 792]"
FONT_RES = b"<< /Font << /F1 7 0 R >> >>"


def content_stream(lines):
    """Text-showing operators for one page (Helvetica, 18pt, top-down)."""
    ops = [b"BT", b"/F1 18 Tf", b"72 720 Td", b"20 TL"]
    for i, line in enumerate(lines):
        if i:
            ops.append(b"T*")
        ops.append(b"(" + line.encode("ascii") + b") Tj")
    ops.append(b"ET")
    return b"\n".join(ops)


def build():
    out = bytearray()
    offsets = {}

    def add(obj_num, body):
        offsets[obj_num] = len(out)
        out.extend(f"{obj_num} 0 obj\n".encode("ascii"))
        out.extend(body)
        out.extend(b"\nendobj\n")

    out.extend(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")

    add(1, b"<< /Type /Catalog /Pages 2 0 R >>")
    add(2, b"<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>")
    add(3, b"<< /Type /Page /Parent 2 0 R /MediaBox " + MEDIA_BOX
        + b" /Resources " + FONT_RES + b" /Contents 4 0 R >>")

    stream = content_stream(PAGE_TEXTS[0])
    add(4, b"<< /Length %d >>\nstream\n" % len(stream) + stream + b"\nendstream")

    add(5, b"<< /Type /Page /Parent 2 0 R /MediaBox " + MEDIA_BOX
        + b" /Resources " + FONT_RES + b" /Contents 6 0 R >>")

    stream = content_stream(PAGE_TEXTS[1])
    add(6, b"<< /Length %d >>\nstream\n" % len(stream) + stream + b"\nendstream")

    add(7, b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica"
        b" /Encoding /WinAnsiEncoding >>")

    xref_offset = len(out)
    count = 8  # object 0 (free head) + objects 1..7
    out.extend(b"xref\n0 %d\n" % count)
    out.extend(b"0000000000 65535 f \n")
    for n in range(1, count):
        out.extend(b"%010d 00000 n \n" % offsets[n])
    out.extend(b"trailer\n<< /Size %d /Root 1 0 R >>\n" % count)
    out.extend(b"startxref\n%d\n%%%%EOF\n" % xref_offset)
    return bytes(out)


def main():
    target = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sample.pdf")
    data = build()
    with open(target, "wb") as fh:
        fh.write(data)
    print("wrote %s (%d bytes)" % (target, len(data)))


if __name__ == "__main__":
    main()
