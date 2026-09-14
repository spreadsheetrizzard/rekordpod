#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

"""Identical Tk GUI entry point for the Windows and macOS installers."""

from __future__ import annotations

import queue
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from installer_core import (
    APP_NAME,
    APP_VERSION,
    BuildResult,
    InstallerError,
    InstallResult,
    ValidationResult,
    build_or_reuse_package,
    install_package,
    validate_selected_volume,
)


BACKGROUND = "#050706"
PANEL = "#111713"
PANEL_LIGHT = "#19221c"
TEXT = "#f2f5f3"
MUTED = "#97a39b"
ACCENT = "#52ff8b"
ACCENT_DARK = "#153b23"
ERROR = "#ff5f5f"

TARGET_NAMES = {
    "ipod6g": "iPod Classic 6G/7G",
}


def human_size(value: int) -> str:
    number = float(value)
    for suffix in ("B", "KB", "MB", "GB", "TB"):
        if number < 1024 or suffix == "TB":
            return f"{number:.1f} {suffix}"
        number /= 1024
    return f"{number:.1f} TB"


class RekordpodInstaller:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.events: queue.Queue[tuple] = queue.Queue()
        self.running = False
        self.selected: ValidationResult | None = None
        self.path_value = tk.StringVar()
        self.status_value = tk.StringVar(value="Choose the mounted iPod to begin.")
        self.progress_value = tk.IntVar(value=0)
        self.force_rebuild = tk.BooleanVar(value=False)
        self.eject_when_finished = tk.BooleanVar(value=True)

        self._configure_window()
        self._build_ui()
        self.root.after(80, self._poll_events)

    def _configure_window(self) -> None:
        self.root.title(f"{APP_NAME} {APP_VERSION}")
        self.root.configure(bg=BACKGROUND)
        self.root.geometry("760x680")
        self.root.minsize(680, 620)
        self.root.protocol("WM_DELETE_WINDOW", self._close)

        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure(
            "Rekordpod.Horizontal.TProgressbar",
            troughcolor=PANEL_LIGHT,
            background=ACCENT,
            bordercolor=PANEL_LIGHT,
            lightcolor=ACCENT,
            darkcolor=ACCENT,
            thickness=12,
        )
        style.configure(
            "Rekordpod.TCheckbutton",
            background=PANEL,
            foreground=TEXT,
            indicatorcolor=BACKGROUND,
            indicatorrelief="flat",
            bordercolor=MUTED,
            focuscolor=PANEL,
            font=("Helvetica", 11),
        )
        style.map(
            "Rekordpod.TCheckbutton",
            background=[("active", PANEL)],
            foreground=[("active", TEXT)],
            indicatorcolor=[("selected", ACCENT), ("!selected", BACKGROUND)],
        )

    def _build_ui(self) -> None:
        outer = tk.Frame(self.root, bg=BACKGROUND, padx=28, pady=22)
        outer.pack(fill="both", expand=True)

        heading = tk.Frame(outer, bg=BACKGROUND)
        heading.pack(fill="x")
        tk.Label(
            heading,
            text="rekordpod",
            bg=BACKGROUND,
            fg=TEXT,
            font=("Helvetica", 30, "bold"),
        ).pack(side="left")
        tk.Label(
            heading,
            text=f"PUBLIC BETA  ·  {APP_VERSION}",
            bg=BACKGROUND,
            fg=ACCENT,
            font=("Helvetica", 10, "bold"),
        ).pack(side="right", pady=(15, 0))

        tk.Label(
            outer,
            text=(
                "Select the iPod yourself. Rekordpod validates only this location "
                "and never searches other drives."
            ),
            bg=BACKGROUND,
            fg=MUTED,
            anchor="w",
            justify="left",
            font=("Helvetica", 11),
        ).pack(fill="x", pady=(5, 18))

        card = tk.Frame(outer, bg=PANEL, padx=18, pady=16)
        card.pack(fill="x")
        tk.Label(
            card,
            text="IPOD DRIVE OR MOUNTED FOLDER",
            bg=PANEL,
            fg=ACCENT,
            anchor="w",
            font=("Helvetica", 9, "bold"),
        ).pack(fill="x")

        path_row = tk.Frame(card, bg=PANEL)
        path_row.pack(fill="x", pady=(7, 9))
        self.path_entry = tk.Entry(
            path_row,
            textvariable=self.path_value,
            bg=BACKGROUND,
            fg=TEXT,
            insertbackground=ACCENT,
            relief="flat",
            highlightthickness=1,
            highlightbackground="#354039",
            highlightcolor=ACCENT,
            font=("Helvetica", 13),
        )
        self.path_entry.pack(side="left", fill="x", expand=True, ipady=9)
        # Aqua draws ordinary Tk buttons with the system's light button face,
        # ignoring ``bg``.  A label-backed control keeps the intended contrast
        # on macOS while using the exact same UI on Windows.
        self.browse_button = tk.Label(
            path_row,
            text="CHOOSE…",
            bg="#080b09",
            fg=TEXT,
            activebackground="#080b09",
            activeforeground=TEXT,
            disabledforeground="#5f6862",
            relief="solid",
            borderwidth=1,
            highlightthickness=0,
            padx=18,
            pady=8,
            font=("Helvetica", 10, "bold"),
            cursor="hand2",
            takefocus=True,
        )
        self.browse_button.pack(side="left", padx=(10, 0))
        self.browse_button.bind("<Button-1>", self._choose_event)
        self.browse_button.bind("<Return>", self._choose_event)
        self.browse_button.bind("<space>", self._choose_event)
        self.browse_button.bind(
            "<Enter>", lambda _event: self._set_browse_background(ACCENT_DARK)
        )
        self.browse_button.bind(
            "<Leave>", lambda _event: self._set_browse_background("#080b09")
        )

        tk.Label(
            card,
            text="Windows example: E:\\    ·    macOS example: /Volumes/RIZZPOD",
            bg=PANEL,
            fg=MUTED,
            anchor="w",
            font=("Helvetica", 9),
        ).pack(fill="x")

        options = tk.Frame(card, bg=PANEL)
        options.pack(fill="x", pady=(13, 0))
        self.rebuild_check = ttk.Checkbutton(
            options,
            text="Rebuild an unchanged cache",
            variable=self.force_rebuild,
            style="Rekordpod.TCheckbutton",
        )
        self.rebuild_check.pack(side="left")
        self.eject_check = ttk.Checkbutton(
            options,
            text="Eject when finished",
            variable=self.eject_when_finished,
            style="Rekordpod.TCheckbutton",
        )
        self.eject_check.pack(side="right")

        progress_card = tk.Frame(outer, bg=PANEL, padx=18, pady=16)
        progress_card.pack(fill="both", expand=True, pady=(14, 0))
        tk.Label(
            progress_card,
            text="INSTALLATION STATUS",
            bg=PANEL,
            fg=ACCENT,
            anchor="w",
            font=("Helvetica", 9, "bold"),
        ).pack(fill="x")
        self.status_label = tk.Label(
            progress_card,
            textvariable=self.status_value,
            bg=PANEL,
            fg=TEXT,
            anchor="w",
            justify="left",
            font=("Helvetica", 12, "bold"),
        )
        self.status_label.pack(fill="x", pady=(8, 8))
        self.progress = ttk.Progressbar(
            progress_card,
            variable=self.progress_value,
            maximum=100,
            style="Rekordpod.Horizontal.TProgressbar",
        )
        self.progress.pack(fill="x")

        log_frame = tk.Frame(progress_card, bg=BACKGROUND)
        log_frame.pack(fill="both", expand=True, pady=(12, 0))
        self.log = tk.Text(
            log_frame,
            height=6,
            bg=BACKGROUND,
            fg=MUTED,
            insertbackground=ACCENT,
            relief="flat",
            padx=10,
            pady=8,
            wrap="word",
            state="disabled",
            font=("Menlo" if self.root.tk.call("tk", "windowingsystem") == "aqua" else "Consolas", 9),
        )
        self.log.pack(side="left", fill="both", expand=True)
        scrollbar = ttk.Scrollbar(log_frame, orient="vertical", command=self.log.yview)
        scrollbar.pack(side="right", fill="y")
        self.log.configure(yscrollcommand=scrollbar.set)

        action_row = tk.Frame(outer, bg=BACKGROUND)
        action_row.pack(fill="x", pady=(16, 0))
        self.action_button = tk.Button(
            action_row,
            text="CHECK + INSTALL",
            command=self._begin,
            bg=ACCENT,
            fg="#001607",
            activebackground="#9affba",
            activeforeground="#001607",
            relief="flat",
            padx=24,
            pady=11,
            font=("Helvetica", 12, "bold"),
            cursor="hand2",
        )
        self.action_button.pack(side="right")
        tk.Label(
            action_row,
            text="Music and Rekordbox analysis are never erased.",
            bg=BACKGROUND,
            fg=MUTED,
            font=("Helvetica", 9),
        ).pack(side="left", pady=(7, 0))

    def _choose(self) -> None:
        if self.running:
            return
        chosen = filedialog.askdirectory(
            title="Choose the mounted iPod",
            mustexist=True,
        )
        if chosen:
            self.path_value.set(chosen)
            self.selected = None
            self._identify_selection()

    def _choose_event(self, _event: tk.Event) -> str:
        self._choose()
        return "break"

    def _set_browse_background(self, color: str) -> None:
        if not self.running:
            self.browse_button.configure(bg=color)

    def _identify_selection(self) -> ValidationResult | None:
        try:
            selected = validate_selected_volume(self.path_value.get())
        except InstallerError as exc:
            self.selected = None
            self.status_value.set("iPod not detected")
            self.status_label.configure(fg=ERROR)
            self._append_log(str(exc))
            messagebox.showerror(
                "Could not identify this iPod",
                str(exc),
                parent=self.root,
            )
            return None

        self.selected = selected
        device = selected.root.name or str(selected.root)
        model = TARGET_NAMES.get(selected.target, selected.target)
        self.status_label.configure(fg=TEXT)
        self.status_value.set(f"{device} detected  ·  {model}")
        self.action_button.configure(text="INSTALL REKORDPOD")
        self._append_log(
            f"Detected {device}: {model}, Rockbox {selected.installed_version}"
        )
        return selected

    def _begin(self) -> None:
        if self.running:
            return
        selected = self._identify_selection()
        if selected is None:
            return

        self.selected = selected
        device = selected.root.name or str(selected.root)
        confirmation = (
            f"Install Rekordpod on {device}?\n\n"
            f"Target: {selected.target}\n"
            f"Current Rockbox: {selected.installed_version}\n"
            f"Free space: {human_size(selected.free_bytes)}\n\n"
            "The cache is built on this computer before the iPod is changed. "
            "A safety copy of export.pdb and existing Rekordpod settings is also created."
        )
        if not messagebox.askokcancel(
            "Confirm Rekordpod installation",
            confirmation,
            icon="warning",
            default="ok",
            parent=self.root,
        ):
            return

        self.running = True
        self.progress_value.set(0)
        self.status_value.set("Starting Rekordpod installation")
        self._append_log(f"Selected only: {selected.root}")
        self._set_controls(False)
        thread = threading.Thread(
            target=self._worker,
            args=(selected, self.force_rebuild.get(), self.eject_when_finished.get()),
            daemon=True,
        )
        thread.start()

    def _worker(
        self,
        selected: ValidationResult,
        force_rebuild: bool,
        eject_when_finished: bool,
    ) -> None:
        def progress(percent: int, label: str) -> None:
            self.events.put(("progress", percent, label))

        def log(message: str) -> None:
            if message:
                self.events.put(("log", message))

        try:
            built: BuildResult = build_or_reuse_package(
                selected,
                force_rebuild=force_rebuild,
                progress=progress,
                log=log,
            )
            result: InstallResult = install_package(
                selected,
                built.package,
                eject=eject_when_finished,
                progress=progress,
                log=log,
            )
            self.events.put(("complete", built, result))
        except Exception as exc:
            self.events.put(("error", str(exc)))

    def _poll_events(self) -> None:
        try:
            while True:
                event = self.events.get_nowait()
                kind = event[0]
                if kind == "progress":
                    _, percent, label = event
                    self.progress_value.set(percent)
                    self.status_value.set(label)
                elif kind == "log":
                    self._append_log(event[1])
                elif kind == "error":
                    self.running = False
                    self._set_controls(True)
                    self._show_error(event[1])
                elif kind == "complete":
                    self.running = False
                    self._set_controls(True)
                    self._show_complete(event[1], event[2])
        except queue.Empty:
            pass
        self.root.after(80, self._poll_events)

    def _show_complete(self, built: BuildResult, result: InstallResult) -> None:
        summary = built.summary
        cache_note = "Reused the unchanged cache." if built.reused_cache else "Built a fresh cache."
        eject_note = (
            "The iPod was ejected safely."
            if result.ejected
            else "Use the system's safe-eject control before disconnecting the iPod."
        )
        message = (
            f"Rekordpod is installed.\n\n"
            f"{summary.get('tracks', 0):,} tracks · "
            f"{summary.get('playlists', 0):,} playlists · "
            f"{summary.get('waveform_tracks', 0):,} RGB waveforms\n"
            f"{cache_note}\n{eject_note}\n\n"
            f"Safety backup:\n{result.backup_directory}"
        )
        self.status_value.set("Rekordpod installed")
        self.progress_value.set(100)
        self._append_log(str(result.backup_directory))
        messagebox.showinfo("Rekordpod installed", message, parent=self.root)

    def _show_error(self, message: str) -> None:
        self.status_value.set("Installation stopped")
        self.status_label.configure(fg=ERROR)
        self._append_log(message)
        messagebox.showerror("Rekordpod was not installed", message, parent=self.root)

    def _append_log(self, message: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", message.rstrip() + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _set_controls(self, enabled: bool) -> None:
        state = "normal" if enabled else "disabled"
        for widget in (
            self.path_entry,
            self.browse_button,
            self.action_button,
            self.rebuild_check,
            self.eject_check,
        ):
            widget.configure(state=state)
        if enabled:
            self.status_label.configure(fg=TEXT)

    def _close(self) -> None:
        if self.running:
            messagebox.showinfo(
                "Installation in progress",
                "Keep this window open until Rekordpod finishes.",
                parent=self.root,
            )
            return
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    RekordpodInstaller(root)
    root.mainloop()


if __name__ == "__main__":
    main()
