#!/usr/bin/env bash
# Nova Engine — compile GLSL shaders to SPIR-V (Vulkan 1.3).
#
# Usage:
#   tools/compile_shaders.sh [output_dir]
#
# Compiler detection order:
#   1. $GLSLANG_VALIDATOR (full path to glslangValidator / glslang / glslc)
#   2. glslangValidator / glslang / glslc from PATH
#
# Output: <output_dir>/<name>.vert.spv / <name>.frag.spv
# Default output dir: app/src/main/assets/shaders (the APK asset tree).
# The checked-in .spv files mean a plain checkout builds without any
# shader toolchain; this script regenerates them when shaders change.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHADER_DIR="${ROOT}/shaders"
OUT_DIR="${1:-${ROOT}/app/src/main/assets/shaders}"

find_compiler() {
    if [[ -n "${GLSLANG_VALIDATOR:-}" && -x "${GLSLANG_VALIDATOR}" ]]; then
        echo "${GLSLANG_VALIDATOR}"
        return 0
    fi
    local candidate
    for candidate in glslangValidator glslang glslc; do
        if command -v "${candidate}" >/dev/null 2>&1; then
            command -v "${candidate}"
            return 0
        fi
    done
    return 1
}

if ! COMPILER="$(find_compiler)"; then
    echo "error: no GLSL compiler found (glslangValidator/glslang/glslc)." >&2
    echo "       Install glslang (apt install glslang-tools / brew install glslang)" >&2
    echo "       or set GLSLANG_VALIDATOR=/path/to/glslangValidator" >&2
    exit 1
fi

COMPILER_NAME="$(basename "${COMPILER}")"
echo "nova: using ${COMPILER} (${COMPILER_NAME})"
mkdir -p "${OUT_DIR}"

shopt -s nullglob
sources=("${SHADER_DIR}"/*.vert "${SHADER_DIR}"/*.frag)
if [[ ${#sources[@]} -eq 0 ]]; then
    echo "error: no shaders found in ${SHADER_DIR}" >&2
    exit 1
fi

for src in "${sources[@]}"; do
    name="$(basename "${src}")"
    dst="${OUT_DIR}/${name}.spv"
    case "${COMPILER_NAME}" in
        glslc*)
            "${COMPILER}" --target-env=vulkan1.3 -O -o "${dst}" "${src}"
            ;;
        *)
            "${COMPILER}" --target-env vulkan1.3 -V -o "${dst}" "${src}"
            ;;
    esac
    echo "nova: ${name} -> $(basename "${dst}")"
done

echo "nova: $(echo "${#sources[@]}") shader(s) compiled into ${OUT_DIR}"
