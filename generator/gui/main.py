"""Graphical frontend for the shared kiwicgen generation pipeline."""

from __future__ import annotations

import os
import pathlib
import queue
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

if __package__ in {None, ""}:
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

from core.errors import KiwicgenError
from core.generator import generate
from core.logging import AsyncLogDispatcher
from core.model import (
    DEFAULT_APIS,
    DEFAULT_FORMAT_GENERATED_CODE,
    DEFAULT_LANGUAGE,
    DEFAULT_MODULE_PREFIX,
    DEFAULT_PORTS,
    DEFAULT_SPLIT_INTO_PORT_DIR,
    DEFAULT_SPLIT_SRC_INC_FILES,
    IMPLEMENTED_LANGUAGES,
    IMPLEMENTED_PORTS,
    SUPPORTED_APIS,
    SUPPORTED_LANGUAGES,
    SUPPORTED_PORTS,
    GenerationConfig,
)
from core.paths import resolve_app_asset, runtime_root
from core.profile import load_profile, save_profile
from core.validation import (
    build_prefix_forms,
    make_generation_config,
    validate_generation_support,
)


class KiwicgenApp(tk.Tk):
    """Tk frontend for the shared kiwicgen generation core."""

    def __init__(self) -> None:
        super().__init__()

        self.title("kiwicgen-gui")
        self._set_window_icon()
        self.geometry("1120x780")
        self.minsize(960, 680)
        self.configure(bg="#101826")

        self.style = ttk.Style(self)
        self.style.theme_use("clam")
        self._configure_styles()

        self._gui_log_queue: queue.Queue[str] = queue.Queue()
        self._generation_result_queue: queue.Queue[
            tuple[bool, pathlib.Path, str | None]
        ] = queue.Queue()
        self._log_dispatcher = AsyncLogDispatcher(self._gui_log_queue.put)
        self._generation_thread: threading.Thread | None = None
        self._generation_active = False
        self._closing = False
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self.prefix_var = tk.StringVar(value=DEFAULT_MODULE_PREFIX)
        self.dest_var = tk.StringVar(value=str(runtime_root() / "generated"))
        self.port_vars = {
            port: tk.BooleanVar(value=port in DEFAULT_PORTS)
            for port in SUPPORTED_PORTS
        }
        self.language_var = tk.StringVar(value=DEFAULT_LANGUAGE)
        self.api_vars = {
            name: tk.BooleanVar(value=name in DEFAULT_APIS)
            for name in SUPPORTED_APIS
        }
        self.split_into_port_dir_var = tk.BooleanVar(
            value=DEFAULT_SPLIT_INTO_PORT_DIR
        )
        self.split_src_inc_files_var = tk.BooleanVar(
            value=DEFAULT_SPLIT_SRC_INC_FILES
        )
        self.format_generated_code_var = tk.BooleanVar(
            value=DEFAULT_FORMAT_GENERATED_CODE
        )

        self._build_ui()
        self.after(50, self._drain_log_queue)
        self.after(50, self._drain_generation_results)

    def _set_window_icon(self) -> None:
        """Apply packaged KIWI artwork when supported by the host window system."""
        try:
            icon_png = resolve_app_asset("doc/kiwi_window.png")
            if icon_png.exists():
                self._window_icon = tk.PhotoImage(file=str(icon_png))
                self.iconphoto(True, self._window_icon)
        except tk.TclError:
            pass

        if sys.platform == "win32":
            try:
                icon_ico = resolve_app_asset("doc/kiwi.ico")
                if icon_ico.exists():
                    self.wm_iconbitmap(str(icon_ico))
            except tk.TclError:
                pass

    def _configure_styles(self) -> None:
        """Configure the application-local Tk visual style."""
        self.style.configure("Card.TFrame", background="#182235")
        self.style.configure(
            "Title.TLabel",
            background="#182235",
            foreground="#f3f6ff",
            font=("Segoe UI", 18, "bold"),
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
        self.style.configure(
            "TRadiobutton",
            background="#182235",
            foreground="#e7eeff",
            font=("Segoe UI", 10),
        )
        self.style.map(
            "TRadiobutton",
            background=[("active", "#26344d"), ("selected", "#182235")],
            foreground=[("active", "#ffffff"), ("selected", "#e7eeff")],
        )

    def _build_ui(self) -> None:
        """Build the complete generator form from shared-core capabilities."""
        outer = ttk.Frame(self, style="Card.TFrame", padding=16)
        outer.pack(fill="both", expand=True, padx=20, pady=20)

        header = ttk.Frame(outer, style="Card.TFrame")
        header.pack(fill="x")
        self._add_header_logo(header)

        header_text = ttk.Frame(header, style="Card.TFrame")
        header_text.pack(side="left", padx=(12, 0), anchor="n")
        ttk.Label(
            header_text,
            text="kiwicgen — KIWI OSAL Generator",
            style="Title.TLabel",
        ).pack(anchor="w")
        ttk.Label(
            header_text,
            text="GUI frontend for the shared kiwicgen generation core.",
            style="Hint.TLabel",
        ).pack(anchor="w", pady=(3, 0))

        settings_card = ttk.Frame(outer, style="Card.TFrame", padding=(0, 10, 0, 6))
        settings_card.pack(fill="x")
        ttk.Label(settings_card, text="Paths settings", style="Body.TLabel").pack(
            anchor="w"
        )

        form = ttk.Frame(settings_card, style="Card.TFrame")
        form.pack(anchor="w", pady=(8, 0))

        ttk.Label(form, text="Ports", style="Body.TLabel").grid(
            row=0, column=0, sticky="w", pady=(0, 7)
        )
        ports_frame = ttk.Frame(form, style="Card.TFrame")
        ports_frame.grid(row=0, column=1, sticky="w", padx=(10, 0), pady=(0, 7))
        for port in SUPPORTED_PORTS:
            text = port if port in IMPLEMENTED_PORTS else f"{port} (planned)"
            ttk.Checkbutton(
                ports_frame,
                text=text,
                variable=self.port_vars[port],
                command=lambda selected_port=port: self._log_boolean_option(
                    f"Port {selected_port}",
                    self.port_vars[selected_port],
                ),
            ).pack(side="left", padx=(0, 18))

        ttk.Label(form, text="Language", style="Body.TLabel").grid(
            row=1, column=0, sticky="w", pady=(0, 7)
        )
        language_frame = ttk.Frame(form, style="Card.TFrame")
        language_frame.grid(row=1, column=1, sticky="w", padx=(10, 0), pady=(0, 7))
        for language in SUPPORTED_LANGUAGES:
            text = language if language in IMPLEMENTED_LANGUAGES else f"{language} (planned)"
            ttk.Radiobutton(
                language_frame,
                text=text,
                variable=self.language_var,
                value=language,
                command=lambda selected_language=language: self._log(
                    f"[INFO] Language selected: {selected_language}"
                ),
            ).pack(side="left", padx=(0, 18))

        ttk.Label(form, text="Module Prefix", style="Body.TLabel").grid(
            row=2, column=0, sticky="w", pady=(0, 7)
        )
        ttk.Entry(form, textvariable=self.prefix_var, width=64).grid(
            row=2, column=1, sticky="w", padx=(10, 0), pady=(0, 7)
        )

        ttk.Label(form, text="Output Folder", style="Body.TLabel").grid(
            row=3, column=0, sticky="w", pady=(0, 7)
        )
        path_frame = ttk.Frame(form, style="Card.TFrame")
        path_frame.grid(row=3, column=1, sticky="w", padx=(10, 0), pady=(0, 7))
        ttk.Entry(path_frame, textvariable=self.dest_var, width=54).pack(side="left")
        ttk.Button(
            path_frame,
            text="Browse",
            style="Action.TButton",
            command=self._select_destination,
        ).pack(side="left", padx=(8, 0))

        layout_checks = ttk.Frame(form, style="Card.TFrame")
        layout_checks.grid(row=4, column=0, columnspan=2, sticky="w", pady=(3, 0))
        ttk.Checkbutton(
            layout_checks,
            text="Split into port directory",
            variable=self.split_into_port_dir_var,
            command=lambda: self._log_boolean_option(
                "Split into port directory",
                self.split_into_port_dir_var,
            ),
        ).pack(side="left", padx=(0, 18))
        ttk.Checkbutton(
            layout_checks,
            text="Split source/include files",
            variable=self.split_src_inc_files_var,
            command=lambda: self._log_boolean_option(
                "Split source/include files",
                self.split_src_inc_files_var,
            ),
        ).pack(side="left", padx=(0, 18))
        ttk.Checkbutton(
            layout_checks,
            text="Format generated code",
            variable=self.format_generated_code_var,
            command=lambda: self._log_boolean_option(
                "Format generated code",
                self.format_generated_code_var,
            ),
        ).pack(side="left")

        api_card = ttk.Frame(outer, style="Card.TFrame", padding=(0, 12, 0, 6))
        api_card.pack(fill="x")
        ttk.Label(api_card, text="API Set", style="Body.TLabel").pack(anchor="w")

        checks = ttk.Frame(api_card, style="Card.TFrame")
        checks.pack(anchor="w", pady=(7, 0))
        labels = (
            ("queue", "Queues"),
            ("stream_buffer", "Stream Buffers"),
            ("mutex", "Mutexes"),
            ("semaphore", "Counting Semaphores"),
            ("event_flags", "Event Flags"),
            ("thread", "Threads"),
            ("critical_section", "Critical Sections"),
            ("software_timer", "Software Timers"),
            ("time", "Time"),
            ("memory", "Memory"),
        )
        for idx, (key, label) in enumerate(labels):
            row = idx // 5
            column = idx % 5
            ttk.Checkbutton(
                checks,
                text=label,
                variable=self.api_vars[key],
                command=lambda api_key=key, api_label=label: self._log_boolean_option(
                    api_label,
                    self.api_vars[api_key],
                ),
            ).grid(
                row=row,
                column=column,
                padx=(0, 12),
                pady=(0 if row == 0 else 7, 0),
                sticky="w",
            )

        actions = ttk.Frame(outer, style="Card.TFrame")
        actions.pack(fill="x", pady=(2, 0))
        action_buttons = ttk.Frame(actions, style="Card.TFrame")
        action_buttons.pack(side="left")

        self.generate_button = ttk.Button(
            action_buttons,
            text="Generate",
            style="Action.TButton",
            command=self.generate,
        )
        self.generate_button.pack(side="left")
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

        ttk.Label(outer, text="Generator log", style="Body.TLabel").pack(
            anchor="w", pady=(12, 6)
        )
        self.log = tk.Text(
            outer,
            height=10,
            bg="#0d1422",
            fg="#d9e5ff",
            insertbackground="#d9e5ff",
            relief="flat",
        )
        self.log.pack(fill="both", expand=True)
        self.log.tag_configure("info", foreground="#d9e5ff")
        self.log.tag_configure("step", foreground="#6fdcff")
        self.log.tag_configure("ok", foreground="#72e6a0")
        self.log.tag_configure("warn", foreground="#ffd166")
        self.log.tag_configure("err", foreground="#ff7b7b")
        self._log("[INFO] Ready. Configure options, load a profile, or click Generate.")

    def _log(self, text: str) -> None:
        """Queue one structured status line for asynchronous presentation."""
        self._log_dispatcher.log(text)

    def _log_boolean_option(self, label: str, variable: tk.BooleanVar) -> None:
        """Report a user-driven checkbox selection change."""
        state = "selected" if variable.get() else "deselected"
        self._log(f"[INFO] {label}: {state}")

    def _append_log(self, text: str) -> None:
        """Append one status line to the Tk log widget on the main thread."""
        tag = "info"
        if text.startswith("[STEP]"):
            tag = "step"
        elif text.startswith("[ OK ]"):
            tag = "ok"
        elif text.startswith("[WARN]"):
            tag = "warn"
        elif text.startswith("[ERR ]"):
            tag = "err"

        self.log.insert("end", text + "\n", tag)
        self.log.see("end")

    def _drain_log_queue(self) -> None:
        """Move pending listener-thread messages into Tk-owned widgets."""
        while True:
            try:
                message = self._gui_log_queue.get_nowait()
            except queue.Empty:
                break
            self._append_log(message)

        if not self._closing:
            self.after(50, self._drain_log_queue)

    def _flush_logs_to_ui(self) -> None:
        """Synchronize queued messages before showing a modal result dialog."""
        self._log_dispatcher.flush()
        while True:
            try:
                message = self._gui_log_queue.get_nowait()
            except queue.Empty:
                break
            self._append_log(message)

    def _drain_generation_results(self) -> None:
        """Present completed worker results from the Tk-owned main thread."""
        while True:
            try:
                success, output_root, error = self._generation_result_queue.get_nowait()
            except queue.Empty:
                break

            self._generation_active = False
            self.generate_button.state(["!disabled"])
            self._flush_logs_to_ui()

            if self._closing:
                continue

            if success:
                messagebox.showinfo("Success", f"Code generated into: {output_root}")
            else:
                messagebox.showerror(
                    "Generation failed",
                    error or "Unknown generation error.",
                )

        if not self._closing:
            self.after(50, self._drain_generation_results)

    def _on_close(self) -> None:
        """Stop log presentation before destroying the Tk application."""
        self._closing = True
        self._log_dispatcher.close()
        self.destroy()

    def _select_destination(self) -> None:
        """Select the output root without changing generation semantics."""
        folder = filedialog.askdirectory(
            initialdir=self.dest_var.get() or str(runtime_root()),
        )
        if folder:
            self.dest_var.set(folder)

    def _open_output(self) -> None:
        """Open the current output directory with the host platform shell."""
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
        """Attach a compact packaged header image to the GUI."""
        try:
            icon_png = resolve_app_asset("doc/kiwi_header.png")
            if not icon_png.exists():
                return

            source_logo = tk.PhotoImage(file=str(icon_png))
            self._header_logo = source_logo.subsample(2, 2)
            tk.Label(
                parent,
                image=self._header_logo,
                bg="#182235",
                bd=0,
                highlightthickness=0,
            ).pack(side="left", anchor="nw")
        except tk.TclError:
            pass

    def _current_config(self) -> GenerationConfig:
        """Normalize the current GUI state through the shared core contract."""
        selected_apis = {
            name for name, variable in self.api_vars.items() if variable.get()
        }
        selected_ports = [
            port for port, variable in self.port_vars.items() if variable.get()
        ]
        return make_generation_config(
            module_prefix=self.prefix_var.get().strip(),
            ports=selected_ports,
            language=self.language_var.get().strip(),
            apis=selected_apis,
            split_into_port_dir=self.split_into_port_dir_var.get(),
            split_src_inc_files=self.split_src_inc_files_var.get(),
            format_generated_code=self.format_generated_code_var.get(),
        )

    def _load_profile(self) -> None:
        """Load a kiwicgen profile and project it onto the GUI controls."""
        profile = filedialog.askopenfilename(
            title="Load kiwicgen generation profile",
            initialdir=self.dest_var.get() or str(runtime_root()),
            filetypes=(
                ("YAML profile", "*.yaml *.yml"),
                ("All files", "*.*"),
            ),
        )
        if not profile:
            return

        try:
            config = load_profile(profile)
        except (KiwicgenError, OSError) as exc:
            self._log(f"[ERR ] {exc}")
            messagebox.showerror("Load profile failed", str(exc))
            return

        self.prefix_var.set(config.module_prefix)
        for port, variable in self.port_vars.items():
            variable.set(port in config.ports)
        self.language_var.set(config.language)
        for name, variable in self.api_vars.items():
            variable.set(name in config.apis)
        self.split_into_port_dir_var.set(config.split_into_port_dir)
        self.split_src_inc_files_var.set(config.split_src_inc_files)
        self.format_generated_code_var.set(config.format_generated_code)
        self._log(f"[INFO] Loaded profile: {profile}")
        self._log(f"[INFO] Ports selected: {', '.join(config.ports)}")
        self._log(f"[INFO] Language selected: {config.language}")

    def _save_profile(self) -> None:
        """Persist the current normalized GUI configuration as a profile."""
        try:
            config = self._current_config()
            forms = build_prefix_forms(config.module_prefix)
        except KiwicgenError as exc:
            self._log(f"[ERR ] {exc}")
            messagebox.showerror("Invalid configuration", str(exc))
            return

        default_name = f"kiwicgen-{forms.snake}-profile.yaml"
        profile = filedialog.asksaveasfilename(
            title="Save kiwicgen generation profile",
            initialdir=self.dest_var.get() or str(runtime_root()),
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
            saved = save_profile(profile, config)
        except (KiwicgenError, OSError) as exc:
            self._log(f"[ERR ] {exc}")
            messagebox.showerror("Save profile failed", str(exc))
            return

        self._log(f"[INFO] Saved profile: {saved}")

    def generate(self) -> None:
        """Validate GUI state and start one asynchronous generation request."""
        if self._generation_active:
            self._log("[WARN] Generation is already in progress.")
            return

        try:
            config = self._current_config()
            validate_generation_support(config)
            output_root = pathlib.Path(self.dest_var.get().strip())
        except KiwicgenError as exc:
            self._log(f"[ERR ] {exc}")
            self._flush_logs_to_ui()
            messagebox.showerror("Invalid configuration", str(exc))
            return

        self._generation_active = True
        self.generate_button.state(["disabled"])
        self._generation_thread = threading.Thread(
            target=self._generation_worker,
            args=(config, output_root),
            name="kiwicgen-generate",
            daemon=True,
        )
        self._generation_thread.start()

    def _generation_worker(
        self,
        config: GenerationConfig,
        output_root: pathlib.Path,
    ) -> None:
        """Run the shared generation pipeline outside the Tk main thread."""
        try:
            generate(config, output_root, log_callback=self._log)
        except (KiwicgenError, OSError) as exc:
            self._log(f"[ERR ] {exc}")
            self._generation_result_queue.put((False, output_root, str(exc)))
            return
        except Exception as exc:
            self._log(f"[ERR ] {exc}")
            self._generation_result_queue.put((False, output_root, str(exc)))
            return

        self._generation_result_queue.put((True, output_root, None))


def main() -> int:
    """Launch the standalone graphical frontend."""
    KiwicgenApp().mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
