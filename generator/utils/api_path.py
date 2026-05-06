import os


def project_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def find_extension_api_json():
    root = project_root()
    candidates = []

    value = os.environ.get("GODOT_EXTENSION_API_JSON")
    if value:
        candidates.append(value)

    for env_name in ("GODOT_CPP_DIR", "GODOT_ENGINE_DIR"):
        value = os.environ.get(env_name)
        if value:
            candidates.append(os.path.join(value, "gdextension", "extension_api.json"))

    candidates.extend([
        os.path.join(root, "third", "godot-cpp", "gdextension", "extension_api.json"),
        os.path.join(root, "godot-cpp", "gdextension", "extension_api.json"),
    ])

    for path in candidates:
        if path and os.path.exists(path):
            return path

    return candidates[0]
