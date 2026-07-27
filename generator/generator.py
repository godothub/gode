import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from generator.builtin_classes_generator import BuiltinClassGenerator
    from generator.class_generator import ClassGenerator
    from generator.dts_generator import DtsGenerator
    from generator.register_generator import RegisterGenerator
    from generator.utility_functions_generator import UtilityFunctionsGenerator
else:
    from .builtin_classes_generator import BuiltinClassGenerator
    from .class_generator import ClassGenerator
    from .dts_generator import DtsGenerator
    from .register_generator import RegisterGenerator
    from .utility_functions_generator import UtilityFunctionsGenerator


GENERATOR_CLASSES = (
    BuiltinClassGenerator,
    ClassGenerator,
    RegisterGenerator,
    UtilityFunctionsGenerator,
    DtsGenerator,
)

GENERATOR_DIR = Path(__file__).resolve().parent
ROOT_DIR = GENERATOR_DIR.parent
TEMPLATE_DIR = GENERATOR_DIR / "templates"
INCLUDE_DIR = ROOT_DIR / "include" / "generated"
SRC_DIR = ROOT_DIR / "src" / "generated"


def main():
    print(f"Using template directory: {TEMPLATE_DIR}")
    print(f"Using include directory: {INCLUDE_DIR}")
    print(f"Using src directory: {SRC_DIR}")

    print(f"Found {len(GENERATOR_CLASSES)} generators.")
    print("\nStarting code generation...")
    failed_generators = []
    for gen_class in GENERATOR_CLASSES:
        print(f"Running {gen_class.__name__}...")
        try:
            config = {
                "include_dir": str(INCLUDE_DIR),
                "src_dir": str(SRC_DIR),
            }
            generator = gen_class(str(TEMPLATE_DIR), config)
            generator.run()
        except Exception as e:
            print(f"Error running {gen_class.__name__}: {e}")
            import traceback
            traceback.print_exc()
            failed_generators.append(gen_class.__name__)

    if failed_generators:
        print("\nCode generation failed:")
        for name in failed_generators:
            print(f"  - {name}")
        return 1

    print("\nCode generation completed.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
