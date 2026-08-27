from __future__ import annotations

import pathlib
import re
import shutil
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from dataclasses import dataclass


# The generator lives in <project>/generator. Keep both paths explicit because
# source execution and a PyInstaller bundle resolve resources differently.
ROOT = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT = ROOT.parent


def resolve_project_asset(relative_path: str) -> pathlib.Path:
    """Resolve a project asset for both source run and PyInstaller bundle."""
    # In one-file PyInstaller builds bundled data is unpacked into _MEIPASS.
    # During normal source execution the same resource is taken from the
    # repository root, so the rest of the UI code does not care how it runs.
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        bundled = pathlib.Path(meipass) / relative_path
        if bundled.exists():
            return bundled

    return PROJECT_ROOT / relative_path


def resolve_templates_dir() -> pathlib.Path:
    """
    Resolve path to OSAL templates for both source run and PyInstaller .exe.
    """
    # Keep template discovery independent from the current working directory.
    # This is important because the packaged executable can be launched from
    # anywhere, while a developer may also run the script directly.
    candidates: list[pathlib.Path] = []

    # PyInstaller onefile/onedir unpack directory.
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        candidates.append(pathlib.Path(meipass) / "osal")

    # Script location and current working directory.
    candidates.append(PROJECT_ROOT / "osal")
    candidates.append(pathlib.Path.cwd() / "osal")

    for path in candidates:
        if path.exists() and path.is_dir():
            return path

    searched = "\n".join(f" - {p}" for p in candidates)
    raise FileNotFoundError(f"osal directory not found. Checked:\n{searched}")


# A single user-entered module prefix is expanded into the naming conventions
# used by C macros, file names, types and function symbols. Keeping these forms
# together makes the replacement stage deterministic and easy to audit.
@dataclass(frozen=True)
class PrefixForms:
    upper: str
    camel: str
    pascal: str
    snake: str


def _to_words(value: str) -> list[str]:
    # Normalize spaces, punctuation, snake_case and camelCase into one sequence
    # of words before generating the required identifier variants.
    cleaned = re.sub(r"[^A-Za-z0-9]+", "_", value.strip())
    cleaned = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", cleaned)
    return [w for w in cleaned.split("_") if w]


def build_prefix_forms(prefix: str) -> PrefixForms:
    # Derive every spelling of the module prefix once. The same forms are then
    # reused for all templates, which avoids ad-hoc transformations per file.
    words = _to_words(prefix)
    if not words:
        raise ValueError("Prefix cannot be empty.")

    lower_words = [w.lower() for w in words]
    upper = "_".join(w.upper() for w in words)
    pascal = "".join(w.capitalize() for w in lower_words)
    camel = lower_words[0] + "".join(w.capitalize() for w in lower_words[1:])
    snake = "_".join(lower_words)
    return PrefixForms(upper=upper, camel=camel, pascal=pascal, snake=snake)


def apply_prefix(content: str, forms: PrefixForms) -> str:
    # Replace the generic template prefix in all naming styles used by the
    # source templates. The legacy SYS_PARAM_SRV aliases are intentionally
    # handled here as part of the same normalization pass.
    mapping = {
        "TEMPLATE": forms.upper,
        "Template": forms.pascal,
        "template": forms.snake,
        "SYS_PARAM_SRV": forms.upper,
        "SysParamSrv": forms.pascal,
        "sysParamSrv": forms.camel,
        "sys_param_srv": forms.snake,
    }
    for src, dst in mapping.items():
        content = content.replace(src, dst)

    # Keep camelCase prefix for template symbols that continue in camelCase
    # after `_osal` (functions, vtable/ptable objects, and global variables).
    camel_symbol_prefix_pattern = re.compile(
        rf"\b{re.escape(forms.snake)}_osal(?=[A-Z])"
    )
    content = camel_symbol_prefix_pattern.sub(f"{forms.camel}_osal", content)
    return content


