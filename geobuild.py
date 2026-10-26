from typing import TYPE_CHECKING
if TYPE_CHECKING:
    from .build.geobuild.prelude import *

def main(build: Build):
    config = build.config

    build.add_include_dir("src")
    build.add_include_dir("include")
    build.add_source_dir("src/*.cpp", recursive=True)
    build.add_source_dir("src/external/*.c")

    build.enable_mod_json_generation("mod.template.json")
    build.relax_geode_requirement()
    build.add_api_headers("include/**/*.hpp")
    # build.mod_json["api"]["cmake"] = [
    #     "cmake/DependencyMain.cmake"
    # ]

    build.add_cpm_dep("stephenberry/glaze", "v8.0.0", link_name="glaze::glaze")
