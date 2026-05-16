#Clay Sanders 2026
import tkinter as tk
from tkinter import messagebox, simpledialog
import sqlite3

DB_NAME = "chatbot_memory.db"

class Chatbot:
    def __init__(self, name="Ray"):
        self.name = name
        self.conn = sqlite3.connect(DB_NAME)
        self.cursor = self.conn.cursor()
        self._setup_db()

    def _setup_db(self):
        self.cursor.execute("CREATE TABLE IF NOT EXISTS knowledge (id INTEGER PRIMARY KEY AUTOINCREMENT, question TEXT UNIQUE, answer TEXT)")
        self.conn.commit()

    def get_answer(self, question):
        self.cursor.execute("SELECT answer FROM knowledge WHERE question = ?", (question.lower(),))
        result = self.cursor.fetchone()
        return result[0] if result else None

    def learn(self, question, answer):
        self.cursor.execute("INSERT OR REPLACE INTO knowledge (question, answer) VALUES (?, ?)", (question.lower(), answer))
        self.conn.commit()

class ChatGUI:
    def __init__(self, root):
        self.bot = Chatbot()
        self.root = root
        self.root.title("Info Browser")
        self.root.geometry("400x500")

        # Chat display area
        self.chat_log = tk.Text(root, state='disabled', bg="white")
        self.chat_log.pack(padx=10, pady=10, fill=tk.BOTH, expand=True)

        # User input field
        self.user_entry = tk.Entry(root)
        self.user_entry.bind("<Return>", self.send_message) # Press Enter to send
        self.user_entry.pack(padx=10, pady=5, fill=tk.X)

        # Send button
        self.send_button = tk.Button(root, text="Send", command=self.send_message)
        self.send_button.pack(pady=5)

        self.display_message("System", "Infor Browser ready. Type 'quit' to exit.")

    def display_message(self, sender, message):
        self.chat_log.config(state='normal')
        self.chat_log.insert(tk.END, f"{sender}: {message}\n")
        self.chat_log.config(state='disabled')
        self.chat_log.see(tk.END)

    def send_message(self, event=None):
        user_input = self.user_entry.get().strip()
        if not user_input:
            return
        
        if user_input.lower() == "quit":
            self.root.destroy()
            return

        self.display_message("You", user_input)
        self.user_entry.delete(0, tk.END)

        # Bot Logic
        known_answer = self.bot.get_answer(user_input)
        
        if known_answer:
            self.display_message(self.bot.name, known_answer)
        else:
