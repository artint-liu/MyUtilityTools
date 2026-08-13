"""FastAPI 应用：路由 + 后台定时存盘 + 内嵌前端页面。"""
from __future__ import annotations

import asyncio
import json
import os
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, File, HTTPException, Request, UploadFile
from fastapi.responses import FileResponse, HTMLResponse, StreamingResponse
from pydantic import BaseModel
from starlette.concurrency import run_in_threadpool

import config
from ai_predictor import AIPredictor
from config import (
    HEARTBEAT_TIMEOUT_SECONDS,
    SAVE_INTERVAL_SECONDS,
)
from file_indexer import FileIndexer
from progress_manager import ProgressManager
from sample_reader import ParquetSampleReader, SampleReader
from utils import LOGGER, atomic_write_json, process_media_in_value


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
        # ---- AI 预测状态 ----
        self.ai_predictor: Optional[AIPredictor] = None
        # ai_predictions: {index: {label, confidence, reason}} —— 低置信度建议（未写入 marks）
        self.ai_predictions: dict[int, dict] = {}
        self.ai_task: Optional[asyncio.Task] = None
        self.ai_task_progress: dict = {
            "running": False,
            "done": 0,
            "total": 0,
            "errors": 0,
            "auto_accepted": 0,
        }
        self.ai_cancel: Optional[asyncio.Event] = None


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


# ---- AI 凭证持久化（非本机地址才保存 host → api_key） ----
_CREDENTIALS_PATH = Path(__file__).parent / ".ai_credentials.json"

# 视为本机的 host
_LOCAL_HOSTS = {"localhost", "127.0.0.1", "0.0.0.0", "::1", ""}


def _extract_host(api_base: str) -> str | None:
    """从 API Base URL 中提取 host（小写）。解析失败返回 None。"""
    if not api_base:
        return None
    try:
        from urllib.parse import urlparse
        # 容错：缺 scheme 时补一个，否则 urlparse 会把 host 当 path
        if "://" not in api_base:
            parsed = urlparse("//" + api_base)
        else:
            parsed = urlparse(api_base)
        return (parsed.hostname or "").lower()
    except Exception:  # noqa: BLE001
        return None


def _is_local_host(host: str | None) -> bool:
    """判断 host 是否为本机地址。"""
    if not host:
        return True
    return host.lower() in _LOCAL_HOSTS


def _load_cred_store() -> dict:
    """加载持久化凭证存储。

    返回 {"last_api_base": str, "credentials": {host: api_key}}。
    """
    if not _CREDENTIALS_PATH.exists():
        return {"last_api_base": "", "credentials": {}}
    try:
        data = json.loads(_CREDENTIALS_PATH.read_text(encoding="utf-8"))
        creds = data.get("credentials", {})
        if not isinstance(creds, dict):
            creds = {}
        last_base = data.get("last_api_base", "")
        if not isinstance(last_base, str):
            last_base = ""
        return {"last_api_base": last_base, "credentials": creds}
    except Exception:  # noqa: BLE001
        return {"last_api_base": "", "credentials": {}}


def _save_cred_store(last_api_base: str, creds: dict[str, str]) -> None:
    """持久化凭证存储到磁盘。"""
    try:
        atomic_write_json(
            _CREDENTIALS_PATH,
            {"last_api_base": last_api_base, "credentials": creds},
        )
    except Exception as exc:  # noqa: BLE001
        LOGGER.warning("保存 AI 凭证失败: %s", exc)


def _store_credential(api_base: str, api_key: str) -> None:
    """保存非本机地址的 Base URL 与 API Key。

    非本机地址：持久化 last_api_base 与 host → api_key；
    本机地址：清空 last_api_base（下次启动回到默认 localhost）。
    """
    host = _extract_host(api_base)
    store = _load_cred_store()
    creds = store["credentials"]
    if not host or _is_local_host(host):
        # 本机地址：清空持久化的 last_api_base，回到默认
        _save_cred_store("", creds)
        return
    if api_key:
        creds[host] = api_key
    else:
        creds.pop(host, None)
    _save_cred_store(api_base, creds)


def _lookup_credential(api_base: str) -> str | None:
    """根据 api_base 的 host 查找已保存的 api_key（非本机才有）。"""
    host = _extract_host(api_base)
    if not host or _is_local_host(host):
        return None
    return _load_cred_store()["credentials"].get(host)


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


