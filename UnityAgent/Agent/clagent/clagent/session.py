"""会话存储：内存 + data/sessions/*.json 持久化（原子写入）。"""

import json
import os
import time
import uuid


class Session:
    def __init__(self, sid: str = None):
        self.id = sid or uuid.uuid4().hex[:12]
        self.title = ""
        self.created = time.time()
        self.updated = time.time()
        # OpenAI API 形式的消息；附加 clagent_* 字段仅用于界面展示，发往前会剔除
        self.messages = []

    def touch(self):
        self.updated = time.time()

    def save(self, directory: str):
        os.makedirs(directory, exist_ok=True)
        data = {
            "id": self.id,
            "title": self.title,
            "created": self.created,
            "updated": self.updated,
            "messages": self.messages,
        }
        path = os.path.join(directory, self.id + ".json")
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False)
        os.replace(tmp, path)

    @classmethod
    def load(cls, path: str):
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        s = cls(sid=data.get("id"))
        s.title = data.get("title") or ""
        s.created = float(data.get("created") or time.time())
        s.updated = float(data.get("updated") or s.created)
        msgs = data.get("messages")
        s.messages = msgs if isinstance(msgs, list) else []
        return s


class SessionStore:
    def __init__(self, directory: str):
        self.dir = directory
        os.makedirs(self.dir, exist_ok=True)
        self.sessions = {}
        for fn in os.listdir(self.dir):
            if not fn.endswith(".json"):
                continue
            try:
                s = Session.load(os.path.join(self.dir, fn))
                self.sessions[s.id] = s
            except Exception as e:
                print(f"[clagent] 会话文件读取失败 {fn}: {e}")

    def create(self) -> Session:
        s = Session()
        self.sessions[s.id] = s
        return s

    def get(self, sid: str):
        return self.sessions.get(sid) if sid else None

    def delete(self, sid: str):
        s = self.sessions.pop(sid, None)
        if s is None:
            return
        try:
            os.remove(os.path.join(self.dir, s.id + ".json"))
        except OSError:
            pass

    def save(self, s: Session):
        try:
            s.save(self.dir)
        except OSError as e:
            print(f"[clagent] 会话保存失败：{e}")

    def list(self):
        return sorted(self.sessions.values(), key=lambda s: s.updated, reverse=True)
