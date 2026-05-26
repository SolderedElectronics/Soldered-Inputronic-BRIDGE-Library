#!/usr/bin/env python3
"""
Inputronic BRIDGE Firmware Flasher
====================================
GUI tool for flashing firmware to the Inputronic BRIDGE device (ESP32-based).

The firmware binary is bundled inside this executable. Select a port, click
Flash, and the tool handles the rest.

Dependencies:
  pip install -r requirements.txt     (customtkinter + pyserial + esptool)
"""

import contextlib
import io
import sys
import threading
from pathlib import Path

import tkinter as tk
from tkinter import messagebox

try:
    import customtkinter as ctk
    ctk.set_appearance_mode("dark")
    ctk.set_default_color_theme("blue")
except ImportError:
    raise SystemExit(
        "customtkinter not found.\n"
        "Install with:  pip install customtkinter\n"
        "or:            pip install -r requirements.txt"
    )

try:
    import serial.tools.list_ports
    _SERIAL_OK = True
except ImportError:
    _SERIAL_OK = False

try:
    import esptool
    _ESPTOOL_OK = True
except ImportError:
    _ESPTOOL_OK = False

# ── Dark theme palette (matches waveform editor) ──────────────────────────────
_BG  = "#1c1c1c"
_FG  = "#d0d0d0"
_BG2 = "#2b2b2b"
_BG3 = "#333333"

FLASH_ADDRESS = "0x0"   # merged binary always starts at 0x0
DEFAULT_BAUD  = "921600"


def _base_dir() -> Path:
    """Return the directory containing bundled resources."""
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS)
    return Path(__file__).parent.parent  # extras/ when running as script


def _find_firmware_binaries() -> list[Path]:
    """Return sorted list of .bin files bundled in firmware_binaries/."""
    d = _base_dir() / "firmware_binaries"
    if not d.exists():
        return []
    return sorted(d.glob("*.bin"))