class FilterRequest(BaseModel):
    # 按标记状态筛选：pass / reject / skip / pass_ai / reject_ai / unmarked
    # 可多选，空列表表示「全部」。仅基于内存 marks 遍历，不读取文件内容。
    statuses: list[str] = ["pass"]
    # 可选关键字：在状态筛选结果内再做「值内容」AND 匹配（仅读取命中集内的样本内容）
    query: str = ""
    # 排序：original=按文件原始顺序，index 升序
    order: str = "original"


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
    # 加载 AI 预测缓存
    state.ai_predictions = _load_ai_predictions(p)
    # 重置 AI 任务状态
    state.ai_task_progress = {
        "running": False, "done": 0, "total": 0, "errors": 0, "auto_accepted": 0,
    }
    LOGGER.info("数据文件已加载: %s, 共 %d 条样本", p, state.reader.total)


def _ai_predictions_path(data_path: Path) -> Path:
    """AI 预测缓存的持久化路径：<数据文件>.ai_predictions.json"""
    return data_path.with_suffix(data_path.suffix + ".ai_predictions.json")


def _load_ai_predictions(data_path: Path) -> dict[int, dict]:
    """从磁盘加载 AI 预测缓存。"""
    p = _ai_predictions_path(data_path)
    if not p.exists():
        return {}
    try:
        data = json.loads(p.read_text(encoding="utf-8"))
        return {int(k): v for k, v in data.items()}
    except Exception as exc:  # noqa: BLE001
        LOGGER.warning("AI 预测缓存损坏，忽略: %s", exc)
        return {}


def _save_ai_predictions() -> None:
    """持久化 AI 预测缓存到磁盘。"""
    if state.data_path is None:
        return
    p = _ai_predictions_path(state.data_path)
    payload = {str(k): v for k, v in state.ai_predictions.items()}
    try:
        atomic_write_json(p, payload)
    except Exception as exc:  # noqa: BLE001
        LOGGER.warning("保存 AI 预测缓存失败: %s", exc)


def _log_ai_failure(index: int, reason: str, raw: str) -> None:
    """把解析失败的 LLM 原始返回追加写入日志文件，方便诊断。

    文件位置：<数据文件>.ai_failures.log（与数据文件同目录）。
    """
    if state.data_path is None:
        return
    log_path = state.data_path.with_suffix(
        state.data_path.suffix + ".ai_failures.log"
    )
    import time

    line = (
        f"\n{'=' * 60}\n"
        f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] index={index}\n"
        f"reason: {reason}\n"
        f"raw (前800字符):\n{raw[:800] if raw else '(空)'}\n"
    )
    try:
        with open(log_path, "a", encoding="utf-8") as f:
            f.write(line)
    except Exception as exc:  # noqa: BLE001
        LOGGER.warning("写入 AI 失败日志失败: %s", exc)


