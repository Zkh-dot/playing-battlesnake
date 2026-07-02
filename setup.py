from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


SOURCE_FILES = [
    "battlesnake/c-core/datatypes/coord.c",
    "battlesnake/c-core/datatypes/snake.c",
    "battlesnake/c-core/datatypes/board.c",
    "battlesnake/c-core/core/core_algorithms.c",
    "battlesnake/c-core/py-datatypes/py_datatypes.c",
    "battlesnake/c-core/py-core/py_core.c",
    "battlesnake/c-core/py-datatypes/init_module.c",
]


ext_modules = [
    Extension(
        "battlesnake.battlesnake_native",
        sources=SOURCE_FILES,
        include_dirs=["battlesnake/c-core"],
        language="c",
        extra_compile_args=["-std=c2x", "-D_POSIX_C_SOURCE=200809L"],
    )
]


class BuildExt(build_ext):
    """Build extension with the same explicit C mode as the reference project."""

    def build_extensions(self):
        super().build_extensions()


setup(
    name="playing-battlesnake",
    version="0.1.0",
    packages=["battlesnake", "battlesnake.core", "battlesnake.strategies", "battlesnake.training"],
    package_data={"battlesnake": ["*.pyi", "py.typed"]},
    install_requires=["fastapi", "uvicorn", "pydantic"],
    python_requires=">=3.11",
    ext_modules=ext_modules,
    cmdclass={"build_ext": BuildExt},
)
