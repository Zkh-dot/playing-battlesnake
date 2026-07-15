"""Build metadata for the Python extension.

The native HTTP server is built by tools/build_native_server.sh from the same
C core files plus battlesnake/c-core/server/*.c. Keep SOURCE_FILES limited to
the CPython extension sources so `pip install .` continues to work.
"""

import os
import subprocess
import sys
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


SOURCE_FILES = [
    "battlesnake/c-core/datatypes/coord.c",
    "battlesnake/c-core/datatypes/snake.c",
    "battlesnake/c-core/datatypes/board.c",
    "battlesnake/c-core/core/core_algorithms.c",
    "battlesnake/c-core/core/standard_ffa.c",
    "battlesnake/c-core/core/position_eval.c",
    "battlesnake/c-core/core/search_stats.c",
    "battlesnake/c-core/core/duel_weight_profiles_generated.c",
    "battlesnake/c-core/core/search_workspace.c",
    "battlesnake/c-core/core/search_state.c",
    "battlesnake/c-core/core/zobrist.c",
    "battlesnake/c-core/core/transposition_table.c",
    "battlesnake/c-core/py-datatypes/py_datatypes.c",
    "battlesnake/c-core/py-core/py_core.c",
    "battlesnake/c-core/py-datatypes/init_module.c",
]
PROJECT_ROOT = Path(__file__).resolve().parent


enable_position_eval_openmp = os.environ.get("BATTLESNAKE_ENABLE_OPENMP") == "1"
enable_minimax_openmp = os.environ.get("BATTLESNAKE_ENABLE_MINIMAX_OPENMP") == "1"
extra_compile_args = ["-std=c2x", "-D_POSIX_C_SOURCE=200809L"]
extra_link_args = []
if enable_position_eval_openmp:
    extra_compile_args.extend(["-DCORE_POSITION_EVAL_OPENMP", "-fopenmp"])
if enable_minimax_openmp:
    extra_compile_args.extend(["-DCORE_MINIMAX_OPENMP", "-fopenmp"])
if enable_position_eval_openmp or enable_minimax_openmp:
    extra_link_args.append("-fopenmp")


ext_modules = [
    Extension(
        "battlesnake.battlesnake_native",
        sources=SOURCE_FILES,
        include_dirs=["battlesnake/c-core"],
        libraries=["m"],
        language="c",
        extra_compile_args=extra_compile_args,
        extra_link_args=extra_link_args,
    )
]


class BuildExt(build_ext):
    """Build extension with the same explicit C mode as the reference project."""

    def run(self):
        subprocess.run(
            [
                sys.executable,
                str(PROJECT_ROOT / "tools/tuning/generate_duel_weight_profiles.py"),
                "--check",
            ],
            cwd=PROJECT_ROOT,
            check=True,
        )
        super().run()


setup(
    name="playing-battlesnake",
    version="0.1.0",
    packages=[
        "battlesnake",
        "battlesnake.core",
        "battlesnake.strategies",
        "battlesnake.training",
        "battlesnake.training.opponent_model",
    ],
    package_data={"battlesnake": ["*.pyi", "py.typed"]},
    install_requires=[],
    python_requires=">=3.11",
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExt},
)