def _get_predictor() -> AIPredictor:
    """获取（惰性创建）AI 预测器实例。"""
    if state.ai_predictor is None:
        state.ai_predictor = AIPredictor()
    return state.ai_predictor


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
    # 启动时恢复上次保存的非本机 Base URL 与 API Key
    store = _load_cred_store()
    saved_base = store["last_api_base"]
    if saved_base and not _is_local_host(_extract_host(saved_base)):
        config.AI_API_BASE = saved_base
        LOGGER.info("已恢复上次使用的 API Base: %s", saved_base)
        # 若未显式配置 key，则用该 host 已保存的凭证
        if not config.AI_API_KEY or config.AI_API_KEY == "lm-studio":
            saved_key = store["credentials"].get(_extract_host(saved_base))
            if saved_key:
                config.AI_API_KEY = saved_key
                LOGGER.info("已从凭证库加载 %s 的 API Key", _extract_host(saved_base))
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
    # ---- 关闭流程（带超时保护，避免卡死） ----
    state._shutdown = True
    # 取消批量 AI 任务（如有）
    if state.ai_cancel is not None:
        state.ai_cancel.set()
    if state.ai_task is not None and not state.ai_task.done():
        state.ai_task.cancel()
        try:
            await asyncio.wait_for(state.ai_task, timeout=2.0)
        except (asyncio.CancelledError, asyncio.TimeoutError):
            pass
    # 取消后台存盘任务并等待退出
    saver.cancel()
    try:
        await asyncio.wait_for(saver, timeout=2.0)
    except (asyncio.CancelledError, asyncio.TimeoutError):
        pass
    # 关闭时存盘（失败不阻塞退出）
    if state.progress is not None:
        try:
            await state.progress.save()
            LOGGER.info("应用关闭，已存盘")
        except Exception as exc:  # noqa: BLE001
            LOGGER.warning("关闭时存盘失败: %s", exc)


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
    payload = {
        "index": index,
        "total": st.reader.total,
        "data": data,
        "status": status,
    }
    # AI 标记附带置信度
    if status in ("pass_ai", "reject_ai"):
        payload["ai_confidence"] = st.progress.confidence_of(index)
    # 低置信度建议（未写入 marks 的预测）
    if index in st.ai_predictions:
        payload["ai_prediction"] = st.ai_predictions[index]
    return payload


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
def _collect_unmarked_from_current(progress: ProgressManager, total: int, count: int | None) -> list[int]:
    """从当前索引开始向后（到末尾后从头继续）收集未标记索引。

    确保「预测多条」与「预测1条」行为一致：都从当前位置开始。
    count=None 表示全部，否则取前 N 条。
    """
    cur = progress.current_index
    if total <= 0:
        return []
    # 构造从当前索引开始的顺序序列：cur, cur+1, ..., total-1, 0, 1, ..., cur-1
    ordered = [(i + cur) % total for i in range(total)]
    unmarked = [i for i in ordered if progress.status_of(i) == "unmarked"]
    return unmarked if count is None else unmarked[:count]


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
            "pass_ai_count": 0,
            "reject_ai_count": 0,
            "marked_count": 0,
            "ai_prediction_count": len(state.ai_predictions),
        }
    return {
        "data_file": str(state.data_path),
        "total": state.reader.total,
        "current_index": state.progress.current_index,
        "load_error": state.load_error,
        "ai_prediction_count": len(state.ai_predictions),
        **state.progress.to_dict(),
    }


@app.get("/api/sample/{index}")
async def api_sample(index: int):
    # 同步后端当前位置：前端浏览/跳转都走此端点，确保 current_index 与
    # 前端显示一致（批量预测依赖 current_index 作为起始位置）
    st = _require_data()
    if index < 0 or index >= st.reader.total:
        raise HTTPException(status_code=404, detail="样本索引越界")
    st.progress.set_current(index)
    return _sample_payload(index)


@app.post("/api/mark")
async def api_mark(req: MarkRequest):
    st = _require_data()
    if req.index < 0 or req.index >= st.reader.total:
        raise HTTPException(status_code=404, detail="样本索引越界")
    st.progress.mark(req.index, req.status)
    # 人工标记后清除该索引的 AI 建议（如有）
    if req.index in st.ai_predictions:
        st.ai_predictions.pop(req.index, None)
        _save_ai_predictions()
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


@app.post("/api/filter")
async def api_filter(req: FilterRequest):
    """按标记状态筛选样本（快速浏览）。

    仅基于内存 progress.marks 遍历，不读取任何样本文件内容，因此
    即使样本量达千万级别，筛选本身也只需毫秒级。

    - statuses 为空表示「全部已标记」；传入具体状态则只返回对应条目。
      unmarked 表示「尚未标记」的条目。
    - query 为空时只返回索引列表（indices）；
      仅当传入 query 时，才在筛选集内读取样本内容做值内容 AND 匹配
      （此时读取量=筛选集大小，而非全量，避免海量数据卡死）。
    """
    st = _require_data()
    pm = st.progress
    total = st.reader.total

    # 1) 基于内存 marks 计算筛选集（不读文件）
    selected: list[int] = []
    want_unmarked = "unmarked" in req.statuses and not req.statuses
    status_set = set(req.statuses)

    def _match_status(idx: int) -> bool:
        if not req.statuses:
            # 空列表 = 全部已标记条目
            return idx in pm.marks
        return pm.marks.get(idx) in status_set

    # 原始顺序遍历（order 目前仅支持 original）
    for i in range(total):
        m = pm.marks.get(i)
        if not req.statuses:
            if m is not None:
                selected.append(i)
        elif m in status_set:
            selected.append(i)

    # 2) 可选的二次关键字过滤（仅读取筛选集内容）
    if req.query.strip():
        keywords = req.query.split()
        kws_lower = [k.lower() for k in keywords]

        def _do_filter() -> list[int]:
            out: list[int] = []
            for i in selected:
                data = st.reader.read(i)
                if isinstance(data, dict) and data.get("__parse_error__"):
                    continue
                values: list[str] = []
                _extract_value_strings(data, values)
                blob = "\n".join(values).lower()
                if all(kw in blob for kw in kws_lower):
                    out.append(i)
            return out

        selected = await run_in_threadpool(_do_filter)

    return {"indices": selected, "total": len(selected),
            "statuses": req.statuses, "query": req.query}


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

    valid = {"pass", "reject", "skip", "unmarked", "pass_ai", "reject_ai"}
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


