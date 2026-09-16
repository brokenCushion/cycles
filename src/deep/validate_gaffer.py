# SPDX-License-Identifier: Apache-2.0
"""Run with: gaffer python validate_gaffer.py FIXTURE_DIRECTORY.

Reads the generated M2 fixtures in Gaffer, tests native deep data, flattening,
and partial-depth opacity, and saves a small reviewable .gfr graph.
"""

import csv
import json
import math
from pathlib import Path
import sys

import Gaffer
import GafferImage
import imath


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def tile_index(point):
    size = GafferImage.ImagePlug.tileSize()
    origin = imath.V2i((point.x // size) * size, (point.y // size) * size)
    return origin, (point.y - origin.y) * size + point.x - origin.x


def flat_alpha(plug, point):
    origin, index = tile_index(point)
    return float(plug.channelData("A", origin)[index])


def deep_pixel(plug, point):
    origin, index = tile_index(point)
    offsets = plug.sampleOffsets(origin)
    start = int(offsets[index - 1]) if index else 0
    end = int(offsets[index])
    arrays = [plug.channelData(c, origin) for c in ("Z", "ZBack", "A")]
    return [tuple(float(a[j]) for a in arrays) for j in range(start, end)]


def expected_samples():
    return [
        [], [(2, 1)], [(2, .5)], [(2, .5), (8, 1)],
        [(2, .25), (8, .5)], [(2, .25), (8, .5), (10, 1)],
        [(2, .25)], [(2, .625)], [],
        [(1 + i * .25, .0001) for i in range(256)],
        [(2, .125), (8, .5)], [(2, .3)],
    ]


def validate(directory):
    with (directory / "expected_pixels.csv").open(newline="") as f:
        csv_pixels = list(csv.DictReader(f))
    expected = expected_samples()
    check(len(csv_pixels) == len(expected), "Unexpected fixture manifest")
    for i, row in enumerate(csv_pixels):
        check(int(row["file_x"]) == i % 4 and int(row["file_y"]) == i // 4,
              "Unexpected fixture pixel order")
        check(int(row["samples"]) == len(expected[i]), "Manifest sample count mismatch")

    report = {"gaffer_version": Gaffer.About.versionString(), "files": [],
              "max_flat_alpha_error": 0.0, "max_partial_alpha_error": 0.0}
    for path in sorted(directory.glob("*.exr")):
        check(path.name.startswith(("surfaces_", "empty_")), "Unexpected EXR fixture")
        empty = path.name.startswith("empty_")
        window = 0 if empty else int(path.name.split("_")[1])
        dw, display, aspect = [
            ((0, 0, 3, 2), (0, 0, 3, 2), 1.0),
            ((-2, -1, 1, 1), (-4, -3, 5, 6), 1.5),
            ((5, 7, 8, 9), (0, 0, 11, 13), .75),
        ][window]
        reader = GafferImage.ImageReader()
        reader["fileName"].setValue(path.as_posix())
        flat = GafferImage.DeepToFlat()
        flat["in"].setInput(reader["out"])
        clip = GafferImage.DeepSlice()
        clip["in"].setInput(reader["out"])
        clip["nearClip"]["enabled"].setValue(False)
        clip["farClip"]["enabled"].setValue(True)
        clip["flatten"].setValue(True)
        check(list(reader["out"].viewNames()) == ["left"], "View identity changed")
        context = Gaffer.Context()
        context["image:viewName"] = "left"
        with context:
            check(reader["out"]["deep"].getValue(), "Gaffer did not recognize deep input")
            check(not flat["out"]["deep"].getValue(), "DeepToFlat did not flatten")
            check(set(reader["out"].channelNames()) == {"A", "Z", "ZBack"},
                  "Channel names changed")
            fmt = reader["out"]["format"].getValue()
            file_display = imath.Box2i(imath.V2i(*display[:2]), imath.V2i(*display[2:]))
            file_data = imath.Box2i(imath.V2i(*dw[:2]), imath.V2i(*dw[2:]))
            check(fmt.toEXRSpace(fmt.getDisplayWindow()) == file_display, "Display window mismatch")
            check(fmt.toEXRSpace(reader["out"]["dataWindow"].getValue()) == file_data,
                  "Data window mismatch")
            check(fmt.getPixelAspect() == aspect, "Pixel aspect mismatch")
            metadata = reader["out"]["metadata"].getValue()
            check(int(metadata["cycles:frame"].value) == 1001, "Frame metadata mismatch")
            for i, samples in enumerate(expected):
                samples = [] if empty else samples
                point = fmt.fromEXRSpace(imath.V2i(dw[0] + i % 4, dw[1] + i // 4))
                actual = deep_pixel(reader["out"], point)
                check(len(actual) == len(samples), f"Sample count mismatch: {path.name}, pixel {i}")
                for (z, back, alpha), (ez, ea) in zip(actual, samples):
                    check(z == ez and back == ez and abs(alpha - ea) <= 1e-7,
                          f"Deep sample mismatch: {path.name}, pixel {i}")
                target = 0.0 if empty else float(csv_pixels[i]["flattened_alpha"])
                error = abs(flat_alpha(flat["out"], point) - target)
                check(math.isfinite(error) and error <= 1e-6, f"Flattening mismatch: {path.name}, pixel {i}")
                report["max_flat_alpha_error"] = max(report["max_flat_alpha_error"], error)
            for far in (1.125, 3.125, 9.125, 70.0):
                clip["farClip"]["value"].setValue(far)
                for i, samples in enumerate(expected):
                    samples = [] if empty else samples
                    t = math.prod(1.0 - alpha for z, alpha in samples if z < far)
                    point = fmt.fromEXRSpace(imath.V2i(dw[0] + i % 4, dw[1] + i // 4))
                    error = abs(flat_alpha(clip["out"], point) - (1.0 - t))
                    check(math.isfinite(error) and error <= 1e-6,
                          f"Partial-depth mismatch: {path.name}, pixel {i}, depth {far}")
                    report["max_partial_alpha_error"] = max(report["max_partial_alpha_error"], error)
        report["files"].append(path.name)
        print("PASS Gaffer deep read / flatten / depth slices:", path.name)

    check(len(report["files"]) == 8, "Expected all eight EXR fixtures")
    # Save an inspectable graph without changing the user's Gaffer preferences.
    script = Gaffer.ScriptNode()
    script["reader"] = GafferImage.ImageReader()
    script["reader"]["fileName"].setValue((directory / "surfaces_0_none.exr").as_posix())
    script["leftView"] = GafferImage.SelectView()
    script["leftView"]["in"].setInput(script["reader"]["out"])
    script["leftView"]["view"].setValue("left")
    script["flatten"] = GafferImage.DeepToFlat()
    script["flatten"]["in"].setInput(script["leftView"]["out"])
    script["nearOpacity"] = GafferImage.DeepSlice()
    script["nearOpacity"]["in"].setInput(script["leftView"]["out"])
    script["nearOpacity"]["farClip"]["enabled"].setValue(True)
    script["nearOpacity"]["farClip"]["value"].setValue(3.125)
    script["nearOpacity"]["flatten"].setValue(True)
    script["fileName"].setValue((directory / "deep_validation.gfr").as_posix())
    script.save()
    (directory / "gaffer_validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    check(len(sys.argv) == 2, "Usage: gaffer python validate_gaffer.py FIXTURE_DIRECTORY")
    validate(Path(sys.argv[1]).resolve())
