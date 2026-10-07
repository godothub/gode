import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / '.github/shell/trim-typescript.py'
spec = importlib.util.spec_from_file_location('trim_typescript', SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class TypeScriptPackageTests(unittest.TestCase):
    def test_preserves_compiler_standard_libraries_and_licenses(self):
        retained = {'package.json', 'lib/typescript.js', 'LICENSE.txt',
                    'ThirdPartyNoticeText.txt', 'lib/lib.es2024.d.ts', 'lib/lib.dom.d.ts'}
        removed = {'bin/tsc', 'lib/_tsc.js', 'lib/tsserver.js',
                   'lib/typescript.d.ts', 'lib/en/diagnosticMessages.generated.json', 'README.md'}
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            for name in retained | removed:
                path = package / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(name)
            module.trim(package)
            module.trim(package)  # Cached packages follow the same trim operation.
            actual = {path.relative_to(package).as_posix() for path in package.rglob('*') if path.is_file()}
            self.assertEqual(retained, actual)
            for name in retained:
                self.assertEqual(name, (package / name).read_text())
            self.assertFalse((package / 'bin').exists())

    def test_incomplete_package_is_rejected_before_deleting_files(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            (package / 'README.md').write_text('keep on failure')
            with self.assertRaises(FileNotFoundError):
                module.trim(package)
            self.assertEqual('keep on failure', (package / 'README.md').read_text())