def apply_api_profile_markers(content: str, selected_apis: set[str]) -> str:
    # Template sections are delimited by BEGIN/END markers. Enabled API groups
    # keep their body while disabled groups are removed completely, producing a
    # component-specific OSAL instead of a monolithic all-purpose interface.
    marker_to_api = {
        "QUEUE": "queue",
        "LOCK": "lock",
        "THREAD": "thread",
        "MEMORY": "memory",
        "TIME": "time",
    }

    for marker, api_name in marker_to_api.items():
        pattern = re.compile(
            rf"(?ms)^[ \t]*//[ \t]*BEGIN[ \t]+{marker}[ \t]*\n"
            rf"(.*?)"
            rf"^[ \t]*//[ \t]*END[ \t]+{marker}[ \t]*\n?"
        )
        if api_name in selected_apis:
            content = pattern.sub(r"\1", content)
        else:
            content = pattern.sub("", content)

    return content


def render_profile_header(forms: PrefixForms, selected_apis: set[str], port: str) -> str:
    # Emit a compact machine-readable description of the generated profile.
    # The header records the selected port and API groups for use by C code and
    # build-time checks without having to infer them from generated sources.
    upper = forms.upper
    guard = f"{upper}_OSAL_PROFILE_H_"

    flags = {
        "queue": "QUEUE",
        "lock": "LOCK",
        "thread": "THREAD",
        "time": "TIME",
        "memory": "MEMORY",
    }

    lines = [
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "/* Auto-generated by OSAL Code Generator. */",
        f"#define {upper}_OSAL_PORT_{port.upper()}    1",
        "",
    ]

    for key, name in flags.items():
        val = 1 if key in selected_apis else 0
        lines.append(f"#define {upper}_OSAL_USE_{name}    {val}")

    lines.extend(["", f"#endif /* {guard} */", ""])
    return "\n".join(lines)


