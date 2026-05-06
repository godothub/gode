import os
import sys
import importlib
import pkgutil
from core.base_generator import CodeGenerator

# Configuration
TEMPLATE_DIR = os.path.join(os.path.dirname(__file__), 'templates')
# Define base output directories for include and src
INCLUDE_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'include', 'generated')
SRC_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'src', 'generated')

def discover_generators(package_name):
    """
    Dynamically discover all generator classes in the given package.
    """
    generators = []
    package = importlib.import_module(package_name)
    
    # Iterate over all modules in the package
    for _, name, is_pkg in pkgutil.walk_packages(package.__path__, package.__name__ + "."):
        if is_pkg:
            continue
            
        try:
            module = importlib.import_module(name)
            # Find subclasses of CodeGenerator
            for attr_name in dir(module):
                attr = getattr(module, attr_name)
                if isinstance(attr, type) and issubclass(attr, CodeGenerator) and attr is not CodeGenerator:
                    generators.append(attr)
        except ImportError as e:
            print(f"Failed to import module {name}: {e}")

    return generators

def main():
    print(f"Using template directory: {TEMPLATE_DIR}")
    print(f"Using include directory: {INCLUDE_DIR}")
    print(f"Using src directory: {SRC_DIR}")

    # Discover generators in builtin, class, utility, register packages
    packages = ['builtin', 'class', 'register', 'dts']
    all_generators = []

    # Ensure current directory is in sys.path
    sys.path.append(os.path.dirname(__file__))

    for pkg in packages:
        print(f"Scanning package: {pkg}...")
        gens = discover_generators(pkg)
        all_generators.extend(gens)
        print(f"  Found {len(gens)} generators.")

    # Run generators
    print("\nStarting code generation...")
    for gen_class in all_generators:
        print(f"Running {gen_class.__name__}...")
        try:
            # We pass a tuple or dict of output directories, or let the generator decide based on arguments
            # For backward compatibility with CodeGenerator init, we might need to adjust.
            # Let's pass a config dict instead of a single output_dir
            config = {
                'include_dir': INCLUDE_DIR,
                'src_dir': SRC_DIR
            }
            generator = gen_class(TEMPLATE_DIR, config)
            generator.run()
        except Exception as e:
            print(f"Error running {gen_class.__name__}: {e}")
            import traceback
            traceback.print_exc()

    print("\nCode generation completed.")

if __name__ == "__main__":
    main()
