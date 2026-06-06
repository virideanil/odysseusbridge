"""
unreal_helpers — curated, reliable wrappers over OdysseusBridge's `run_python`.

Call them from any MCP client through the bridge's `run_python` tool. Either drop this
file into your project's `Content/Python` (Unreal auto-imports it) and:

    import unreal_helpers as u
    print(u.project())
    print(u.spawn_static('/Engine/BasicShapes/Cube.Cube', 8))
    print(u.screenshot())

…or paste a function body straight into `run_python`. Every call is wrapped so a wrong
API name returns "ERR: <reason>" instead of a raw traceback. UE 5.7.

Convenience only — `run_python` already reaches the whole `unreal` API. Not compiled in
CI; verify behaviour in your editor.
"""
import os
import unreal


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def _guard(fn):
    """Wrap a function so it always returns a clean string, never a raw traceback."""
    def wrapped(*a, **k):
        try:
            return fn(*a, **k)
        except Exception as e:  # noqa: BLE001 — the caller needs the message, not the stack
            return f"ERR: {fn.__name__}: {type(e).__name__}: {e}"
    wrapped.__name__ = fn.__name__
    wrapped.__doc__ = fn.__doc__
    return wrapped


def _find(label):
    for a in _eas().get_all_level_actors():
        if a and a.get_actor_label() == label:
            return a
    return None


# ── recon ────────────────────────────────────────────────────────────────────
@_guard
def project():
    """Project file name, current level, and live actor count."""
    proj = os.path.basename(unreal.Paths.get_project_file_path()) or "?"
    lvl = _world().get_name() if _world() else "?"
    return f"project={proj} level={lvl} actors={len(_eas().get_all_level_actors())}"


@_guard
def actors(limit=40):
    """List up to `limit` level actors as 'label [Class]'."""
    out = []
    for a in _eas().get_all_level_actors():
        if a:
            out.append(f"{a.get_actor_label()} [{a.get_class().get_name()}]")
        if len(out) >= limit:
            break
    return f"{len(out)} actor(s):\n" + "\n".join(out)


@_guard
def find_assets(folder="/Game", recursive=True, limit=60):
    """List asset paths under a content folder."""
    paths = list(unreal.EditorAssetLibrary.list_assets(folder, recursive=recursive, include_folder=False))[:limit]
    return f"{len(paths)} asset(s) under {folder}:\n" + "\n".join(paths)


# ── act (labeled + verifiable) ───────────────────────────────────────────────
@_guard
def spawn_static(mesh_path, n=1, spacing=160.0, origin=(0.0, 0.0, 0.0), label="Mesh"):
    """Spawn n StaticMeshActors with the given mesh, in a grid. Returns the count."""
    mesh = unreal.load_asset(mesh_path)
    if not mesh:
        return f"ERR: mesh not found: {mesh_path}"
    eas = _eas()
    cols = max(1, int(n ** 0.5))
    made = 0
    for i in range(int(n)):
        r, c = divmod(i, cols)
        loc = unreal.Vector(origin[0] + c * spacing, origin[1] + r * spacing, origin[2])
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor, loc, unreal.Rotator(0, 0, 0))
        if not a:
            continue
        a.static_mesh_component.set_static_mesh(mesh)
        a.set_actor_label(f"{label}_{i + 1}")
        made += 1
    return f"spawned {made}/{n} static mesh actor(s)"


@_guard
def spawn_skeletal(mesh_path, n=1, spacing=160.0, origin=(0.0, 0.0, 100.0), label="Skel"):
    """Spawn n SkeletalMeshActors with the given skeletal mesh, in a grid."""
    mesh = unreal.load_asset(mesh_path)
    if not mesh:
        return f"ERR: skeletal mesh not found: {mesh_path}"
    eas = _eas()
    cols = max(1, int(n ** 0.5))
    made = 0
    for i in range(int(n)):
        r, c = divmod(i, cols)
        loc = unreal.Vector(origin[0] + c * spacing, origin[1] + r * spacing, origin[2])
        a = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, loc, unreal.Rotator(0, 0, 0))
        if not a:
            continue
        a.skeletal_mesh_component.set_skeletal_mesh_asset(mesh)
        a.set_actor_label(f"{label}_{i + 1}")
        made += 1
    return f"spawned {made}/{n} skeletal mesh actor(s)"


@_guard
def scale_actor(label, s=1.5):
    """Scale an actor (by label) uniformly; reads the new scale back."""
    a = _find(label)
    if not a:
        return f"ERR: no actor labeled '{label}'"
    a.set_actor_scale3d(unreal.Vector(s, s, s))
    v = a.get_actor_scale3d()
    return f"{label} scale -> ({v.x:.2f},{v.y:.2f},{v.z:.2f})"


@_guard
def move_actor(label, x, y, z):
    """Move an actor (by label) to a world location; reads it back."""
    a = _find(label)
    if not a:
        return f"ERR: no actor labeled '{label}'"
    a.set_actor_location(unreal.Vector(x, y, z), False, True)
    p = a.get_actor_location()
    return f"{label} at ({p.x:.0f},{p.y:.0f},{p.z:.0f})"


@_guard
def clear(prefix):
    """Destroy every actor whose label starts with `prefix` — undo a spawn cleanly."""
    eas = _eas()
    killed = 0
    for a in list(eas.get_all_level_actors()):
        if a and a.get_actor_label().startswith(prefix):
            eas.destroy_actor(a)
            killed += 1
    return f"destroyed {killed} actor(s) with prefix '{prefix}'"


# ── look / persist / level ───────────────────────────────────────────────────
@_guard
def screenshot(name="bridge_view", width=1600, height=900):
    """Queue a high-res viewport screenshot; returns where it lands (Saved/Screenshots)."""
    fname = name if name.lower().endswith(".png") else name + ".png"
    unreal.AutomationLibrary.take_high_res_screenshot(width, height, fname)
    shots = os.path.join(unreal.Paths.project_saved_dir(), "Screenshots")
    return f"screenshot queued -> {os.path.normpath(shots)}\\...\\{fname} (appears next tick)"


@_guard
def save_all():
    """Save every dirty package."""
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    return "saved dirty packages"


@_guard
def open_level(path):
    """Open a level/map by content path (e.g. /Game/Maps/Main)."""
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(path)
    return f"opened level {path}"


@_guard
def datatable_rows(asset_path, limit=30):
    """List the row names of a DataTable asset."""
    dt = unreal.load_asset(asset_path)
    if not isinstance(dt, unreal.DataTable):
        return f"ERR: {asset_path} is not a DataTable"
    names = [str(x) for x in unreal.DataTableFunctionLibrary.get_data_table_row_names(dt)][:limit]
    return f"{len(names)} row(s):\n" + ", ".join(names)


@_guard
def toast(msg):
    """On-screen + log message in the editor."""
    unreal.SystemLibrary.print_string(_world(), str(msg), True, True, unreal.LinearColor(0.9, 0.82, 0.6), 6.0)
    return f"toast: {msg}"
