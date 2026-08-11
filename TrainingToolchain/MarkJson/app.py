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
from starlette.concurrency import run_in_threadpool

import config
from config import (
    HEARTBEAT_TIMEOUT_SECONDS,
    SAVE_INTERVAL_SECONDS,
)
from file_indexer import FileIndexer
from progress_manager import ProgressManager
from sample_reader import ParquetSampleReader, SampleReader
from utils import LOGGER, process_media_in_value


# ---- 全局状态 ----
class AppState:
    def __init__(self):
        self.data_path: Optional[Path] = None
        self.indexer: Optional[FileIndexer] = None
        # 读取器可能是 JSON/JSONL 索引读取器或 Parquet 行组读取器，二者接口一致
        self.reader: Optional[SampleReader | ParquetSampleReader] = None
        self.progress: Optional[ProgressManager] = None
        self.last_heartbeat: float = 0.0
        self._shutdown = False
        self.file_history: list[str] = []
        # 加载失败时的错误信息（供前端展示具体原因，而非静默回退到空闲界面）
        self.load_error: Optional[str] = None


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


class SearchRequest(BaseModel):
    query: str


class SearchNextRequest(BaseModel):
    query: str
    from_index: int = 0
    forward: bool = True  # True=向后查找, False=向前查找


class OpenRequest(BaseModel):
    file_path: str


class SaveRequest(BaseModel):
    pass


class ExportRequest(BaseModel):
    file_path: Optional[str] = None  # 不传时使用当前已打开文件
    statuses: list[str] = ["pass"]   # 导出哪些状态的条目，默认 pass


# ---- 初始化数据 ----
def _make_reader(file_path: str | Path) -> "SampleReader | ParquetSampleReader":
    """根据文件扩展名构造对应的读取器：.parquet 用 ParquetSampleReader，其余用 JSON/JSONL 索引读取器。"""
    p = Path(file_path)
    if p.suffix.lower() == ".parquet":
        return ParquetSampleReader(p)
    indexer = FileIndexer(p)
    offsets = indexer.build()
    return SampleReader(p, offsets)


def load_data_file(file_path: str | Path) -> None:
    """加载数据文件，构建索引/读取器并初始化进度管理。

    注意：此函数含同步阻塞操作（文件扫描、pyarrow import 等），在 async 路由中
    调用时必须通过 run_in_threadpool 在后台线程执行，避免阻塞事件循环。
    """
    global state
    p = Path(file_path)
    if not p.exists():
        raise FileNotFoundError(f"数据文件不存在: {p}")
    state.data_path = p
    state.reader = _make_reader(p)
    state.progress = ProgressManager(p, state.reader.total)
    state.load_error = None
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
            await run_in_threadpool(load_data_file, config.DEFAULT_DATA_FILE)
            record_file(config.DEFAULT_DATA_FILE)
        except Exception as exc:  # noqa: BLE001
            LOGGER.error("默认数据文件加载失败: %s", exc)
            state.load_error = str(exc)
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
    # 把图片字节/base64 图片转成可前端渲染的标记对象（原数据不变，仅展示层处理）
    data = process_media_in_value(data)
    status = st.progress.status_of(index)
    return {
        "index": index,
        "total": st.reader.total,
        "data": data,
        "status": status,
    }


def _extract_value_strings(obj, out: list) -> None:
    """递归提取所有叶子值（不含键名）的字符串表示，用于关键字匹配。"""
    if obj is None:
        out.append("null")
    elif isinstance(obj, bool):
        out.append("true" if obj else "false")
    elif isinstance(obj, (int, float)):
        out.append(str(obj))
    elif isinstance(obj, bytes):
        # 二进制（含图片字节）不参与文本搜索
        return
    elif isinstance(obj, str):
        out.append(obj)
    elif isinstance(obj, list):
        for item in obj:
            _extract_value_strings(item, out)
    elif isinstance(obj, dict):
        for v in obj.values():
            _extract_value_strings(v, out)


# ---- 路由 ----
@app.get("/", response_class=HTMLResponse)
async def index():
    return HTMLResponse(FRONTEND_HTML)