class App(tk.Tk):
    def __init__(self) -> None:
        # Window-level configuration is kept separate from widget construction
        # so styling, state defaults and layout remain easy to modify independently.
        super().__init__()
        self.title("kiwi")
        self._set_window_icon()
        self.geometry("820x760")
        self.minsize(780, 680)
        self.configure(bg="#101826")

        self.style = ttk.Style(self)
        self.style.theme_use("clam")
        self._configure_styles()

        # UI state. These variables are the single source of truth consumed by
        # the generation pipeline when the user presses Generate.
        self.prefix_var = tk.StringVar(value="foo_module")
        self.dest_var = tk.StringVar(value=str(PROJECT_ROOT / "generated"))
        self.port_var = tk.StringVar(value="FreeRTOS")

        # Implemented API groups directly correspond to removable template sections.
        self.api_vars = {
            "queue": tk.BooleanVar(value=True),
            "lock": tk.BooleanVar(value=True),
            "thread": tk.BooleanVar(value=True),
            "time": tk.BooleanVar(value=True),
            "memory": tk.BooleanVar(value=True),
        }

        # Planned primitives are exposed in the UI now so the intended API shape
        # is visible, but they are deliberately not connected to generation yet.
        self.planned_api_vars = {
            "event_group": tk.BooleanVar(value=False),
            "software_timer": tk.BooleanVar(value=False),
            "stream_buffer": tk.BooleanVar(value=False),
        }

        self._build_ui()

    def _set_window_icon(self) -> None:
        """Apply the KIWI icon to the Tk window and Windows title bar."""
        # Use a dedicated pre-rendered 32x32 PNG instead of runtime subsampling.
        # This keeps small title-bar rendering sharp and avoids Tk's coarse scaler.
        try:
            icon_png = resolve_project_asset("doc/kiwi_window.png")
            if icon_png.exists():
                self._window_icon = tk.PhotoImage(file=str(icon_png))
                self.iconphoto(True, self._window_icon)
        except tk.TclError:
            pass

        # On Windows explicitly apply the multi-resolution ICO as well. The EXE
        # carries the same ICO through PyInstaller, so source and packaged runs
        # use a consistent icon in the title bar and shell.
        if sys.platform == "win32":
            try:
                icon_ico = resolve_project_asset("doc/kiwi.ico")
                if icon_ico.exists():
                    self.wm_iconbitmap(str(icon_ico))
            except tk.TclError:
                pass

    def _configure_styles(self) -> None:
        # Centralize theme colors and widget states. In particular, checkbutton
        # hover colors are overridden to remain readable on the dark background.
        self.style.configure("Card.TFrame", background="#182235")
        self.style.configure("Title.TLabel", background="#182235", foreground="#f3f6ff", font=("Segoe UI", 19, "bold"))
        self.style.configure("Hint.TLabel", background="#182235", foreground="#b6c4dc", font=("Segoe UI", 10))
        self.style.configure("Body.TLabel", background="#182235", foreground="#e7eeff", font=("Segoe UI", 11))
        self.style.configure("Action.TButton", font=("Segoe UI", 10), padding=8)
        self.style.configure("TCheckbutton", background="#182235", foreground="#e7eeff", font=("Segoe UI", 10))
        self.style.map(
            "TCheckbutton",
            background=[("active", "#26344d"), ("selected", "#182235")],
            foreground=[("active", "#ffffff"), ("selected", "#e7eeff")],
        )

    def _build_ui(self) -> None:
        # Main card: all controls live on one consistent dark surface.
        outer = ttk.Frame(self, style="Card.TFrame", padding=20)
        outer.pack(fill="both", expand=True, padx=20, pady=20)

        # Header block combines the KIWI mark with the tool name and purpose.
        # The logo sits on a slightly contrasting badge so it stays visually
        # separated from the dark card background without feeling disconnected.
        header = ttk.Frame(outer, style="Card.TFrame")
        header.pack(fill="x")

        self._add_header_logo(header)

        header_text = ttk.Frame(header, style="Card.TFrame")
        header_text.pack(side="left", fill="x", expand=True, padx=(14, 0))
        ttk.Label(header_text, text="KIWI OSAL Generator for Embedded Projects", style="Title.TLabel").pack(anchor="w")
        ttk.Label(
            header_text,
            text="Generate C source/header files from templates with custom prefix and API profile.",
            style="Hint.TLabel",
        ).pack(anchor="w", pady=(4, 0))

        # Core generation parameters: module prefix, target OS port and output path.
        form = ttk.Frame(outer, style="Card.TFrame")
        form.pack(fill="x", pady=(20, 0))

        ttk.Label(form, text="Module Prefix", style="Body.TLabel").grid(row=0, column=0, sticky="w", pady=(0, 8))
        prefix_entry = ttk.Entry(form, textvariable=self.prefix_var, width=40)
        prefix_entry.grid(row=0, column=1, sticky="ew", padx=(10, 0), pady=(0, 8))

        ttk.Label(form, text="Port", style="Body.TLabel").grid(row=1, column=0, sticky="w", pady=(0, 8))
        port_combo = ttk.Combobox(form, textvariable=self.port_var, state="readonly", width=38)
        port_combo["values"] = ("FreeRTOS", "POSIX", "CMSIS RTOS v2")
        port_combo.current(0)
        port_combo.grid(row=1, column=1, sticky="ew", padx=(10, 0), pady=(0, 8))

        ttk.Label(form, text="Output Folder", style="Body.TLabel").grid(row=2, column=0, sticky="w")
        path_frame = ttk.Frame(form, style="Card.TFrame")
        path_frame.grid(row=2, column=1, sticky="ew", padx=(10, 0))
        ttk.Entry(path_frame, textvariable=self.dest_var).pack(side="left", fill="x", expand=True)
        ttk.Button(path_frame, text="Browse", style="Action.TButton", command=self._select_destination).pack(side="left", padx=(8, 0))

        form.columnconfigure(1, weight=1)

        # API profile selector. Each checkbox controls one marked template group.
        api_card = ttk.Frame(outer, style="Card.TFrame", padding=(0, 18, 0, 8))
        api_card.pack(fill="x")
        ttk.Label(api_card, text="API Set", style="Body.TLabel").pack(anchor="w")

        checks = ttk.Frame(api_card, style="Card.TFrame")
        checks.pack(anchor="w", pady=(8, 0))
        labels = {
            "queue": "Queues",
            "lock": "Locks",
            "thread": "Threads",
            "time": "Time",
            "memory": "Memory",
        }
        for idx, key in enumerate(labels):
            ttk.Checkbutton(checks, text=labels[key], variable=self.api_vars[key]).grid(
                row=0, column=idx, padx=(0, 12), sticky="w"
            )

        planned_labels = {
            "event_group": "Event Groups",
            "software_timer": "Software Timers",
            "stream_buffer": "Stream Buffers",
        }
        for idx, key in enumerate(planned_labels):
            ttk.Checkbutton(checks, text=planned_labels[key], variable=self.planned_api_vars[key]).grid(
                row=1, column=idx, padx=(0, 12), pady=(8, 0), sticky="w"
            )

        # Keep the current implementation status visible without enabling ports
        # that are present in the UI but not implemented by the generator yet.
        tips = (
            "• FreeRTOS generation is available now; POSIX and CMSIS RTOS v2 are reserved for future ports.\n"
            "• Event Groups, Software Timers and Stream Buffers are UI placeholders for upcoming API support.\n"
            "• Disable APIs you don't need (e.g., only locks).\n"
            "• The tool rewrites module prefix in symbols and file names."
        )
        ttk.Label(outer, text=tips, style="Hint.TLabel", justify="left").pack(anchor="w", pady=(12, 20))

        # Primary actions. The bottom decorative logo has been removed to keep
        # the layout cleaner and concentrate branding in the header area.
        actions = ttk.Frame(outer, style="Card.TFrame")
        actions.pack(fill="x")

        action_buttons = ttk.Frame(actions, style="Card.TFrame")
        action_buttons.pack(side="left")
        ttk.Button(action_buttons, text="Generate", style="Action.TButton", command=self.generate).pack(side="left")
        ttk.Button(action_buttons, text="Open Output Folder", style="Action.TButton", command=self._open_output).pack(side="left", padx=(10, 0))


        # The console is intentionally simple: generation progress and failures
        # are appended here so the user can immediately see what files were made.
        self.log = tk.Text(outer, height=12, bg="#0d1422", fg="#d9e5ff", insertbackground="#d9e5ff", relief="flat")
        self.log.pack(fill="both", expand=True, pady=(20, 0))
        self._log("Ready. Configure options and click Generate.")

    def _log(self, text: str) -> None:
        # Always keep the newest status line visible.
        self.log.insert("end", text + "\n")
        self.log.see("end")

    def _select_destination(self) -> None:
        # Let Tk choose a directory, then mirror the result into the bound entry.
        folder = filedialog.askdirectory(initialdir=self.dest_var.get() or str(ROOT))
        if folder:
            self.dest_var.set(folder)

    def _open_output(self) -> None:
        # Open the generated output using the platform shell when available.
        # A message fallback keeps the command harmless on unsupported systems.
        output = pathlib.Path(self.dest_var.get())
        if not output.exists():
            messagebox.showinfo("Info", "Output folder does not exist yet.")
            return
        try:
            if hasattr(pathlib, "WindowsPath"):
                import os
                os.startfile(str(output))  # type: ignore[attr-defined]
        except Exception:
            messagebox.showinfo("Info", f"Output: {output}")

    def _add_header_logo(self, parent: ttk.Frame) -> None:
        """Render the prominent high-quality KIWI logo in the header."""
        # Use the larger pre-rendered asset directly, without an extra outer
        # badge, so only the icon itself remains visually prominent.
        try:
            icon_png = resolve_project_asset("doc/kiwi_header.png")
            if not icon_png.exists():
                return

            self._header_logo = tk.PhotoImage(file=str(icon_png))
            logo_label = tk.Label(
                parent,
                image=self._header_logo,
                bg="#182235",
                bd=0,
                highlightthickness=0,
            )
            logo_label.pack(side="left", anchor="nw")
        except tk.TclError:
            pass

    def generate(self) -> None:
        # Snapshot all UI selections first. Generation below works only with these
        # local values, making one button press a deterministic transformation.
        prefix_raw = self.prefix_var.get().strip()
        port_raw = self.port_var.get().strip().lower()
        output_root = pathlib.Path(self.dest_var.get().strip())

        selected = {name for name, var in self.api_vars.items() if var.get()}

        # The UI already reserves names for future ports, but only FreeRTOS has
        # templates and generation logic at this stage. Reject unsupported ports
        # before any output directory or file is touched.
        if port_raw != "freertos":
            messagebox.showerror("Error", "Currently only FreeRTOS port generation is implemented.")
            return

        # Validate and canonicalize the user prefix before applying it to files.
        try:
            forms = build_prefix_forms(prefix_raw)
        except ValueError as exc:
            messagebox.showerror("Error", str(exc))
            return

        # Resolve templates only after basic input validation. This supports both
        # repository execution and the temporary directory used by PyInstaller.
        try:
            templates_dir = resolve_templates_dir()
        except FileNotFoundError as exc:
            messagebox.showerror("Error", str(exc))
            return

        # Generation pipeline:
        #   1. create the deterministic output tree;
        #   2. load each source template;
        #   3. remove disabled API groups;
        #   4. rewrite the generic prefix;
        #   5. write the generated file and profile header.
        try:
            module_dir = output_root / forms.snake
            portable_dir = module_dir / "portable" / "freertos"
            portable_dir.mkdir(parents=True, exist_ok=True)

            # Keep the generated layout parallel to the repository template tree.
            files = [
                templates_dir / "CMakeLists.txt",
                templates_dir / "template_osal.h",
                templates_dir / "template_osal.c",
                templates_dir / "portable" / "freertos" / "CMakeLists.txt",
                templates_dir / "portable" / "freertos" / "template_osal_freertos.h",
                templates_dir / "portable" / "freertos" / "template_osal_freertos.c",
            ]

            # Apply the same transformation sequence to every template so output
            # depends only on the selected profile, port and module prefix.
            for src in files:
                rel = src.relative_to(templates_dir)
                text = src.read_text(encoding="utf-8")
                text = apply_api_profile_markers(text, selected)
                text = apply_prefix(text, forms)
                dst_rel = str(rel).replace("template", forms.snake)
                dst = module_dir / dst_rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_text(text, encoding="utf-8")
                self._log(f"Generated: {dst}")

            # The profile header is generated from configuration rather than copied
            # from a template because its feature flags are entirely data-driven.
            profile_name = f"{forms.snake}_osal_profile.h"
            profile_path = module_dir / profile_name
            profile_path.write_text(render_profile_header(forms, selected, port="freertos"), encoding="utf-8")
            self._log(f"Generated: {profile_path}")

            # Copy project documentation alongside generated output when available.
            # This does not modify README.md in the repository itself.
            shutil.copy2(PROJECT_ROOT / "README.md", output_root / "README_generator.md") if (PROJECT_ROOT / "README.md").exists() else None

        except Exception as exc:
            messagebox.showerror("Generation failed", str(exc))
            self._log(f"ERROR: {exc}")
            return

        messagebox.showinfo("Success", f"Code generated into: {output_root}")
        self._log("Done.")


# Start the Tk event loop only when this file is executed as the application.
# Importing helper functions for tests or tooling therefore has no UI side effects.
if __name__ == "__main__":
    App().mainloop()
