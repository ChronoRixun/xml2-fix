"""V-TBD (number assigned at merge): no conversation data scan at each capture."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
engine = (ROOT / "src/save_format.cpp").read_text(encoding="utf-8")
api = (ROOT / "src/lcsave/api_engine.cpp").read_text(encoding="utf-8")
internal = (ROOT / "src/lcsave/internal.hpp").read_text(encoding="utf-8")

def block(text, first, last):
    return text.split(first, 1)[1].split(last, 1)[0]

capture = block(engine, "bool __fastcall capture_hook(", "void reset_zone_record(")
ready = block(engine, "void ensure_ready()", "void refuse(")
checks = {
    "capture uses the shared digest API without direct filesystem reads":
        "lcsave::conversation_digest(tables," in capture and
        not any(name in capture for name in ("read_file_bytes", "recursive_directory_iterator", "CreateFile")),
    "LC readiness primes identity before importing or loading player files":
        "lcsave::prepare_conversation_digest(tables," in ready and
        ready.index("lcsave::prepare_conversation_digest(tables,") < ready.index("import_old_saves()"),
    "pinned engine lookup returns before any inventory with a nonblocking change check":
        "WaitForSingleObject(cache.change_notification, 0)" in api and
        api.index("if (cache.change_notification)") < api.index("const auto files = conversation_files(root)") and
        "out = cache.digest; return;" in api,
    "cache is owned by loaded build tables":
        "std::shared_ptr<ConversationCache> conversations" in internal and
        "tables.data->build->conversations" in api,
    "unchanged files use attribute-only version validation":
        "FILE_READ_ATTRIBUTES" in api and "basic.ChangeTime" in api and
        "file.file_id" in api and "files == cache.files" in api,
    "refresh rejects an unstable tree before publication":
        "files != conversation_files(root)" in api and
        api.index("files != conversation_files(root)") < api.index("cache.valid = std::all_of"),
}
for name, ok in checks.items():
    print(("PASS " if ok else "FAIL ") + name)
raise SystemExit(0 if all(checks.values()) else 1)