@app.get("/api/status")
async def api_status():
    # 未加载数据时不抛异常，而是返回空状态 + load_error，让前端能展示具体失败原因
    if state.reader is None or state.progress is None:
        return {
            "data_file": str(state.data_path) if state.data_path else None,
            "total": 0,
            "current_index": 0,
            "load_error": state.load_error,
            "total_samples": 0,
            "pass_count": 0,
            "reject_count": 0,
            "skip_count": 0,
            "marked_count": 0,
        }
    return {
        "data_file": str(state.data_path),
        "total": state.reader.total,
        "current_index": state.progress.current_index,
        "load_error": state.load_error,
        **state.progress.to_dict(),
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


@app.post("/api/search")
async def api_search(req: SearchRequest):
    """按关键字搜索样本，仅匹配值内容（不匹配键名），空格分词做 AND 查找。

    返回所有匹配样本的索引列表。
    """
    st = _require_data()
    keywords = req.query.split()
    if not keywords:
        return {"indices": [], "total": 0, "query": req.query}

    def _do_search() -> list[int]:
        kws_lower = [k.lower() for k in keywords]
        matches: list[int] = []
        for i in range(st.reader.total):
            data = st.reader.read(i)
            # 跳过解析失败的样本
            if isinstance(data, dict) and data.get("__parse_error__"):
                continue
            values: list[str] = []
            _extract_value_strings(data, values)
            blob = "\n".join(values).lower()
            if all(kw in blob for kw in kws_lower):
                matches.append(i)
        return matches

    matches = await run_in_threadpool(_do_search)
    return {"indices": matches, "total": len(matches), "query": req.query}


@app.post("/api/search_next")
async def api_search_next(req: SearchNextRequest):
    """快速查找：从 from_index 之后（forward=True）或之前（forward=False）查找第一个匹配样本。

    不统计全部匹配项，命中即返回，适合海量数据。返回 index（-1 表示未找到）。
    """
    st = _require_data()
    keywords = req.query.split()
    if not keywords:
        return {"index": -1, "query": req.query}

    def _do_find() -> int:
        kws_lower = [k.lower() for k in keywords]
        total = st.reader.total
        start = req.from_index
        if req.forward:
            rng = range(start + 1, total)
        else:
            rng = range(start - 1, -1, -1)
        for i in rng:
            data = st.reader.read(i)
            if isinstance(data, dict) and data.get("__parse_error__"):
                continue
            values: list[str] = []
            _extract_value_strings(data, values)
            blob = "\n".join(values).lower()
            if all(kw in blob for kw in kws_lower):
                return i
        return -1

    idx = await run_in_threadpool(_do_find)
    return {"index": idx, "query": req.query}


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
            await run_in_threadpool(load_data_file, req.file_path)
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
            or file.filename.lower().endswith(".parquet")
        ):
            raise HTTPException(status_code=400, detail="仅支持 .json / .jsonl / .parquet 文件")
        # 避免文件名冲突：保留原名（同目录内覆盖）
        dest = _UPLOAD_DIR / file.filename
        try:
            out_fh = dest.open("wb")
            try:
                await run_in_threadpool(shutil.copyfileobj, file.file, out_fh)
            finally:
                out_fh.close()
        except Exception as exc:  # noqa: BLE001
            raise HTTPException(status_code=500, detail=f"保存失败: {exc}")
        try:
            await run_in_threadpool(load_data_file, dest)
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
        reader = _make_reader(file_path)
        progress = ProgressManager(file_path, reader.total)
        progress.load()

    if reader is None or progress is None:
        raise HTTPException(status_code=400, detail="无法读取文件数据")

    # 收集需要导出的条目（保持原顺序，保留原始数据用于回写）
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
        lower = file_path.lower()
        if lower.endswith(".parquet"):
            import pyarrow as pa
            import pyarrow.parquet as pq

            table = pa.Table.from_pylist(selected)
            pq.write_table(table, out_path)
        elif lower.endswith(".jsonl"):
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