# ---- AI 预测路由 ----
class AIPredictRequest(BaseModel):
    index: int


class AIAcceptRequest(BaseModel):
    index: int
    label: Optional[str] = None  # 指定采纳为 pass/reject，默认用建议的 label


class AIAcceptAllRequest(BaseModel):
    threshold: Optional[float] = None  # 默认用配置阈值


class AIPromoteRequest(BaseModel):
    index: int  # 仅提升指定条为人工标记


class AIConfigRequest(BaseModel):
    api_base: Optional[str] = None
    api_key: Optional[str] = None
    model: Optional[str] = None
    vision_enabled: Optional[bool] = None
    confidence_threshold: Optional[float] = None


@app.get("/api/ai/config")
async def ai_get_config():
    # 判断当前 Base URL 是否非本机（前端据此决定是否提示已保存凭证）
    host = _extract_host(config.AI_API_BASE)
    is_remote = bool(host) and not _is_local_host(host)
    return {
        "backend": config.AI_BACKEND,
        "api_base": config.AI_API_BASE,
        "api_key": config.AI_API_KEY,
        "is_remote": is_remote,
        "model": config.AI_MODEL,
        "vision_enabled": config.AI_VISION_ENABLED,
        "confidence_threshold": config.AI_CONFIDENCE_THRESHOLD,
        "max_examples_per_class": config.AI_MAX_EXAMPLES_PER_CLASS,
        "has_key": bool(config.AI_API_KEY),
    }


@app.post("/api/ai/config")
async def ai_set_config(req: AIConfigRequest):
    """运行时更新 AI 配置。

    非本机 Base URL 与 API Key 会持久化到 .ai_credentials.json，下次启动自动恢复；
    本机地址不持久化（回到默认 localhost）。
    """
    if req.api_base is not None:
        config.AI_API_BASE = req.api_base
    if req.api_key is not None:
        config.AI_API_KEY = req.api_key
    if req.model is not None:
        config.AI_MODEL = req.model
    if req.vision_enabled is not None:
        config.AI_VISION_ENABLED = req.vision_enabled
    if req.confidence_threshold is not None:
        config.AI_CONFIDENCE_THRESHOLD = max(0.0, min(1.0, req.confidence_threshold))
    # 持久化 Base URL 与凭证：非本机地址才保存
    if req.api_base is not None:
        _store_credential(config.AI_API_BASE, config.AI_API_KEY)
    elif req.api_key is not None:
        # Base URL 未变但 key 变了：更新该 host 的凭证
        _store_credential(config.AI_API_BASE, req.api_key)
    # 配置变更后重建预测器
    state.ai_predictor = None
    return {"success": True, **(await ai_get_config())}


@app.post("/api/ai/test")
async def ai_test_connection():
    predictor = _get_predictor()
    ok, msg = await predictor.test_connection()
    return {"success": ok, "message": msg}


@app.get("/api/ai/examples")
async def ai_examples():
    """预览将作为 few-shot 的已标注样本（精简后）。"""
    st = _require_data()
    predictor = _get_predictor()
    examples = await run_in_threadpool(
        predictor.collect_examples, st.progress, st.reader
    )
    # 精简展示，避免响应过大
    compacted = {
        label: [predictor._compact_sample(s) for s in samples]
        for label, samples in examples.items()
    }
    return {
        "pass_count": len(examples["pass"]),
        "reject_count": len(examples["reject"]),
        "examples": compacted,
    }


