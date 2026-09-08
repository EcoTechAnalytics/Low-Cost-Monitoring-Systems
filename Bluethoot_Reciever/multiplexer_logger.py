#!/usr/bin/env python3
"""
============================================================
  Multiplexer Data Logger - Raspberry Pi GUI
============================================================
  Reads measurement data from the Receiver Arduino over USB
  (/dev/ttyUSB0). The Receiver already adds its RTC timestamp,
  so each incoming line is already:

        timestamp,channel,countdown,temp

  The Pi just records it (no clock needed), shows it live,
  auto-saves every row to disk as it arrives, and can export
  a clean copy on demand.

  Requirements (install once on the Pi):
      sudo apt update
      sudo apt install python3-serial python3-tk

  Run with:
      python3 multiplexer_logger.py

  NOTE: Close the Arduino IDE Serial Monitor before running,
        only one program can use /dev/ttyUSB0 at a time.
============================================================
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import threading
import csv
import os
from datetime import datetime

import serial  # from pyserial

# ---------------- Configuration ----------------
SERIAL_PORT = "/dev/ttyUSB0"
BAUD_RATE = 9600

# Auto-save file: every received row is appended here immediately,
# so a power cut can never lose collected data.
AUTOSAVE_DIR = os.path.expanduser("~/multiplexer_data")
COLUMNS = ["timestamp", "channel", "countdown", "temp"]


class LoggerApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Multiplexer Data Logger - MonksHill Lab")
        self.root.geometry("720x540")

        self.ser = None
        self.collecting = False
        self.read_thread = None
        self.data = []                  # rows held for the on-demand export
        self.autosave_path = None       # current auto-save file
        self.autosave_file = None
        self.autosave_writer = None

        os.makedirs(AUTOSAVE_DIR, exist_ok=True)
        self._build_gui()

    # ---------------- GUI layout ----------------
    def _build_gui(self):
        top = tk.Frame(self.root, pady=10)
        top.pack(fill=tk.X)

        self.start_btn = tk.Button(top, text="\u25B6 Start Collecting",
                                   bg="#2e7d32", fg="white",
                                   font=("Arial", 12, "bold"),
                                   width=16, command=self.start_collecting)
        self.start_btn.pack(side=tk.LEFT, padx=8)

        self.stop_btn = tk.Button(top, text="\u23F9 Stop",
                                  bg="#c62828", fg="white",
                                  font=("Arial", 12, "bold"),
                                  width=10, command=self.stop_collecting,
                                  state=tk.DISABLED)
        self.stop_btn.pack(side=tk.LEFT, padx=8)

        self.export_btn = tk.Button(top, text="\U0001F4BE Export CSV",
                                    bg="#1565c0", fg="white",
                                    font=("Arial", 12, "bold"),
                                    width=14, command=self.export_csv)
        self.export_btn.pack(side=tk.LEFT, padx=8)

        # --- Status lines ---
        self.status = tk.Label(self.root, text="Idle - not collecting",
                               font=("Arial", 10), fg="#555")
        self.status.pack(anchor=tk.W, padx=12)

        self.count_label = tk.Label(self.root, text="Rows collected: 0",
                                    font=("Arial", 10), fg="#555")
        self.count_label.pack(anchor=tk.W, padx=12)

        self.autosave_label = tk.Label(self.root, text="Auto-save: (not started)",
                                       font=("Arial", 9), fg="#888")
        self.autosave_label.pack(anchor=tk.W, padx=12)

        # --- Setpoint sender ---
        sp = tk.Frame(self.root, pady=6)
        sp.pack(fill=tk.X, padx=12)
        tk.Label(sp, text="Set temperature:", font=("Arial", 10)).pack(side=tk.LEFT)
        self.sp_entry = tk.Entry(sp, width=6)
        self.sp_entry.pack(side=tk.LEFT, padx=4)
        tk.Button(sp, text="Send SET", command=self.send_setpoint).pack(side=tk.LEFT)

        # --- Live data table ---
        self.tree = ttk.Treeview(self.root, columns=COLUMNS, show="headings", height=16)
        for c, w in zip(COLUMNS, (180, 90, 90, 80)):
            self.tree.heading(c, text=c)
            self.tree.column(c, width=w, anchor=tk.CENTER)
        self.tree.pack(fill=tk.BOTH, expand=True, padx=12, pady=8)

        sb = ttk.Scrollbar(self.tree, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        sb.pack(side=tk.RIGHT, fill=tk.Y)

    # ---------------- Serial handling ----------------
    def start_collecting(self):
        if self.collecting:
            return
        try:
            self.ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        except Exception as e:
            messagebox.showerror("Serial error",
                                 f"Could not open {SERIAL_PORT}.\n\n{e}\n\n"
                                 "Is the Arduino IDE Serial Monitor still open?")
            return

        # Open a fresh auto-save file for this session
        fname = "session_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".csv"
        self.autosave_path = os.path.join(AUTOSAVE_DIR, fname)
        self.autosave_file = open(self.autosave_path, "w", newline="")
        self.autosave_writer = csv.writer(self.autosave_file)
        self.autosave_writer.writerow(COLUMNS)
        self.autosave_file.flush()
        self.autosave_label.config(text=f"Auto-save: {self.autosave_path}")

        self.collecting = True
        self.start_btn.config(state=tk.DISABLED)
        self.stop_btn.config(state=tk.NORMAL)
        self.status.config(text=f"Collecting from {SERIAL_PORT} ...", fg="#2e7d32")

        self.read_thread = threading.Thread(target=self._read_loop, daemon=True)
        self.read_thread.start()

    def stop_collecting(self):
        self.collecting = False
        self.stop_btn.config(state=tk.DISABLED)
        self.start_btn.config(state=tk.NORMAL)
        self.status.config(text="Stopped.", fg="#555")
        if self.ser and self.ser.is_open:
            self.ser.close()
        if self.autosave_file:
            self.autosave_file.flush()
            self.autosave_file.close()
            self.autosave_file = None

    def _read_loop(self):
        """Background thread: read, parse, auto-save, queue GUI update."""
        while self.collecting:
            try:
                raw = self.ser.readline().decode("utf-8", errors="ignore").strip()
            except Exception:
                continue
            if not raw:
                continue

            # Receiver sends: timestamp,channel,countdown,temp
            parts = raw.split(",")
            if len(parts) != 4:
                continue   # ignore confirmations / malformed lines

            row = [p.strip() for p in parts]
            self.data.append(row)

            # Auto-save immediately so nothing is lost on power cut
            try:
                self.autosave_writer.writerow(row)
                self.autosave_file.flush()
            except Exception:
                pass

            self.root.after(0, self._add_row, row)

    def _add_row(self, row):
        self.tree.insert("", tk.END, values=row)
        self.tree.yview_moveto(1.0)
        self.count_label.config(text=f"Rows collected: {len(self.data)}")

    # ---------------- Setpoint command ----------------
    def send_setpoint(self):
        if not (self.ser and self.ser.is_open):
            messagebox.showwarning("Not connected",
                                   "Start collecting first to open the serial port.")
            return
        val = self.sp_entry.get().strip()
        if not val:
            return
        try:
            float(val)
        except ValueError:
            messagebox.showwarning("Invalid", "Enter a number, e.g. 27")
            return
        self.ser.write(f"SET {val}\n".encode("utf-8"))
        self.status.config(text=f"Sent: SET {val}", fg="#1565c0")

    # ---------------- Export ----------------
    def export_csv(self):
        if not self.data:
            messagebox.showinfo("No data", "Nothing collected yet.")
            return
        default_name = "multiplexer_" + datetime.now().strftime("%Y%m%d_%H%M%S") + ".csv"
        path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            initialfile=default_name,
            filetypes=[("CSV files", "*.csv")])
        if not path:
            return
        try:
            with open(path, "w", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(COLUMNS)
                writer.writerows(self.data)
            messagebox.showinfo("Exported",
                                f"Saved {len(self.data)} rows to:\n{path}\n\n"
                                f"(A live copy was also auto-saved at:\n{self.autosave_path})")
        except Exception as e:
            messagebox.showerror("Export error", str(e))


if __name__ == "__main__":
    root = tk.Tk()
    app = LoggerApp(root)
    root.mainloop()
