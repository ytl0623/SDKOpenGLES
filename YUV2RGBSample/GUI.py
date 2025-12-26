import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import subprocess
import os
import sys

# 執行檔名稱
BINARY_NAME = "./YUV2RGBSample"

class YUVConverterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("YUV2RGB 轉換工具 (Log 顯示版)")
        self.root.geometry("600x600") #稍微加大高度以容納 Log
        
        # 存放補償圖路徑
        self.comp_files = []

        # --- 1. 主要輸入圖片 ---
        self.frame_main = ttk.LabelFrame(root, text="1. 主要輸入圖片 (Input BMP)")
        self.frame_main.pack(padx=10, pady=5, fill="x")
        
        self.entry_main = ttk.Entry(self.frame_main)
        self.entry_main.pack(side="left", padx=5, pady=10, fill="x", expand=True)
        
        btn_main = ttk.Button(self.frame_main, text="瀏覽", command=self.browse_main)
        btn_main.pack(side="right", padx=5, pady=10)

        # --- 2. 補償圖 (一次選取 5 張) ---
        self.frame_comps = ttk.LabelFrame(root, text="2. 補償圖設定 (一次選取 5 張)")
        self.frame_comps.pack(padx=10, pady=5, fill="x")
        
        lbl_hint = ttk.Label(self.frame_comps, text="請按住 Ctrl 或 Shift 鍵一次選取 5 張圖片 (將依檔名自動排序)", foreground="gray")
        lbl_hint.pack(pady=(5, 0))

        self.btn_multi = ttk.Button(self.frame_comps, text="選取 5 張補償圖", command=self.browse_comps)
        self.btn_multi.pack(pady=5)

        # 顯示已選檔案的小列表
        self.listbox_files = tk.Listbox(self.frame_comps, height=5, bg="#fafafa", fg="#333")
        self.listbox_files.pack(padx=10, pady=5, fill="x")

        # --- 3. 執行按鈕 ---
        self.btn_run = ttk.Button(root, text="開始執行轉換", command=self.run_process)
        self.btn_run.pack(pady=10, ipadx=40, ipady=8)

        # --- 4. 執行結果 Log (新增區域) ---
        self.frame_log = ttk.LabelFrame(root, text="3. 執行結果 Log")
        self.frame_log.pack(padx=10, pady=5, fill="both", expand=True)

        # 建立文字框與捲動條
        self.text_log = tk.Text(self.frame_log, bg="black", fg="lime", font=("Courier", 10))
        self.scrollbar = ttk.Scrollbar(self.frame_log, orient="vertical", command=self.text_log.yview)
        
        self.text_log.configure(yscrollcommand=self.scrollbar.set)
        
        self.scrollbar.pack(side="right", fill="y")
        self.text_log.pack(side="left", fill="both", expand=True)

    def browse_main(self):
        filename = filedialog.askopenfilename(filetypes=[("BMP Files", "*.bmp"), ("All Files", "*.*")])
        if filename:
            self.entry_main.delete(0, tk.END)
            self.entry_main.insert(0, filename)

    def browse_comps(self):
        filenames = filedialog.askopenfilenames(
            title="請一次選取 5 張補償圖",
            filetypes=[("BMP Files", "*.bmp"), ("All Files", "*.*")]
        )
        if filenames:
            if len(filenames) != 5:
                messagebox.showwarning("數量錯誤", f"您選取了 {len(filenames)} 張圖片。\n程式需要正好 5 張！")
                return
            
            # 依檔名排序
            self.comp_files = sorted(list(filenames))
            
            # 更新介面列表
            self.listbox_files.delete(0, tk.END)
            for idx, f in enumerate(self.comp_files):
                self.listbox_files.insert(tk.END, f"{idx+1}: {os.path.basename(f)}")

    def log(self, msg, tag=None):
        """ 將訊息寫入下方的 Log 視窗 """
        self.text_log.insert(tk.END, msg + "\n", tag)
        self.text_log.see(tk.END) # 自動捲動到底部

    def run_process(self):
        # 清空 Log
        self.text_log.delete(1.0, tk.END)
        self.log(">>> 準備開始執行...", "info")

        main_bmp = self.entry_main.get().strip()
        
        if not main_bmp:
            self.log("[錯誤] 未選擇主要輸入圖片！")
            return
            
        if len(self.comp_files) != 5:
            self.log("[錯誤] 補償圖數量不足 5 張！")
            return

        if not os.path.exists(BINARY_NAME):
            self.log(f"[錯誤] 找不到執行檔: {BINARY_NAME}")
            self.log("請確認應用程式與執行檔位於同一目錄。")
            return

        # 組合指令
        cmd = [BINARY_NAME, main_bmp] + self.comp_files
        self.log(f"執行指令: {' '.join(cmd)}\n")

        try:
            # 執行並捕捉輸出
            # check=False 讓我們可以手動處理錯誤碼，不會直接 crash
            result = subprocess.run(cmd, capture_output=True, text=True)
            
            # 顯示標準輸出 (stdout)
            if result.stdout:
                self.log("--- 標準輸出 (STDOUT) ---")
                self.log(result.stdout)
            
            # 顯示錯誤輸出 (stderr)
            if result.stderr:
                self.log("--- 錯誤輸出 (STDERR) ---")
                self.log(result.stderr)
            
            self.log("-" * 30)
            if result.returncode == 0:
                self.log(">>> 執行成功 (Success)", "success")
            else:
                self.log(f">>> 執行失敗 (Code: {result.returncode})", "error")

        except Exception as e:
            self.log(f"[系統例外錯誤]: {str(e)}", "error")

if __name__ == "__main__":
    root = tk.Tk()
    # 設定一些 Log 顏色標籤 (Optional)
    app = YUVConverterApp(root)
    # 設定 Text Widget 的顏色標籤
    app.text_log.tag_config("error", foreground="red")
    app.text_log.tag_config("success", foreground="cyan")
    app.text_log.tag_config("info", foreground="yellow")
    
    root.mainloop()