@app.post("/api/ai/predict")
async def ai_predict(req: AIPredictRequest):
    """预测单条样本。"""
    st = _require_data()
    if req.index < 0 or req.index >= st.reader.total:
        raise HTTPException(status_code=404, detail="样本索引越界")
    predictor = _get_predictor()
    # 收集 few-shot 示例
    examples = await run_in_threadpool(
        predictor.collect_examples, st.progress, st.reader
    )
    if not examples["pass"] or not examples["reject"]:
        raise HTTPException(
            status_code=400,
            detail="人工标注样本不足，需要至少 1 条 pass 和 1 条 reject 示例",
        )
    predictor.set_examples(examples)
    data = st.reader.read(req.index)
    result = await predictor.predict_one(data)
    # 调用失败（网络/HTTP/解析错误）：不作为 pass/reject 处理，直接返回错误
    if result.get("error"):
        _log_ai_failure(req.index, result.get("reason", "未知错误"), result.get("raw", ""))
        log_file = (
            str(state.data_path.with_suffix(state.data_path.suffix + ".ai_failures.log"))
            if state.data_path else ""
        )
        return {
            "success": False,
            "error": True,
            "reason": result.get("reason", "未知错误"),
            "raw": result.get("raw", ""),
            "log_file": log_file,
        }
    current_status = st.progress.status_of(req.index)
    # 高置信度且当前无人工标记（unmarked 或仅 AI 标记）→ 自动采纳
    # 已有人工标记（pass/reject/skip）不覆盖，仅返回建议
    auto = False
    if result["confidence"] >= config.AI_CONFIDENCE_THRESHOLD and current_status not in ("pass", "reject", "skip"):
        ai_status = f"{result['label']}_ai"
        st.progress.mark(req.index, ai_status, confidence=result["confidence"])
        auto = True
    else:
        # 低置信度或已有人工标记：存建议（不覆盖 marks）
        state.ai_predictions[req.index] = result
        _save_ai_predictions()
    return {"success": True, "result": result, "auto_accepted": auto}


@app.post("/api/ai/predict_stream")
async def ai_predict_stream(req: AIPredictRequest):
    """流式预测单条样本，以 SSE 推送推理过程给前端实时显示。

    事件流：
    - event: thinking  data: {text}        思维链片段（reasoning 模型）
    - event: content   data: {text}        回答内容片段
    - event: result    data: {result, raw, reasoning}  解析后的最终结果
    - event: done      data: {auto_accepted, result} 或 {error, reason, raw, log_file}
    - event: error     data: {reason}      调用失败（连接/超时等）
    """
    st = _require_data()
    if req.index < 0 or req.index >= st.reader.total:
        raise HTTPException(status_code=404, detail="样本索引越界")
    predictor = _get_predictor()
    examples = await run_in_threadpool(
        predictor.collect_examples, st.progress, st.reader
    )
    if not examples["pass"] or not examples["reject"]:
        raise HTTPException(
            status_code=400,
            detail="人工标注样本不足，需要至少 1 条 pass 和 1 条 reject 示例",
        )
    predictor.set_examples(examples)
    data = st.reader.read(req.index)

    def _sse(event: str, payload: dict) -> str:
        return f"event: {event}\ndata: {json.dumps(payload, ensure_ascii=False)}\n\n"

    async def _stream():
        try:
            final_result = None
            full_raw = ""
            full_reasoning = ""
            async for evt in predictor.predict_one_stream(data):
                evt_type = evt["type"]
                if evt_type == "thinking":
                    full_reasoning += evt["text"]
                    yield _sse("thinking", {"text": evt["text"]})
                elif evt_type == "content":
                    full_raw += evt["text"]
                    yield _sse("content", {"text": evt["text"]})
                elif evt_type == "result":
                    final_result = evt["result"]
                    yield _sse("result", {
                        "result": final_result,
                        "raw": evt["raw"],
                        "reasoning": evt["reasoning"],
                    })
                elif evt_type == "error":
                    yield _sse("error", {"reason": evt["reason"]})
                    return

            # 处理最终结果：自动采纳 / 存建议
            if final_result and not final_result.get("error"):
                current_status = st.progress.status_of(req.index)
                auto = False
                if final_result["confidence"] >= config.AI_CONFIDENCE_THRESHOLD and current_status not in ("pass", "reject", "skip"):
                    ai_status = f"{final_result['label']}_ai"
                    st.progress.mark(req.index, ai_status, confidence=final_result["confidence"])
                    auto = True
                else:
                    state.ai_predictions[req.index] = final_result
                    _save_ai_predictions()
                yield _sse("done", {"auto_accepted": auto, "result": final_result})
            else:
                reason = final_result.get("reason", "未知错误") if final_result else "未知错误"
                _log_ai_failure(req.index, reason, full_raw or full_reasoning)
                log_file = (
                    str(state.data_path.with_suffix(state.data_path.suffix + ".ai_failures.log"))
                    if state.data_path else ""
                )
                yield _sse("done", {
                    "error": True,
                    "reason": reason,
                    "raw": full_raw or full_reasoning,
                    "log_file": log_file,
                })
        except Exception as exc:  # noqa: BLE001
            LOGGER.error("流式预测异常: %s", exc)
            yield _sse("error", {"reason": str(exc)})

    return StreamingResponse(
        _stream(),
        media_type="text/event-stream",
        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
    )


