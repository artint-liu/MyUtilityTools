"""FastAPI 应用：路由 + 后台定时存盘 + 内嵌前端页面。"""
from __future__ import annotations

import asyncio
import json
import os
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, File, HTTPException, Request, UploadFile
from fastapi.responses import FileResponse, HTMLResponse
from pydantic import BaseModel

import config
from config import (
    HEARTBEAT_TIMEOUT_SECONDS,
    SAVE_INTERVAL_SECONDS,
)
from file_indexer import FileIndexer
from progress_manager import ProgressManager
from sample_reader import SampleReader
from utils import LOGGER


# ---- 全局状态 ----
class AppState:
    def __init__(self):
        self.data_path: Optional[Path] = None
        self.indexer: Optional[FileIndexer] = None
        self.reader: Optional[SampleReader] = None
        self.progress: Optional[ProgressManager] = None
        self.last_heartbeat: float = 0.0
        self._shutdown = False
        self.file_history: list[str] = []


state = AppState()

# 打开/上传文件时为全局串行锁：防止大文件加载期间并发请求导致状态错乱
_open_lock = asyncio.Lock()

# 文件历史记录持久化路径
_HISTORY_PATH = Path(__file__).parent / ".filehistory.json"


def load_history() -> None:
    """加载已打开过的文件历史。"""
    global state
    if _HISTORY_PATH.exists():
        try:
            data = json.loads(_HISTORY_PATH.read_text(encoding="utf-8"))
            state.file_history = [p for p in data.get("files", []) if Path(p).exists()]
        except Exception:  # noqa: BLE001
            state.file_history = []


def save_history() -> None:
    """保存文件历史（仅保留仍存在且最多 50 条）。"""
    try:
        _HISTORY_PATH.write_text(
            json.dumps({"files": state.file_history}, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
    except Exception:  # noqa: BLE001
        pass


def record_file(path: str) -> None:
    """记录一个打开过的文件（去重、置顶、限制长度）。"""
    global state
    path = str(Path(path).resolve())
    state.file_history = [p for p in state.file_history if p != path]
    state.file_history.insert(0, path)
    state.file_history = state.file_history[:50]
    save_history()


# ---- 请求模型 ----
class MarkRequest(BaseModel):
    index: int
    status: str  # pass | reject | skip | unmarked


class NavigateRequest(BaseModel):
    index: int


class OpenRequest(BaseModel):
    file_path: str


class SaveRequest(BaseModel):
    pass


class ExportRequest(BaseModel):
    file_path: Optional[str] = None  # 不传时使用当前已打开文件
    statuses: list[str] = ["pass"]   # 导出哪些状态的条目，默认 pass


# ---- 初始化数据 ----
def load_data_file(file_path: str | Path) -> None:
    """加载数据文件，构建索引并初始化进度管理。"""
    global state
    p = Path(file_path)
    if not p.exists():
        raise FileNotFoundError(f"数据文件不存在: {p}")
    state.data_path = p
    state.indexer = FileIndexer(p)
    offsets = state.indexer.build()
    state.reader = SampleReader(p, offsets)
    state.progress = ProgressManager(p, state.reader.total)
    LOGGER.info("数据文件已加载: %s, 共 %d 条样本", p, state.reader.total)


async def background_saver() -> None:
    """后台定时存盘任务 + 心跳断开检测。"""
    import time

    while not state._shutdown:
        await asyncio.sleep(SAVE_INTERVAL_SECONDS)
        if state.progress is None:
            continue
        # 定时存盘：有变更则保存
        if state.progress.dirty:
            await state.progress.save()
            LOGGER.info("定时存盘完成")
        # 心跳超时检测：超过阈值视为前端断开，触发存盘
        if state.last_heartbeat and (
            time.time() - state.last_heartbeat > HEARTBEAT_TIMEOUT_SECONDS
        ):
            if state.progress.dirty:
                await state.progress.save()
                LOGGER.info("检测到前端断开，已存盘")
            state.last_heartbeat = 0.0


@asynccontextmanager
async def lifespan(app: FastAPI):
    # 加载文件历史
    load_history()
    # 启动时若有默认文件则加载
    if config.DEFAULT_DATA_FILE:
        try:
            load_data_file(config.DEFAULT_DATA_FILE)
            record_file(config.DEFAULT_DATA_FILE)
        except Exception as exc:  # noqa: BLE001
            LOGGER.error("默认数据文件加载失败: %s", exc)
    saver = asyncio.create_task(background_saver())
    yield
    state._shutdown = True
    saver.cancel()
    # 关闭时存盘
    if state.progress is not None:
        await state.progress.save()
        LOGGER.info("应用关闭，已存盘")


app = FastAPI(title="JSON 训练数据标记工具", lifespan=lifespan)


# ---- 辅助 ----
def _require_data():
    if state.reader is None or state.progress is None:
        raise HTTPException(status_code=400, detail="尚未加载数据文件")
    return state


def _sample_payload(index: int) -> dict:
    st = _require_data()
    try:
        data = st.reader.read(index)
    except IndexError:
        raise HTTPException(status_code=404, detail="样本索引越界")
    status = st.progress.status_of(index)
    return {
        "index": index,
        "total": st.reader.total,
        "data": data,
        "status": status,
    }


# ---- 路由 ----
@app.get("/", response_class=HTMLResponse)
async def index():
    return HTMLResponse(FRONTEND_HTML)


@app.get("/api/status")
async def api_status():
    st = _require_data()
    return {
        "data_file": str(st.data_path),
        "total": st.reader.total,
        "current_index": st.progress.current_index,
        **st.progress.to_dict(),
    }


@app.get("/api/sample/{index}")
async def api_sample(index: int):
    return _sample_payload(index)


@app.post("/api/mark")
async def api_mark(req: MarkRequest):
    st = _require_data()
    if req.index < 0 or req.index >= st.reader.total:
        raise HTTPException(status_code=404, detail="样本索引越界")
    st.progress.mark(req.index, req.status)
    # 返回下一条（跳过已标记的，定位到下一个未标记项，便于连续标注）
    return {"success": True, "index": req.index, "status": req.status}


@app.post("/api/navigate")
async def api_navigate(req: NavigateRequest):
    st = _require_data()
    if req.index < 0 or req.index >= st.reader.total:
        raise HTTPException(status_code=404, detail="样本索引越界")
    st.progress.set_current(req.index)
    return _sample_payload(req.index)


@app.post("/api/save")
async def api_save(req: SaveRequest = None):
    st = _require_data()
    await st.progress.save()
    return {"success": True}


@app.post("/api/heartbeat")
async def api_heartbeat(request: Request):
    import time

    state.last_heartbeat = time.time()
    return {"success": True}


@app.post("/api/open")
async def api_open(req: OpenRequest):
    async with _open_lock:
        try:
            load_data_file(req.file_path)
            record_file(req.file_path)
            import time
            state.last_heartbeat = time.time()
            return {
                "success": True,
                "total": state.reader.total,
                "current_index": state.progress.current_index,
                "files": state.file_history,
            }
        except Exception as exc:  # noqa: BLE001
            raise HTTPException(status_code=400, detail=str(exc))


@app.get("/api/files")
async def api_files():
    """返回所有打开过的文件历史列表。"""
    return {"files": state.file_history}


_UPLOAD_DIR = Path(__file__).parent / "uploads"
_UPLOAD_DIR.mkdir(parents=True, exist_ok=True)


@app.post("/api/upload")
async def api_upload(file: UploadFile = File(...)):
    """接收拖拽/选择的文件，保存到 uploads 目录后打开索引。"""
    import shutil

    async with _open_lock:
        if not file.filename or not (
            file.filename.lower().endswith(".json")
            or file.filename.lower().endswith(".jsonl")
        ):
            raise HTTPException(status_code=400, detail="仅支持 .json / .jsonl 文件")
        # 避免文件名冲突：保留原名（同目录内覆盖）
        dest = _UPLOAD_DIR / file.filename
        try:
            with dest.open("wb") as out:
                shutil.copyfileobj(file.file, out)
        except Exception as exc:  # noqa: BLE001
            raise HTTPException(status_code=500, detail=f"保存失败: {exc}")
        try:
            load_data_file(dest)
            record_file(str(dest))
            import time
            state.last_heartbeat = time.time()
            return {
                "success": True,
                "total": state.reader.total,
                "current_index": state.progress.current_index,
                "files": state.file_history,
            }
        except Exception as exc:  # noqa: BLE001
            raise HTTPException(status_code=400, detail=str(exc))


@app.post("/api/shutdown")
async def api_shutdown():
    state._shutdown = True
    if state.progress is not None:
        await state.progress.save()
    return {"success": True}


def _build_export_path(file_path: str, statuses: list) -> str:
    """根据源文件路径与导出状态组合生成导出文件名。"""
    base, ext = os.path.splitext(file_path)
    tag = "".join(s[0] for s in statuses)  # pass -> p, pass+reject -> pr
    return f"{base}_export_{tag}{ext}"


@app.post("/api/export")
async def api_export(req: ExportRequest):
    """将标记为指定状态的条目导出为新的文件（默认导出 pass）。

    导出文件保存在源文件同目录下，文件名形如 <原名>_export_p.<原扩展名>。
    返回可被前端直接下载的文件响应。
    """
    # 确定目标文件：显式传入优先，否则用当前已打开文件
    file_path = req.file_path or (str(state.data_path) if state.data_path else None)
    if not file_path or not Path(file_path).is_file():
        raise HTTPException(status_code=400, detail="没有可导出的文件，请先打开文件")

    valid = {"pass", "reject", "skip", "unmarked"}
    statuses = [s for s in (req.statuses or ["pass"]) if s in valid]
    if not statuses:
        raise HTTPException(status_code=400, detail="无效的导出状态")

    # 进度优先取内存中已打开文件的进度（避免未保存遗漏）
    if state.data_path and Path(file_path).resolve() == Path(str(state.data_path)).resolve():
        progress = state.progress
        reader = state.reader
    else:
        indexer = FileIndexer(file_path)
        offsets = indexer.build()
        reader = SampleReader(file_path, offsets)
        progress = ProgressManager(file_path, reader.total)
        progress.load()

    if reader is None or progress is None:
        raise HTTPException(status_code=400, detail="无法读取文件数据")

    # 收集需要导出的条目（保持原顺序）
    total = reader.total
    selected = []
    for i in range(total):
        status = progress.status_of(i)
        if status in statuses:
            selected.append(reader.read(i))

    if not selected:
        raise HTTPException(status_code=404, detail="没有符合导出条件的条目")

    out_path = _build_export_path(file_path, statuses)
    try:
        if file_path.lower().endswith(".jsonl"):
            with open(out_path, "w", encoding="utf-8") as f:
                for entry in selected:
                    f.write(json.dumps(entry, ensure_ascii=False) + "\n")
        else:
            with open(out_path, "w", encoding="utf-8") as f:
                json.dump(selected, f, ensure_ascii=False, indent=2)
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=500, detail=f"写入导出文件失败: {exc}")

    return FileResponse(
        out_path,
        media_type="application/octet-stream",
        filename=os.path.basename(out_path),
    )


# ---- 前端 HTML（从文件读取，保持开发可维护性） ----
_FRONTEND_PATH = Path(__file__).parent / "frontend.html"
try:
    FRONTEND_HTML = _FRONTEND_PATH.read_text(encoding="utf-8")
except FileNotFoundError:
    FRONTEND_HTML = "<h1>frontend.html 缺失</h1>"


if __name__ == "__main__":
    import uvicorn

    from config import HOST, PORT

    uvicorn.run(app, host=HOST, port=PORT)
