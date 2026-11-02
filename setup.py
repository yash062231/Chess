from pathlib import Path
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import pybind11

ROOT = Path(__file__).parent.resolve()
sources = [str(ROOT / "engine_cpp" / "bindings.cpp"), str(ROOT / "core_c" / "bitboard.c")]
class Cpp17BuildExt(build_ext):
    def build_extensions(self):
        original_compile = self.compiler._compile
        def compile_source(obj, src, ext, cc_args, extra_postargs, pp_opts):
            args = list(extra_postargs or [])
            if src.lower().endswith((".cpp", ".cc", ".cxx")):
                args.append("/std:c++17" if self.compiler.compiler_type == "msvc" else "-std=c++17")
            elif self.compiler.compiler_type != "msvc":
                args.append("-std=c99")
            return original_compile(obj, src, ext, cc_args, args, pp_opts)
        self.compiler._compile = compile_source
        super().build_extensions()

ext = Extension("chess_engine", sources=sources, include_dirs=[pybind11.get_include(), str(ROOT)], language="c++", extra_compile_args=["/O2"] if __import__("sys").platform == "win32" else ["-O3"])
setup(name="chess-engine-ai", version="1.0.0", description="C bitboard chess engine with C++ search and Python bindings", ext_modules=[ext], cmdclass={"build_ext": Cpp17BuildExt}, zip_safe=False)