class AIPredictAllRequest(BaseModel):
    count: Optional[int] = None  # None = 全部未标记，数字 = 从当前索引起的 N 条未标记


@app.post("/api/ai/predict_all")
async def ai_predict_all(req: Optional[AIPredictAllRequest] = None):
    """批量预测未标记样本（后台任务）。

    count: None → 从当前索引起全部未标记；数字 → 从当前索引起的 N 条未标记样本。
    与「预测1条」逻辑一致：都从当前位置开始，逐条排队执行。
    """
    if req is None:
        req = AIPredictAllRequest()
    st = _require_data()
    if state.ai_task_progress.get("running"):
        raise HTTPException(status_code=409, detail="已有批量预测任务在运行")
    predictor = _get_predictor()
    # 收集 few-shot 示例
    examples = await run_in_threadpool(
        predictor.collect_examples, st.progress, st.reader
    )
    if not examples["pass"] or not examples["reject"]:
        raise HTTPException(
            status_code=400,
            detail="人工标注样本不足，需要至少 1 条 pass 和 1 条 reject 示例",
        )
    predictor.set_examples(examples)
    # 从当前索引开始收集未标记索引（向后到末尾后从头继续）
    indices = _collect_unmarked_from_current(st.progress, st.reader.total, req.count)
    if not indices:
        return {"success": True, "message": "没有未标记的样本", "total": 0}

    cancel_event = asyncio.Event()
    state.ai_cancel = cancel_event
    state.ai_task_progress = {
        "running": True,
        "done": 0,
        "total": len(indices),
        "errors": 0,
        "auto_accepted": 0,
    }

    async def _run():
        try:
            done = 0
            errors = 0
            auto_accepted = 0

            def _on_progress(idx: int, result: dict, auto: bool) -> None:
                nonlocal done, errors, auto_accepted
                done += 1
                # 立即处理结果：写 marks 或存建议，让前端轮询能实时看到变化
                if result.get("error"):
                    errors += 1
                    _log_ai_failure(idx, result.get("reason", ""), result.get("raw", ""))
                else:
                    # 与「预测1条」完全一致：高置信度且非人工标记 → 自动采纳；
                    # 否则存为低置信度建议（不覆盖 pass/reject/skip）
                    current_status = st.progress.status_of(idx)
                    if auto and current_status not in ("pass", "reject", "skip"):
                        ai_status = f"{result['label']}_ai"
                        st.progress.mark(idx, ai_status, confidence=result["confidence"])
                        auto_accepted += 1
                    else:
                        state.ai_predictions[idx] = result
                state.ai_task_progress["done"] = done
                state.ai_task_progress["auto_accepted"] = auto_accepted
                state.ai_task_progress["errors"] = errors

            await predictor.predict_batch(
                indices, st.reader, on_progress=_on_progress, cancel_event=cancel_event
            )
            _save_ai_predictions()
            await st.progress.save()
        except Exception as exc:  # noqa: BLE001
            LOGGER.error("批量预测任务异常: %s", exc)
        finally:
            state.ai_task_progress["running"] = False
            state.ai_cancel = None

    state.ai_task = asyncio.create_task(_run())
    return {"success": True, "total": len(indices)}


