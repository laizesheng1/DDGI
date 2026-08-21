#!/usr/bin/env python3
"""Dependency-free structural contract for a CMake-based DDGI checkout."""

from __future__ import annotations

import argparse
from pathlib import Path


REQUIRED_PATHS = (
    "CMakeLists.txt",
    "src/app/main.cpp",
    "src/ddgi/DDGIVolume.cpp",
    "src/rt/RayTracingPipeline.cpp",
    "src/renderer/Renderer.cpp",
    "src/sdf/SDFGenerator.cpp",
    "include/ddgi/DDGITypes.h",
    "shaders/glsl/common/ddgi_query.glsl",
    "shaders/glsl/ddgi/ddgi_update_irradiance.comp",
    "shaders/glsl/rt/ddgi_trace.rgen",
    "shaders/glsl/lighting/ddgi_lighting.frag",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    harness_root = Path(__file__).resolve().parents[2]
    default_root = harness_root if (harness_root / "CMakeLists.txt").is_file() else harness_root.parent
    parser.add_argument(
        "--project-root",
        type=Path,
        default=default_root,
        help="DDGI repository root; defaults to the harness root after installation or its parent in this template checkout.",
    )
    args = parser.parse_args()
    root = args.project_root.resolve()

    missing = [path for path in REQUIRED_PATHS if not (root / path).is_file()]
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8") if not missing else ""
    required_cmake_terms = ("project(VK_DDGI", "find_package(Vulkan REQUIRED)", "add_custom_target(", "Shaders")
    absent_terms = [term for term in required_cmake_terms if term not in cmake]

    if missing or absent_terms:
        if missing:
            print("Missing required DDGI paths:")
            print("\n".join(f"  - {path}" for path in missing))
        if absent_terms:
            print("Missing expected CMake terms:")
            print("\n".join(f"  - {term}" for term in absent_terms))
        return 1

    print(f"DDGI project contract passed: {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
