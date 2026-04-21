import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext
import threading
import os
import sys

# Add current directory to path so we can import ai
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
try:
    from ai import process_and_save
except ImportError:
    # Handle case where it's run differently
    from .ai import process_and_save

class App:
    def __init__(self, root):
        self.root = root
        self.root.title("Vision Logistics Entry Tool")
        self.root.geometry("600x600")

        # Title
        tk.Label(root, text="Vision Logistics Data Entry", font=("Helvetica", 16, "bold")).pack(pady=10)

        # Image Path
        frame_img = tk.LabelFrame(root, text="Step 1: Image Selection", padx=10, pady=10)
        frame_img.pack(fill="x", padx=20, pady=5)
        
        self.img_path_var = tk.StringVar()
        tk.Entry(frame_img, textvariable=self.img_path_var, width=50).pack(side="left", padx=5)
        tk.Button(frame_img, text="Browse", command=self.browse_image).pack(side="left")

        # Invoice IDs
        frame_ids = tk.LabelFrame(root, text="Step 2: Enter Invoice IDs (one per line, in order)", padx=10, pady=10)
        frame_ids.pack(fill="both", expand=True, padx=20, pady=5)
        
        self.ids_text = scrolledtext.ScrolledText(frame_ids, height=10, width=50)
        self.ids_text.pack(fill="both", expand=True)

        # Process Button
        self.process_btn = tk.Button(root, text="Step 3: Process & Sync to Google Sheets", 
                                     command=self.start_processing, bg="#4CAF50", fg="white", 
                                     font=("Helvetica", 12, "bold"), pady=10)
        self.process_btn.pack(fill="x", padx=20, pady=10)

        # Status/Log
        frame_log = tk.LabelFrame(root, text="Status/Log", padx=10, pady=10)
        frame_log.pack(fill="both", expand=True, padx=20, pady=10)
        
        self.log_text = scrolledtext.ScrolledText(frame_log, height=8, width=70, state='disabled', bg="#f0f0f0")
        self.log_text.pack(fill="both", expand=True)

    def browse_image(self):
        filename = filedialog.askopenfilename(filetypes=[("Image files", "*.png *.jpg *.jpeg")])
        if filename:
            self.img_path_var.set(filename)

    def log(self, message):
        self.log_text.config(state='normal')
        self.log_text.insert(tk.END, f"> {message}\n")
        self.log_text.see(tk.END)
        self.log_text.config(state='disabled')

    def start_processing(self):
        img_path = self.img_path_var.get()
        ids_raw = self.ids_text.get("1.0", tk.END).strip()
        
        if not img_path:
            messagebox.showerror("Error", "Please select an image file.")
            return
        if not ids_raw:
            messagebox.showerror("Error", "Please enter at least one Invoice ID.")
            return

        ids = [line.strip() for line in ids_raw.split('\n') if line.strip()]
        
        self.process_btn.config(state='disabled', text="Processing... Please wait")
        self.log(f"Starting process for {len(ids)} IDs...")
        
        # Run in thread to keep UI responsive
        threading.Thread(target=self.run_process, args=(img_path, ids), daemon=True).start()

    def run_process(self, img_path, ids):
        try:
            process_and_save(img_path, ids, status_callback=self.log)
        except Exception as e:
            self.log(f"CRITICAL ERROR: {str(e)}")
        finally:
            self.root.after(0, self.reset_button)

    def reset_button(self):
        self.process_btn.config(state='normal', text="Step 3: Process & Sync to Google Sheets")

if __name__ == "__main__":
    root = tk.Tk()
    app = App(root)
    root.mainloop()