@app.post("/api/ai/predict_batch_stream")
async def ai_predict_batch_stream(req: Optional[AIPredictAllRequest] = None):
    """流式批量预测，以 SSE 推送每条样本的推理过程给前端实时显示。

    从当前索引开始，逐条排队执行（与「预测1条」逻辑一致，仅是多条队列）。
    顺序执行，适合本地 LLM。事件流：
    - event: item_start  data: {index, done, total}        开始预测某条
    - event: thinking     data: {text}                      思维链片段
    - event: content      data: {text}                      回答内容片段
    - event: item_done    data: {index, result, auto_accepted, done, total}  某条成功
    - event: item_error   data: {reason, index, done, total}  某条失败（不中断）
    - event: done         data: {total, auto_accepted, errors, cancelled}    全部完成
    """
    if req is None:
        req = AIPredictAllRequest()
    st = _require_data()
    if state.ai_task_progress.get("running"):
        raise HTTPException(status_code=409, detail="已有批量预测任务在运行")
    predictor = _get_predictor()
    examples = await run_in_threadpool(
        predictor.collect_examples, st.progress, st.reader
    )
    if not examples["pass"] or not examples["reject"]:
        raise HTTPException(
            status_code=400,
            detail="人工标注样本不足，需要至少 1 条 pass 和 1 条 reject 示例",
        )
    predictor.set_examples(examples)
    # 从当前索引开始收集未标记索引（向后到末尾后从头继续）
    indices = _collect_unmarked_from_current(st.progress, st.reader.total, req.count)
    total = len(indices)

    cancel_event = asyncio.Event()
    state.ai_cancel = cancel_event
    state.ai_task_progress = {
        "running": True, "done": 0, "total": total,
        "errors": 0, "auto_accepted": 0,
    }

    def _sse(event: str, payload: dict) -> str:
        return f"event: {event}\ndata: {json.dumps(payload, ensure_ascii=False)}\n\n"

    async def _stream():
        try:
            if total == 0:
                yield _sse("done", {"total": 0, "auto_accepted": 0, "errors": 0, "cancelled": False})
                return
            done = 0
            errors = 0
            auto_accepted = 0
            for idx in indices:
                if cancel_event.is_set():
                    break
                data = st.reader.read(idx)
                yield _sse("item_start", {"index": idx, "done": done, "total": total})
                final_result = None
                had_error = False
                try:
                    async for evt in predictor.predict_one_stream(data):
                        if cancel_event.is_set():
                            had_error = True
                            break
                        evt_type = evt["type"]
                        if evt_type == "thinking":
                            yield _sse("thinking", {"text": evt["text"]})
                        elif evt_type == "content":
                            yield _sse("content", {"text": evt["text"]})
                        elif evt_type == "result":
                            final_result = evt["result"]
                        elif evt_type == "error":
                            had_error = True
                            errors += 1
                            done += 1
                            _log_ai_failure(idx, evt.get("reason", ""), "")
                            state.ai_task_progress["errors"] = errors
                            state.ai_task_progress["done"] = done
                            yield _sse("item_error", {
                                "reason": evt.get("reason", "未知错误"),
                                "index": idx, "done": done, "total": total,
                            })
                            break
                except Exception as exc:  # noqa: BLE001
                    had_error = True
                    errors += 1
                    done += 1
                    _log_ai_failure(idx, str(exc), "")
                    state.ai_task_progress["errors"] = errors
                    state.ai_task_progress["done"] = done
                    yield _sse("item_error", {
                        "reason": str(exc), "index": idx, "done": done, "total": total,
                    })
                if had_error:
                    continue
                if final_result and not final_result.get("error"):
                    current_status = st.progress.status_of(idx)
                    auto = False
                    if final_result["confidence"] >= config.AI_CONFIDENCE_THRESHOLD and current_status not in ("pass", "reject", "skip"):
                        ai_status = f"{final_result['label']}_ai"
                        st.progress.mark(idx, ai_status, confidence=final_result["confidence"])
                        auto = True
                        auto_accepted += 1
                    else:
                        state.ai_predictions[idx] = final_result
                    done += 1
                    state.ai_task_progress["done"] = done
                    state.ai_task_progress["auto_accepted"] = auto_accepted
                    yield _sse("item_done", {
                        "index": idx, "result": final_result,
                        "auto_accepted": auto, "done": done, "total": total,
                    })
                elif final_result:
                    errors += 1
                    done += 1
                    _log_ai_failure(idx, final_result.get("reason", ""), final_result.get("raw", ""))
                    state.ai_task_progress["errors"] = errors
                    state.ai_task_progress["done"] = done
                    yield _sse("item_error", {
                        "reason": final_result.get("reason", "未知错误"),
                        "index": idx, "done": done, "total": total,
                    })
            _save_ai_predictions()
            await st.progress.save()
            yield _sse("done", {
                "total": total, "auto_accepted": auto_accepted,
                "errors": errors, "cancelled": cancel_event.is_set(),
            })
        except Exception as exc:  # noqa: BLE001
            LOGGER.error("流式批量预测异常: %s", exc)
            yield _sse("error", {"reason": str(exc)})
        finally:
            state.ai_task_progress["running"] = False
            state.ai_cancel = None

    return StreamingResponse(
        _stream(),
        media_type="text/event-stream",
        headers={"Cache-Control": "no-cache", "X-Accel-Buffering": "no"},
    )


