from __future__ import annotations

import os
import pathlib
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import kiwi_codegen as codegen


ROOT = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT = ROOT.parent

def _resolve_app_asset(relative_path: str) -> pathlib.Path:
    """Resolve a GUI asset for source execution and PyInstaller bundles."""
    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        bundled = pathlib.Path(meipass) / relative_path
        if bundled.exists():
            return bundled

    return PROJECT_ROOT / relative_path


class App(tk.Tk):
    """Tk frontend for the shared KIWI code-generation core."""

    def __init__(self) -> None:
        super().__init__()
        self.title("kiwi")
        self._set_window_icon()
        self.geometry("820x820")
        self.minsize(780, 720)
        self.configure(bg="#101826")

        self.style = ttk.Style(self)
        self.style.theme_use("clam")
        self._configure_styles()

        self.prefix_var = tk.StringVar(value=codegen.DEFAULT_MODULE_PREFIX)
        self.dest_var = tk.StringVar(value=str(PROJECT_ROOT / "generated"))
        self.port_var = tk.StringVar(value=codegen.DEFAULT_PORT)

        self.api_vars = {
            name: tk.BooleanVar(value=name in codegen.DEFAULT_APIS)
            for name in codegen.SUPPORTED_APIS
        }

        # Preserve the already-present future API placeholders. They remain
        # deliberately disconnected from generation in this architecture step.
        self.planned_api_vars = {
            "event_group": tk.BooleanVar(value=False),
            "software_timer": tk.BooleanVar(value=False),
            "stream_buffer": tk.BooleanVar(value=False),
        }

        self.split_into_port_dir_var = tk.BooleanVar(
            value=codegen.DEFAULT_SPLIT_INTO_PORT_DIR
        )
        self.split_src_inc_files_var = tk.BooleanVar(
            value=codegen.DEFAULT_SPLIT_SRC_INC_FILES
        )

        self._build_ui()

    def _set_window_icon(self) -> None:
        try:
            icon_png = _resolve_app_asset("doc/kiwi_window.png")
            if icon_png.exists():
                self._window_icon = tk.PhotoImage(file=str(icon_png))
                self.iconphoto(True, self._window_icon)
        except tk.TclError:
            pass

        if sys.platform == "win32":
            try:
                icon_ico = _resolve_app_asset("doc/kiwi.ico")
                if icon_ico.exists():
                    self.wm_iconbitmap(str(icon_ico))
            except tk.TclError:
                pass

    def _configure_styles(self) -> None:
        self.style.configure("Card.TFrame", background="#182235")
        self.style.configure(
            "Title.TLabel",
            background="#182235",
            foreground="#f3f6ff",
            font=("Segoe UI", 19, "bold"),
        )
        self.style.configure(
            "Hint.TLabel",
            background="#182235",
            foreground="#b6c4dc",
            font=("Segoe UI", 10),
        )
        self.style.configure(
            "Body.TLabel",
            background="#182235",
            foreground="#e7eeff",
            font=("Segoe UI", 11),
        )
        self.style.configure("Action.TButton", font=("Segoe UI", 10), padding=8)
        self.style.configure(
            "TCheckbutton",
            background="#182235",
            foreground="#e7eeff",
            font=("Segoe UI", 10),
        )
        self.style.map(
            "TCheckbutton",
            background=[("active", "#26344d"), ("selected", "#182235")],
            foreground=[("active", "#ffffff"), ("selected", "#e7eeff")],
        )

    def _build_ui(self) -> None:
        outer = ttk.Frame(self, style="Card.TFrame", padding=20)
        outer.pack(fill="both", expand=True, padx=20, pady=20)

        header = ttk.Frame(outer, style="Card.TFrame")
        header.pack(fill="x")
        self._add_header_logo(header)

        header_text = ttk.Frame(header, style="Card.TFrame")
        header_text.pack(side="left", fill="x", expand=True, padx=(14, 0))
        ttk.Label(
            header_text,
            text="KIWI OSAL Generator for Embedded Projects",
            style="Title.TLabel",
        ).pack(anchor="w")
        ttk.Label(
            header_text,
            text="UI frontend for the shared KIWI code-generation core.",
            style="Hint.TLabel",
        ).pack(anchor="w", pady=(4, 0))

        settings_card = ttk.Frame(outer, style="Card.TFrame", padding=(0, 20, 0, 8))
        settings_card.pack(fill="x")
        ttk.Label(settings_card, text="Paths settings", style="Body.TLabel").pack(anchor="w")

        form = ttk.Frame(settings_card, style="Card.TFrame")
        form.pack(fill="x", pady=(10, 0))

        ttk.Label(form, text="Port", style="Body.TLabel").grid(
            row=0, column=0, sticky="w", pady=(0, 8)
        )
        port_combo = ttk.Combobox(
            form, textvariable=self.port_var, state="readonly", width=38
        )
        port_combo["values"] = codegen.SUPPORTED_PORTS
        port_combo.grid(row=0, column=1, sticky="ew", padx=(10, 0), pady=(0, 8))

        ttk.Label(form, text="Module Prefix", style="Body.TLabel").grid(
            row=1, column=0, sticky="w", pady=(0, 8)
        )
        ttk.Entry(form, textvariable=self.prefix_var, width=40).grid(
            row=1, column=1, sticky="ew", padx=(10, 0), pady=(0, 8)
        )

        ttk.Label(form, text="Output Folder", style="Body.TLabel").grid(
            row=2, column=0, sticky="w", pady=(0, 8)
        )
        path_frame = ttk.Frame(form, style="Card.TFrame")
        path_frame.grid(row=2, column=1, sticky="ew", padx=(10, 0), pady=(0, 8))
        ttk.Entry(path_frame, textvariable=self.dest_var).pack(
            side="left", fill="x", expand=True
        )
        ttk.Button(
            path_frame,
            text="Browse",
            style="Action.TButton",
            command=self._select_destination,
        ).pack(side="left", padx=(8, 0))
        form.columnconfigure(1, weight=1)

        layout_checks = ttk.Frame(form, style="Card.TFrame")
        layout_checks.grid(row=3, column=0, columnspan=2, sticky="w", pady=(4, 0))
        ttk.Checkbutton(
            layout_checks,
            text="Split into port directory",
            variable=self.split_into_port_dir_var,
        ).pack(side="left", padx=(0, 18))
        ttk.Checkbutton(
            layout_checks,
            text="Split source/include files",
            variable=self.split_src_inc_files_var,
        ).pack(side="left")

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
            ttk.Checkbutton(
                checks, text=labels[key], variable=self.api_vars[key]
            ).grid(row=0, column=idx, padx=(0, 12), sticky="w")

        planned_labels = {
            "event_group": "Event Groups",
            "software_timer": "Software Timers",
            "stream_buffer": "Stream Buffers",
        }
        for idx, key in enumerate(planned_labels):
            ttk.Checkbutton(
                checks, text=planned_labels[key], variable=self.planned_api_vars[key]
            ).grid(row=1, column=idx, padx=(0, 12), pady=(8, 0), sticky="w")


        actions = ttk.Frame(outer, style="Card.TFrame")
        actions.pack(fill="x")
        action_buttons = ttk.Frame(actions, style="Card.TFrame")
        action_buttons.pack(side="left")

        ttk.Button(
            action_buttons,
            text="Generate",
            style="Action.TButton",
            command=self.generate,
        ).pack(side="left")
        ttk.Button(
            action_buttons,
            text="Load Profile...",
            style="Action.TButton",
            command=self._load_profile,
        ).pack(side="left", padx=(10, 0))
        ttk.Button(
            action_buttons,
            text="Save Profile...",
            style="Action.TButton",
            command=self._save_profile,
        ).pack(side="left", padx=(10, 0))
        ttk.Button(
            action_buttons,
            text="Open Output Folder",
            style="Action.TButton",
            command=self._open_output,
        ).pack(side="left", padx=(10, 0))

        ttk.Label(outer, text="Generator log", style="Body.TLabel").pack(anchor="w", pady=(18, 6))

        self.log = tk.Text(
            outer,
            height=10,
            bg="#0d1422",
            fg="#d9e5ff",
            insertbackground="#d9e5ff",
            relief="flat",
        )
        self.log.pack(fill="both", expand=True, pady=(18, 0))
        self._log("Ready. Configure options, load a profile, or click Generate.")

    def _log(self, text: str) -> None:
        self.log.insert("end", text + "\n")
        self.log.see("end")

    def _select_destination(self) -> None:
        folder = filedialog.askdirectory(
            initialdir=self.dest_var.get() or str(PROJECT_ROOT)
        )
        if folder:
            self.dest_var.set(folder)

    def _open_output(self) -> None:
        output = pathlib.Path(self.dest_var.get())
        if not output.exists():
            messagebox.showinfo("Info", "Output folder does not exist yet.")
            return

        try:
            if sys.platform == "win32":
                os.startfile(str(output))  # type: ignore[attr-defined]
            elif sys.platform == "darwin":
                os.system(f'open "{output}"')
            else:
                os.system(f'xdg-open "{output}" >/dev/null 2>&1 &')
        except Exception:
            messagebox.showinfo("Info", f"Output: {output}")

    def _add_header_logo(self, parent: ttk.Frame) -> None:
        try:
            icon_png = _resolve_app_asset("doc/kiwi_header.png")
            if not icon_png.exists():
                return

            self._header_logo = tk.PhotoImage(file=str(icon_png))
            tk.Label(
                parent,
                image=self._header_logo,
                bg="#182235",
                bd=0,
                highlightthickness=0,
            ).pack(side="left", anchor="nw")
        except tk.TclError:
            pass

    def _current_config(self) -> codegen.GenerationConfig:
        selected = {
            name for name, variable in self.api_vars.items() if variable.get()
        }
        return codegen.make_generation_config(
            module_prefix=self.prefix_var.get().strip(),
            port=self.port_var.get().strip(),
            apis=selected,
            split_into_port_dir=self.split_into_port_dir_var.get(),
            split_src_inc_files=self.split_src_inc_files_var.get(),
        )

    def _load_profile(self) -> None:
        profile = filedialog.askopenfilename(
            title="Load KIWI code-generation profile",
            initialdir=self.dest_var.get() or str(PROJECT_ROOT),
            filetypes=(
                ("YAML profile", "*.yaml *.yml"),
                ("All files", "*.*"),
            ),
        )
        if not profile:
            return

        try:
            config = codegen.load_profile(profile)
        except (codegen.CodegenError, OSError) as exc:
            messagebox.showerror("Load profile failed", str(exc))
            self._log(f"ERROR: {exc}")
            return

        self.prefix_var.set(config.module_prefix)
        self.port_var.set(config.port)
        for name, variable in self.api_vars.items():
            variable.set(name in config.apis)
        self.split_into_port_dir_var.set(config.split_into_port_dir)
        self.split_src_inc_files_var.set(config.split_src_inc_files)
        self._log(f"Loaded profile: {profile}")

    def _save_profile(self) -> None:
        try:
            config = self._current_config()
            forms = codegen.build_prefix_forms(config.module_prefix)
        except codegen.CodegenError as exc:
            messagebox.showerror("Invalid configuration", str(exc))
            return

        default_name = f"{forms.snake}_kiwi_profile.yaml"
        profile = filedialog.asksaveasfilename(
            title="Save KIWI code-generation profile",
            initialdir=self.dest_var.get() or str(PROJECT_ROOT),
            initialfile=default_name,
            defaultextension=".yaml",
            filetypes=(
                ("YAML profile", "*.yaml"),
                ("YAML profile", "*.yml"),
                ("All files", "*.*"),
            ),
        )
        if not profile:
            return

        try:
            saved = codegen.save_profile(profile, config)
        except OSError as exc:
            messagebox.showerror("Save profile failed", str(exc))
            self._log(f"ERROR: {exc}")
            return

        self._log(f"Saved profile: {saved}")

    def generate(self) -> None:
        try:
            config = self._current_config()
            output_root = pathlib.Path(self.dest_var.get().strip())
            codegen.generate(config, output_root, log_callback=self._log)
        except (codegen.CodegenError, OSError) as exc:
            messagebox.showerror("Generation failed", str(exc))
            self._log(f"ERROR: {exc}")
            return
        except Exception as exc:
            messagebox.showerror("Generation failed", str(exc))
            self._log(f"ERROR: {exc}")
            return

        messagebox.showinfo("Success", f"Code generated into: {output_root}")
        self._log("Done.")


if __name__ == "__main__":
    App().mainloop()
