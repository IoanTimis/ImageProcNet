import socket
import struct
import threading
import tkinter as tk
from tkinter import filedialog, ttk, messagebox
from PIL import Image, ImageTk
import io  # folosit pentru manipularea imaginilor in memorie

SERVER_HOST = '127.0.0.1'
SERVER_PORT = 9000           # port HTTP pentru REST API
# NOTIFY_PORT rezervat pentru notificari asincrone

OP_UPLOAD     = 1  # cod pentru solicitare procesare imagine (nefolosit in REST)
NOTIFY_CODE   = 2  # cod notificare finalizare task (neimplementat aici)

class ClientGUI:
    def __init__(self, master):
        self.master = master
        master.title("Client GUI ImageProcNet")

        # Socket REST (HTTP) nu folosim socket TCP aici
        # Vom folosi requests in versiunea finala, dar pastram interfacele
        self.file_path = None
        self.image_data = None
        self._build_ui()

    def _build_ui(self):
        frame_top = ttk.Frame(self.master, padding=10)
        frame_top.pack(fill=tk.X)

        btn_browse = ttk.Button(frame_top, text="Browse...", command=self._browse_file)
        btn_browse.grid(row=0, column=0, sticky=tk.W)

        self.lbl_path = ttk.Label(frame_top, text="Niciun fisier selectat")
        self.lbl_path.grid(row=0, column=1, padx=5)

        self.canvas = tk.Canvas(frame_top, width=200, height=200, bg='grey')
        self.canvas.grid(row=1, column=0, columnspan=2, pady=10)

        frame_mid = ttk.Frame(self.master, padding=10)
        frame_mid.pack(fill=tk.X)

        ttk.Label(frame_mid, text="Procesare:").grid(row=0, column=0)
        self.process_var = tk.StringVar(value="resize")
        ttk.OptionMenu(frame_mid, self.process_var, "resize", "resize", "grayscale", "blur").grid(row=0, column=1)

        frame_bot = ttk.Frame(self.master, padding=10)
        frame_bot.pack(fill=tk.X)

        self.btn_upload = ttk.Button(frame_bot, text="Upload & Proceseaza", command=self._upload)
        self.btn_upload.grid(row=0, column=0)

        self.progress = ttk.Progressbar(frame_bot, mode='indeterminate')
        self.progress.grid(row=0, column=1, padx=5)

        self.btn_save = ttk.Button(frame_bot, text="Salveaza Rezultat", command=self._save_result, state=tk.DISABLED)
        self.btn_save.grid(row=0, column=2)

    def _browse_file(self):
        path = filedialog.askopenfilename(filetypes=[("JPEG","*.jpg"), ("PNG","*.png")])
        if not path:
            return
        self.file_path = path
        self.lbl_path.config(text=path)

        img = Image.open(path)
        img.thumbnail((200,200))
        self.photo = ImageTk.PhotoImage(img)
        self.canvas.create_image(100,100, image=self.photo)

    def _upload(self):
        if not self.file_path:
            messagebox.showwarning("Eroare","Nu ati selectat niciun fisier.")
            return
        # porneste operatia de upload intr-un thread separat
        threading.Thread(target=self._do_upload, daemon=True).start()

    def _do_upload(self):
        try:
            # dezactiveaza butonul pentru a preveni upload-uri simultane
            self.btn_upload.config(state=tk.DISABLED)
            self.progress.start()  # arata utilizatorului ca operatia este in curs

            # Trimite HTTP POST la /process cu fisierul si tipul procesarii
            import requests
            files = {'image': open(self.file_path, 'rb')}
            data = {'type': self.process_var.get()}
            resp = requests.post(f"http://{SERVER_HOST}:{SERVER_PORT}/process", files=files, data=data)
            resp.raise_for_status()  # arunca exceptie la erori HTTP

            # primeste continutul imaginii procesate si il stocheaza in memorie
            img_data = resp.content
            self.image_data = img_data

            # actualizeaza interfata cu preview-ul rezultatului
            img = Image.open(io.BytesIO(img_data))
            img.thumbnail((200,200))  # redimensioneaza pentru afisare rapida
            self.photo = ImageTk.PhotoImage(img)
            self.canvas.create_image(100,100, image=self.photo)

            # activeaza optiunea de salvare a rezultatului
            self.btn_save.config(state=tk.NORMAL)
        except Exception as e:
            # afiseaza un dialog in cazul unei erori de retea sau procesare
            messagebox.showerror("Eroare retea", str(e))
        finally:
            # opreste progress bar si reactiveaza butonul de upload
            self.progress.stop()
            self.btn_upload.config(state=tk.NORMAL)

    def _save_result(self):
        # salveaza datele imaginii procesate in fisierul ales de utilizator
        if not self.image_data:
            return
        path = filedialog.asksaveasfilename(defaultextension='.jpg', filetypes=[("JPEG","*.jpg")])
        if not path:
            return
        with open(path, 'wb') as f:
            f.write(self.image_data)
        # notifica utilizatorul ca salvarea a fost realizata cu succes
        messagebox.showinfo("Salvat","Imaginea a fost salvata cu succes.")

if __name__ == '__main__':
    # porneste bucla principala Tkinter
    root = tk.Tk()
    app = ClientGUI(root)
    root.mainloop()