@app.get("/api/ai/predict_status")
async def ai_predict_status():
    """查询批量预测进度。"""
    return state.ai_task_progress


@app.get("/api/ai/predictions")
async def ai_predictions():
    """获取所有低置信度建议（未写入 marks 的预测）。"""
    return {"predictions": state.ai_predictions, "count": len(state.ai_predictions)}


@app.post("/api/ai/accept")
async def ai_accept(req: AIAcceptRequest):
    """采纳某条 AI 建议，写入 marks（转为人工标记）。"""
    st = _require_data()
    if req.index not in state.ai_predictions:
        raise HTTPException(status_code=404, detail="该样本没有 AI 建议")
    pred = state.ai_predictions.pop(req.index)
    label = req.label or pred["label"]
    if label not in ("pass", "reject"):
        raise HTTPException(status_code=400, detail="label 必须为 pass 或 reject")
    st.progress.mark(req.index, label)
    _save_ai_predictions()
    return {"success": True, "index": req.index, "status": label}


@app.post("/api/ai/accept_all")
async def ai_accept_all(req: AIAcceptAllRequest):
    """批量采纳高置信度建议。threshold 默认用配置阈值。"""
    st = _require_data()
    threshold = req.threshold if req.threshold is not None else config.AI_CONFIDENCE_THRESHOLD
    accepted = []
    for idx in list(state.ai_predictions.keys()):
        pred = state.ai_predictions[idx]
        if pred.get("confidence", 0.0) >= threshold:
            label = pred["label"]
            if label in ("pass", "reject"):
                st.progress.mark(idx, label)
                state.ai_predictions.pop(idx)
                accepted.append(idx)
    _save_ai_predictions()
    return {"success": True, "accepted_count": len(accepted), "indices": accepted}


@app.post("/api/ai/clear")
async def ai_clear():
    """清除所有 AI 预测：pass_ai/reject_ai → unmarked，清空 ai_predictions。"""
    st = _require_data()
    cleared_marks = 0
    for idx in list(st.progress.marks.keys()):
        if st.progress.status_of(idx) in ("pass_ai", "reject_ai"):
            st.progress.mark(idx, "unmarked")
            cleared_marks += 1
    cleared_preds = len(state.ai_predictions)
    state.ai_predictions.clear()
    _save_ai_predictions()
    await st.progress.save()
    return {
        "success": True,
        "cleared_marks": cleared_marks,
        "cleared_predictions": cleared_preds,
    }


@app.post("/api/ai/cancel")
async def ai_cancel():
    """取消进行中的批量预测。"""
    if state.ai_cancel is not None:
        state.ai_cancel.set()
        return {"success": True, "message": "已请求取消"}
    return {"success": False, "message": "没有进行中的任务"}


@app.post("/api/ai/open_log")
async def ai_open_log():
    """用系统默认应用打开 AI 失败日志文件。"""
    import os
    import subprocess
    import sys

    if state.data_path is None:
        raise HTTPException(status_code=400, detail="尚未加载数据文件")
    log_path = state.data_path.with_suffix(
        state.data_path.suffix + ".ai_failures.log"
    )
    if not log_path.exists():
        raise HTTPException(status_code=404, detail="日志文件不存在（尚未产生失败记录）")
    try:
        if sys.platform == "win32":
            os.startfile(str(log_path))
        elif sys.platform == "darwin":
            subprocess.Popen(["open", str(log_path)])
        else:
            subprocess.Popen(["xdg-open", str(log_path)])
        return {"success": True}
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=500, detail=f"打开失败: {exc}")


@app.post("/api/ai/promote")
async def ai_promote(req: AIPromoteRequest):
    """将指定条 AI 标记提升为人工标记：pass_ai → pass, reject_ai → reject。"""
    st = _require_data()
    status = st.progress.status_of(req.index)
    if status == "pass_ai":
        st.progress.mark(req.index, "pass")
        promoted = 1
    elif status == "reject_ai":
        st.progress.mark(req.index, "reject")
        promoted = 1
    else:
        promoted = 0
    await st.progress.save()
    return {"success": True, "promoted_count": promoted, "index": req.index}


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
