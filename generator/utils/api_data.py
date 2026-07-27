import json
from typing import Iterable

from .api_path import find_extension_api_json


def load_extension_api_json(required_keys: Iterable[str] = ()) -> dict:
    api_path = find_extension_api_json()
    try:
        with open(api_path, "r", encoding="utf-8") as f:
            api_data = json.load(f)
    except FileNotFoundError as exc:
        raise FileNotFoundError(f"extension_api.json not found at {api_path}") from exc

    missing = [key for key in required_keys if key not in api_data]
    if missing:
        joined = ", ".join(sorted(missing))
        raise KeyError(f"extension_api.json missing required key(s): {joined}")

    return api_data