# ──────────────────────────────────────────────────────────────────────────────
class FirmwareFlasher(ctk.CTk):
    """Main application window."""

    def __init__(self) -> None:
        super().__init__()
        self.title("Inputronic BRIDGE Firmware Flasher")
        self.geometry("700x560")
        self.minsize(560, 440)
        self.configure(fg_color=_BG)
        self.resizable(True, True)

        self._port_var   = tk.StringVar()
        self._baud_var   = tk.StringVar(value=DEFAULT_BAUD)
        self._status_var = tk.StringVar(value="Ready.")
        self._flashing   = False

        self._binaries = _find_firmware_binaries()
        self._selected_bin: Path | None = self._binaries[0] if self._binaries else None

        self._build_ui()
        self._refresh_ports()

    # ── UI construction ───────────────────────────────────────────────────────

    def _build_ui(self) -> None:

        # ── header card ───────────────────────────────────────────────────────
        header = ctk.CTkFrame(self, corner_radius=10, fg_color=_BG2)
        header.pack(fill=tk.X, padx=14, pady=(14, 6))

        ctk.CTkLabel(
            header,
            text="Inputronic BRIDGE  —  Firmware Flasher",
            font=ctk.CTkFont(size=16, weight="bold"),
            anchor="w",
        ).pack(side=tk.LEFT, padx=16, pady=12)

        # ── config card ───────────────────────────────────────────────────────
        cfg = ctk.CTkFrame(self, corner_radius=10, fg_color=_BG2)
        cfg.pack(fill=tk.X, padx=14, pady=6)

        # Firmware row
        fw_row = ctk.CTkFrame(cfg, fg_color="transparent")
        fw_row.pack(fill=tk.X, padx=14, pady=(12, 4))
        ctk.CTkLabel(
            fw_row, text="Firmware", width=80, anchor="w",
            font=ctk.CTkFont(size=12),
        ).pack(side=tk.LEFT)

        fw_name, fw_size = self._firmware_display()
        self._fw_label = ctk.CTkLabel(
            fw_row,
            text=fw_name,
            anchor="w",
            font=ctk.CTkFont(family="Courier", size=11),
            text_color="#81c784" if self._selected_bin else "#ef5350",
        )
        self._fw_label.pack(side=tk.LEFT, padx=(0, 12))

        if fw_size:
            ctk.CTkLabel(
                fw_row, text=fw_size,
                text_color="#666666",
                font=ctk.CTkFont(size=10),
            ).pack(side=tk.LEFT)

        ctk.CTkLabel(
            fw_row, text=f"@ {FLASH_ADDRESS}",
            text_color="#555555",
            font=ctk.CTkFont(size=10),
        ).pack(side=tk.RIGHT, padx=(0, 4))

        # Port row
        port_row = ctk.CTkFrame(cfg, fg_color="transparent")
        port_row.pack(fill=tk.X, padx=14, pady=4)
        ctk.CTkLabel(
            port_row, text="Port", width=80, anchor="w",
            font=ctk.CTkFont(size=12),
        ).pack(side=tk.LEFT)
        self._port_combo = ctk.CTkComboBox(
            port_row, variable=self._port_var,
            values=[], width=200, state="readonly",
            font=ctk.CTkFont(size=12),
        )
        self._port_combo.pack(side=tk.LEFT, padx=(0, 6))
        ctk.CTkButton(
            port_row, text="↻", width=34, height=30,
            command=self._refresh_ports,
            font=ctk.CTkFont(size=14),
        ).pack(side=tk.LEFT)

        if not _SERIAL_OK:
            ctk.CTkLabel(
                port_row, text="pyserial not installed",
                text_color="#ef5350", font=ctk.CTkFont(size=10),
            ).pack(side=tk.LEFT, padx=10)

        # Baud row
        baud_row = ctk.CTkFrame(cfg, fg_color="transparent")
        baud_row.pack(fill=tk.X, padx=14, pady=(4, 14))
        ctk.CTkLabel(
            baud_row, text="Baud rate", width=80, anchor="w",
            font=ctk.CTkFont(size=12),
        ).pack(side=tk.LEFT)
        ctk.CTkComboBox(
            baud_row, variable=self._baud_var,
            values=["115200", "460800", "921600"],
            width=120, state="readonly",
            font=ctk.CTkFont(size=12),
        ).pack(side=tk.LEFT)

        # ── flash button ──────────────────────────────────────────────────────
        btn_frame = ctk.CTkFrame(self, fg_color="transparent")
        btn_frame.pack(fill=tk.X, padx=14, pady=6)

        flash_state = "normal" if (self._selected_bin and _ESPTOOL_OK and _SERIAL_OK) else "disabled"
        self._flash_btn = ctk.CTkButton(
            btn_frame,
            text="⬆  Flash Firmware",
            command=self._flash,
            state=flash_state,
            height=44,
            font=ctk.CTkFont(size=14, weight="bold"),
            fg_color="#2e6e2e",
            hover_color="#3a8a3a",
        )
        self._flash_btn.pack(fill=tk.X)

        if not _ESPTOOL_OK:
            ctk.CTkLabel(
                btn_frame, text="esptool not installed — pip install esptool",
                text_color="#ef5350", font=ctk.CTkFont(size=10),
            ).pack(pady=(4, 0))

        if not self._selected_bin:
            ctk.CTkLabel(
                btn_frame, text="No firmware binary found in firmware_binaries/",
                text_color="#ef5350", font=ctk.CTkFont(size=10),
            ).pack(pady=(4, 0))

        # ── log area ──────────────────────────────────────────────────────────
        log_outer = ctk.CTkFrame(self, corner_radius=10, fg_color=_BG2)
        log_outer.pack(fill=tk.BOTH, expand=True, padx=14, pady=(6, 0))

        log_hdr = ctk.CTkFrame(log_outer, fg_color="transparent")
        log_hdr.pack(fill=tk.X, padx=10, pady=(6, 2))
        ctk.CTkLabel(
            log_hdr, text="Output",
            font=ctk.CTkFont(size=12, weight="bold"),
            anchor="w",
        ).pack(side=tk.LEFT)
        ctk.CTkButton(
            log_hdr, text="Clear", width=60, height=26,
            command=self._log_clear,
            fg_color=_BG3, hover_color="#444",
        ).pack(side=tk.RIGHT)

        self._log_box = ctk.CTkTextbox(
            log_outer, state="disabled",
            font=ctk.CTkFont(family="Courier", size=10),
            fg_color=_BG, text_color=_FG,
            activate_scrollbars=True, corner_radius=6,
        )
        self._log_box.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 8))
        self._log_box._textbox.tag_configure("INFO", foreground="#9e9e9e")
        self._log_box._textbox.tag_configure("OK",   foreground="#81c784")
        self._log_box._textbox.tag_configure("ERR",  foreground="#ef5350")
        self._log_box._textbox.tag_configure("OUT",  foreground=_FG)

        # ── status bar ────────────────────────────────────────────────────────
        status_bar = ctk.CTkFrame(self, fg_color=_BG2, corner_radius=0, height=28)
        status_bar.pack(fill=tk.X, side=tk.BOTTOM)
        status_bar.pack_propagate(False)
        ctk.CTkLabel(
            status_bar, textvariable=self._status_var,
            anchor="w", font=ctk.CTkFont(size=10),
            text_color="#888888",
        ).pack(side=tk.LEFT, padx=10)

    # ── helpers ───────────────────────────────────────────────────────────────

    def _firmware_display(self) -> tuple[str, str]:
        if not self._selected_bin:
            return "No firmware binary found", ""
        size_kb = self._selected_bin.stat().st_size / 1024
        return self._selected_bin.name, f"{size_kb:.1f} KB"

    def _refresh_ports(self) -> None:
        if not _SERIAL_OK:
            return
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self._port_combo.configure(values=ports)
        if ports:
            if self._port_var.get() not in ports:
                self._port_combo.set(ports[0])
            self._set_status(f"Found {len(ports)} port(s).")
        else:
            self._port_combo.set("")
            self._set_status("No serial ports found.")

    def _set_status(self, msg: str) -> None:
        self._status_var.set(msg)

    # ── log ───────────────────────────────────────────────────────────────────

    def _log(self, text: str, tag: str = "OUT") -> None:
        self._log_box.configure(state="normal")
        self._log_box._textbox.insert(tk.END, text + "\n", tag)
        self._log_box._textbox.see(tk.END)
        self._log_box.configure(state="disabled")

    def _log_clear(self) -> None:
        self._log_box.configure(state="normal")
        self._log_box.delete("0.0", tk.END)
        self._log_box.configure(state="disabled")

    # ── flash ─────────────────────────────────────────────────────────────────

    def _flash(self) -> None:
        if self._flashing:
            return

        if not _ESPTOOL_OK:
            messagebox.showerror("esptool missing",
                                 "esptool not installed.\n"
                                 "Install with:  pip install esptool", parent=self)
            return

        port = self._port_var.get()
        if not port:
            messagebox.showerror("No port", "Select a serial port first.", parent=self)
            return

        if not self._selected_bin or not self._selected_bin.exists():
            messagebox.showerror("No firmware", "Firmware binary not found.", parent=self)
            return

        baud = self._baud_var.get()
        args = [
            "--chip", "esp32c6",
            "--port", port,
            "--baud", baud,
            "--before", "default_reset",
            "--after", "hard_reset",
            "write_flash",
            "-z",
            FLASH_ADDRESS, str(self._selected_bin),
        ]

        self._flashing = True
        self._flash_btn.configure(state="disabled", text="Flashing…")
        self._set_status(f"Flashing {self._selected_bin.name} → {port} @ {baud} baud…")
        self._log(f"esptool {' '.join(args)}", "INFO")

        threading.Thread(target=self._flash_thread, args=(args,), daemon=True).start()

    def _flash_thread(self, args: list[str]) -> None:
        class _LineWriter(io.RawIOBase):
            def __init__(self, cb):
                self._cb = cb
                self._buf = ""
            def write(self, s):
                if isinstance(s, bytes):
                    s = s.decode("utf-8", errors="replace")
                self._buf += s
                while "\n" in self._buf:
                    line, self._buf = self._buf.split("\n", 1)
                    if line.strip():
                        self._cb(line)
                return len(s)
            def flush(self): pass
            @property
            def encoding(self): return "utf-8"
            @property
            def errors(self): return "replace"

        def on_line(line: str) -> None:
            ll = line.lower()
            is_err = any(w in ll for w in ("error", "failed", "invalid", "warning")) and "no warning" not in ll
            tag = "ERR" if is_err else "OUT"
            self.after(0, lambda l=line, t=tag: self._log(l, t))

        writer = _LineWriter(on_line)
        success = False
        try:
            with contextlib.redirect_stdout(writer), contextlib.redirect_stderr(writer):
                esptool.main(args)
            success = True
        except SystemExit as exc:
            success = exc.code in (None, 0)
            if not success:
                self.after(0, lambda: self._log(f"Flash failed (exit {exc.code}).", "ERR"))
        except Exception as exc:
            self.after(0, lambda e=str(exc): self._log(e, "ERR"))

        if success:
            self.after(0, lambda: self._log("Flash complete. Device is resetting.", "OK"))
            self.after(0, lambda: self._set_status("Flash complete."))
        else:
            self.after(0, lambda: self._set_status("Flash failed."))

        self.after(0, self._flash_done)

    def _flash_done(self) -> None:
        self._flashing = False
        self._flash_btn.configure(state="normal", text="⬆  Flash Firmware")


# ──────────────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    app = FirmwareFlasher()
    app.mainloop()
