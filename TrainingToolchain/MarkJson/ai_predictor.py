"""AI 预测模块：调用 OpenAI 兼容 API（LM Studio / Code Buddy）对样本做 pass/reject 分类。

工作流：
1. collect_examples 从已人工标注的样本中均衡采样 few-shot 示例
2. build_messages 构造 system + few-shot + 待预测样本的 prompt
3. predict_one 调用 LLM，解析返回 {label, confidence, reason}
4. predict_batch 批量预测（并发受 AI_CONCURRENCY 控制）

样本格式不固定（问答/对话/纯文本/含图片），精简时：
- 文本字段截断到 AI_SAMPLE_TEXT_MAX_CHARS
- 图片：vision 模式以 image_url 发送真实 base64；非 vision 模式替换为文字描述
"""
from __future__ import annotations

import asyncio
import base64
import json
import random
import re
from typing import Any

import httpx

import config
from utils import LOGGER, detect_image_mime, _try_b64_image

# ---- parse_response 容错常量 ----
# label 字段名候选（按优先级）
_LABEL_KEYS = (
    "label", "result", "decision", "verdict", "answer",
    "classification", "category", "判断", "结论", "结果",
)
# pass 值候选（小写比较）
_PASS_VALUES = {
    "pass", "yes", "accept", "accepted", "approve", "approved",
    "通过", "合格", "是", "positive", "good", "ok", "保留", "采纳", "✓", "true",
}
# reject 值候选
_REJECT_VALUES = {
    "reject", "no", "deny", "denied", "discard", "discarded",
    "拒绝", "不合格", "否", "negative", "bad", "丢弃", "剔除", "✗", "false",
}
# 置信度字段名候选
_CONF_KEYS = (
    "confidence", "score", "probability", "certainty", "conf", "prob",
    "置信度", "确信度", "概率",
)
# 理由字段名候选
_REASON_KEYS = (
    "reason", "explanation", "rationale", "justification", "comment", "note",
    "理由", "原因", "分析", "说明", "解释",
)
# 全文关键词匹配（JSON 解析全失败时的兜底），覆盖常见中英文判断表述
# 注意：避免单字（如"是/否/行/对/好/差/错"）易误匹配，只保留明确判断词
_PASS_KEYWORDS = (
    "pass", "通过", "合格", "accept", "采纳", "保留", "可以", "符合", "正确",
    "优质", "没问题", "合适", "值得", "推荐", "yes", "ok", "good", "approve",
    "positive", "✓",
)
_REJECT_KEYWORDS = (
    "reject", "拒绝", "不合格", "deny", "丢弃", "剔除", "不行", "不符合", "错误",
    "劣质", "有问题", "不合适", "舍弃", "删除", "no", "bad", "discard", "negative",
    "✗",
)


class AIPredictor:
    """调用 OpenAI 兼容 /v1/chat/completions 进行样本分类预测。"""

    def __init__(
        self,
        api_base: str | None = None,
        api_key: str | None = None,
        model: str | None = None,
        vision_enabled: bool | None = None,
    ):
        self.api_base = (api_base or config.AI_API_BASE).rstrip("/")
        self.api_key = api_key or config.AI_API_KEY
        self.model = model or config.AI_MODEL
        self.vision_enabled = (
            config.AI_VISION_ENABLED if vision_enabled is None else vision_enabled
        )
        self.max_examples = config.AI_MAX_EXAMPLES_PER_CLASS
        self.max_chars = config.AI_SAMPLE_TEXT_MAX_CHARS
        self.timeout = config.AI_REQUEST_TIMEOUT

    # ---- 连接测试 ----
    async def test_connection(self) -> tuple[bool, str]:
        """GET /models 测试连接，返回 (成功?, 模型列表/错误信息)。"""
        headers = self._headers()
        try:
            async with httpx.AsyncClient(timeout=15) as client:
                resp = await client.get(f"{self.api_base}/models", headers=headers)
                if resp.status_code != 200:
                    return False, f"HTTP {resp.status_code}: {resp.text[:200]}"
                data = resp.json()
                models = [m.get("id", "?") for m in data.get("data", [])]
                if not models:
                    return True, "连接成功（未返回模型列表）"
                return True, "可用模型: " + ", ".join(models[:10])
        except Exception as exc:  # noqa: BLE001
            return False, f"连接失败: {exc}"

    # ---- few-shot 示例收集 ----
    def collect_examples(
        self, progress, reader, max_per_class: int | None = None
    ) -> dict[str, list[dict]]:
        """从已人工标注（pass/reject）的样本中均衡采样 few-shot 示例。

        只取人工标注（pass/reject），不取 pass_ai/reject_ai，避免 AI 偏差放大。
        """
        n = max_per_class or self.max_examples
        pass_indices = [i for i, s in progress.marks.items() if s == "pass"]
        reject_indices = [i for i, s in progress.marks.items() if s == "reject"]
        # 随机采样，保证多样性
        if len(pass_indices) > n:
            random.shuffle(pass_indices)
            pass_indices = pass_indices[:n]
        if len(reject_indices) > n:
            random.shuffle(reject_indices)
            reject_indices = reject_indices[:n]

        def _read_safe(reader, idx):
            try:
                data = reader.read(idx)
                if isinstance(data, dict) and data.get("__parse_error__"):
                    return None
                return data
            except Exception:  # noqa: BLE001
                return None

        pass_samples = [s for s in (_read_safe(reader, i) for i in pass_indices) if s]
        reject_samples = [
            s for s in (_read_safe(reader, i) for i in reject_indices) if s
        ]
        return {"pass": pass_samples, "reject": reject_samples}

    # ---- 样本精简 ----
    def _compact_sample(self, sample: Any, depth: int = 0) -> Any:
        """递归精简样本用于 prompt 展示：截断长文本、图片替换为文字描述。

        始终用文字描述图片（含 few-shot），仅在 _build_target_message 的 vision
        模式下额外发送真实图片。
        """
        if sample is None:
            return None
        if isinstance(sample, bool):
            return sample
        if isinstance(sample, (int, float)):
            return sample
        if isinstance(sample, bytes):
            mime = detect_image_mime(sample)
            if mime:
                return f"[图片: {mime}, {len(sample)}字节]"
            return f"[二进制: {len(sample)}字节]"
        if isinstance(sample, str):
            mime = _try_b64_image(sample)
            if mime:
                try:
                    raw = base64.b64decode(sample)
                    return f"[图片: {mime}, {len(raw)}字节(base64)]"
                except Exception:  # noqa: BLE001
                    return "[图片(base64, 解码失败)]"
            # 普通文本截断
            if len(sample) > self.max_chars:
                return sample[: self.max_chars] + f"...[截断, 原长{len(sample)}]"
            return sample
        if isinstance(sample, list):
            if depth >= 5:
                return f"[列表, {len(sample)}项, 已折叠]"
            return [self._compact_sample(v, depth + 1) for v in sample[:50]]
        if isinstance(sample, dict):
            if depth >= 5:
                return f"[对象, {len(sample)}键, 已折叠]"
            return {
                k: self._compact_sample(v, depth + 1)
                for k, v in list(sample.items())[:50]
            }
        return str(sample)

    def _extract_images(self, sample: Any, out: list[str]) -> None:
        """递归提取图片的 data URL（仅 vision 模式用）。"""
        if isinstance(sample, bytes):
            mime = detect_image_mime(sample)
            if mime and len(sample) <= 4 * 1024 * 1024:  # 单图上限 4MB
                b64 = base64.b64encode(sample).decode("ascii")
                out.append(f"data:{mime};base64,{b64}")
        elif isinstance(sample, str):
            mime = _try_b64_image(sample)
            if mime:
                try:
                    raw = base64.b64decode(sample)
                    if len(raw) <= 4 * 1024 * 1024:
                        b64 = base64.b64encode(raw).decode("ascii")
                        out.append(f"data:{mime};base64,{b64}")
                except Exception:  # noqa: BLE001
                    pass
        elif isinstance(sample, list):
            for v in sample:
                self._extract_images(v, out)
        elif isinstance(sample, dict):
            for v in sample.values():
                self._extract_images(v, out)

    # ---- prompt 构造 ----
    def _build_system_prompt(self) -> str:
        return (
            "你是训练数据质量审核助手。用户会提供若干「已标注示例」和一条「待判断样本」。\n"
            "样本格式不固定（可能是问答、对话、纯文本、含图片等），你需要理解其结构后判断质量。\n\n"
            "判断维度（按示例中体现的规律）：\n"
            "- 内容准确性、完整性、相关性\n"
            "- 格式规范性、语言流畅度\n"
            "- 安全性（无有害/违法内容）\n"
            "- 与已通过示例的共性 vs 与被拒绝示例的共性\n\n"
            "## 输出格式（极其重要，必须严格遵守）\n"
            "只输出一个 JSON 对象，不要输出任何其他文字、解释、markdown 代码块标记：\n"
            '{"label": "pass", "confidence": 0.9, "reason": "一句话理由"}\n'
            '或\n'
            '{"label": "reject", "confidence": 0.8, "reason": "一句话理由"}\n\n'
            "字段说明：\n"
            '- label: 只能是 "pass" 或 "reject"（英文小写，不要用中文）\n'
            "- confidence: 0.0~1.0 之间的数字\n"
            "- reason: 一句话中文理由\n\n"
            "置信度说明：非常确定→0.9+，较确定→0.7~0.9，不确定→<0.7。"
        )

    def _build_few_shot(self, examples: dict[str, list[dict]]) -> str:
        parts: list[str] = []
        pass_list = examples.get("pass", [])
        reject_list = examples.get("reject", [])
        parts.append(f"## 通过(pass)的示例（共{len(pass_list)}条）：\n")
        for i, s in enumerate(pass_list, 1):
            compact = self._compact_sample(s)
            parts.append(f"### 示例{i}\n{json.dumps(compact, ensure_ascii=False, indent=2)}\n")
        parts.append(f"\n## 拒绝(reject)的示例（共{len(reject_list)}条）：\n")
        for i, s in enumerate(reject_list, 1):
            compact = self._compact_sample(s)
            parts.append(f"### 示例{i}\n{json.dumps(compact, ensure_ascii=False, indent=2)}\n")
        return "".join(parts)

    def _build_target_message(self, sample: dict) -> dict:
        """构造待预测样本消息。vision 模式返回 content list（含图片），否则纯文本。"""
        compact = self._compact_sample(sample)
        text_part = (
            "## 待判断样本：\n"
            + json.dumps(compact, ensure_ascii=False, indent=2)
            + "\n\n## 请输出判断结果（仅JSON）："
        )
        if self.vision_enabled:
            images: list[str] = []
            self._extract_images(sample, images)
            if images:
                content: list[dict] = [{"type": "text", "text": text_part}]
                for url in images[:4]:  # 最多发送4张图
                    content.append({"type": "image_url", "image_url": {"url": url}})
                return {"role": "user", "content": content}
        return {"role": "user", "content": text_part}

    def build_messages(self, examples: dict[str, list[dict]], sample: dict) -> list[dict]:
        """构造完整 messages 列表（system + few-shot user + target user）。"""
        messages: list[dict] = [
            {"role": "system", "content": self._build_system_prompt()},
            {"role": "user", "content": self._build_few_shot(examples) + "\n请先观察上述示例的规律。"},
            self._build_target_message(sample),
        ]
        return messages

    # ---- 调用 LLM ----
    def _headers(self) -> dict[str, str]:
        return {
            "Authorization": f"Bearer {self.api_key}",
            "Content-Type": "application/json",
        }

    async def _call_llm(self, messages: list[dict]) -> str:
        payload = {
            "model": self.model,
            "messages": messages,
            "temperature": config.AI_TEMPERATURE,
            "max_tokens": config.AI_MAX_TOKENS,
            "stream": False,  # 显式禁用流式，等待完整响应
            # 注意：LM Studio 不支持 json_object，只支持 json_schema/text；
            # 这里用 text 靠 prompt 约束格式，兼容性最好
        }
        # read timeout 单独放大：本地大模型推理可能数分钟，connect/write 快
        timeout = httpx.Timeout(
            connect=float(config.AI_CONNECT_TIMEOUT),
            read=float(config.AI_REQUEST_TIMEOUT),
            write=float(config.AI_CONNECT_TIMEOUT),
            pool=float(config.AI_CONNECT_TIMEOUT),
        )
        try:
            resp = await self._post(payload, timeout)
        except httpx.ReadTimeout:
            raise RuntimeError(
                f"请求超时（推理超过 {config.AI_REQUEST_TIMEOUT}s），"
                f"请增大 AI_REQUEST_TIMEOUT 或换用更快的模型"
            )
        except httpx.ConnectError as exc:
            raise RuntimeError(f"无法连接 {self.api_base}：{exc}")
        except httpx.HTTPError as exc:
            raise RuntimeError(f"HTTP 错误: {exc}")

        # 降级：response_format 不被支持时（400），去掉该参数重试一次
        if resp.status_code == 400 and "response_format" in resp.text:
            LOGGER.warning("后端不支持 response_format，降级重试")
            payload.pop("response_format", None)
            try:
                resp = await self._post(payload, timeout)
            except httpx.HTTPError as exc:
                raise RuntimeError(f"HTTP 错误: {exc}")

        if resp.status_code != 200:
            raise RuntimeError(f"LLM 返回 HTTP {resp.status_code}: {resp.text[:300]}")
        try:
            data = resp.json()
        except ValueError as exc:
            raise RuntimeError(f"LLM 响应非 JSON: {exc}，原始: {resp.text[:300]}")

        return self._extract_content(data, payload)

    @staticmethod
    def _extract_content(data: dict, payload: dict) -> str:
        """从 OpenAI 兼容响应中提取文本内容，处理 reasoning 模型和 length 截断。"""
        try:
            choice = data["choices"][0]
        except (KeyError, IndexError) as exc:
            raise RuntimeError(f"响应缺少 choices: {exc}，原始: {json.dumps(data, ensure_ascii=False)[:300]}")

        message = choice.get("message", {})
        content = message.get("content") or ""

        # reasoning 模型（如 deepseek-r1）：内容可能在 reasoning_content 字段
        if not content:
            reasoning = message.get("reasoning_content") or ""
            if reasoning:
                LOGGER.info("content 为空，使用 reasoning_content（%d字符）", len(reasoning))
                content = reasoning

        finish_reason = choice.get("finish_reason", "")
        # content 仍为空：检查是否被 reasoning 耗尽 token（finish_reason=length）
        if not content:
            if finish_reason == "length":
                raise RuntimeError(
                    f"LLM 返回空内容（finish_reason=length，token 被 reasoning 耗尽），"
                    f"当前 max_tokens={payload.get('max_tokens')}，"
                    f"请在 config.py 增大 AI_MAX_TOKENS"
                )
            raise RuntimeError(
                f"LLM 返回空内容（finish_reason={finish_reason}），"
                f"message 字段: {json.dumps(message, ensure_ascii=False)[:300]}"
            )

        # 有内容但被截断：记录警告（不报错，尽力解析已有内容）
        if finish_reason == "length":
            LOGGER.warning(
                "LLM 响应被截断（finish_reason=length），max_tokens=%s，content 长度=%d",
                payload.get("max_tokens"), len(content)
            )
        return content

    async def _post(self, payload: dict, timeout: httpx.Timeout):
        """发送 POST 请求并返回 response。"""
        async with httpx.AsyncClient(timeout=timeout) as client:
            return await client.post(
                f"{self.api_base}/chat/completions",
                headers=self._headers(),
                json=payload,
            )

    async def _call_llm_stream(self, messages: list[dict]):
        """流式调用 LLM，yield (type, text) 元组。

        type: 'thinking' (reasoning_content 思维链) | 'content' (最终回答)
        流式 read timeout 设为 None，避免推理慢被中断（靠 [DONE] 或连接关闭结束）。
        """
        payload = {
            "model": self.model,
            "messages": messages,
            "temperature": config.AI_TEMPERATURE,
            "max_tokens": config.AI_MAX_TOKENS,
            "stream": True,
        }
        # 流式：connect/write/pool 用常规超时，read 设 None（等待 chunk 不限时）
        timeout = httpx.Timeout(
            connect=float(config.AI_CONNECT_TIMEOUT),
            read=None,
            write=float(config.AI_CONNECT_TIMEOUT),
            pool=float(config.AI_CONNECT_TIMEOUT),
        )
        async with httpx.AsyncClient(timeout=timeout) as client:
            async with client.stream(
                "POST",
                f"{self.api_base}/chat/completions",
                headers=self._headers(),
                json=payload,
            ) as resp:
                if resp.status_code != 200:
                    body = await resp.aread()
                    raise RuntimeError(
                        f"LLM 返回 HTTP {resp.status_code}: {body.decode('utf-8', errors='replace')[:300]}"
                    )
                async for line in resp.aiter_lines():
                    if not line or not line.startswith("data:"):
                        continue
                    data = line[5:].strip()
                    if data == "[DONE]":
                        break
                    try:
                        chunk = json.loads(data)
                    except (json.JSONDecodeError, ValueError):
                        continue
                    choices = chunk.get("choices") or []
                    if not choices:
                        continue
                    delta = choices[0].get("delta", {}) or {}
                    # reasoning 模型（如 deepseek-r1）的思维链
                    reasoning = delta.get("reasoning_content") or ""
                    if reasoning:
                        yield ("thinking", reasoning)
                    content = delta.get("content") or ""
                    if content:
                        yield ("content", content)

    @staticmethod
    def _find_json_objects(text: str) -> list[str]:
        """用栈匹配提取所有顶层 JSON 对象字符串，正确处理字符串内的 {} 和转义。"""
        objects: list[str] = []
        depth = 0
        start = -1
        in_string = False
        escape = False
        for i, c in enumerate(text):
            if in_string:
                if escape:
                    escape = False
                elif c == "\\":
                    escape = True
                elif c == '"':
                    in_string = False
                continue
            if c == '"':
                in_string = True
            elif c == "{":
                if depth == 0:
                    start = i
                depth += 1
            elif c == "}":
                if depth > 0:
                    depth -= 1
                    if depth == 0 and start >= 0:
                        objects.append(text[start : i + 1])
                        start = -1
        return objects

    @staticmethod
    def _normalize_label(val) -> str | None:
        """将各种 label 值归一化为 pass/reject，无法识别返回 None。"""
        if val is None:
            return None
        if isinstance(val, bool):
            return "pass" if val else "reject"
        s = str(val).strip().lower()
        if not s:
            return None
        if s in _PASS_VALUES:
            return "pass"
        if s in _REJECT_VALUES:
            return "reject"
        # 模糊匹配：包含关键词
        if any(kw in s for kw in ("pass", "通过", "accept", "合格", "approve")):
            return "pass"
        if any(kw in s for kw in ("reject", "拒绝", "deny", "不合格", "discard")):
            return "reject"
        return None

    @staticmethod
    def parse_response(text: str) -> dict:
        """解析 LLM 返回，提取 label/confidence/reason。高度容错。

        支持场景：
        - 纯 JSON / JSON 前后有解释文字
        - markdown 代码块包裹（```json ... ```）
        - 字段名变体（result/decision/verdict...）
        - 中文标签（通过/拒绝）
        - reason 中含 {} 嵌套（栈匹配）
        完全无法提取 label 时返回 error。
        """
        # 空内容/None 单独处理（LLM 偶尔返回空）
        if text is None:
            return {"error": True, "reason": "LLM 返回空内容（None）", "raw": ""}
        cleaned = text.strip()
        if not cleaned:
            return {"error": True, "reason": "LLM 返回空内容", "raw": ""}
        # 去掉 markdown 代码块包裹
        code_block = re.search(r"```(?:json|JSON)?\s*\n?(.*?)\n?```", cleaned, re.DOTALL)
        if code_block:
            cleaned = code_block.group(1).strip()

        # 收集 JSON 候选：整个文本 + 所有 {...} 片段（栈匹配）
        candidates: list[str] = [cleaned]
        candidates.extend(AIPredictor._find_json_objects(cleaned))
        if cleaned != text.strip():
            candidates.extend(AIPredictor._find_json_objects(text))

        for candidate in candidates:
            try:
                obj = json.loads(candidate)
            except (json.JSONDecodeError, ValueError):
                continue
            if not isinstance(obj, dict):
                continue
            # 提取 label
            label = None
            for key in _LABEL_KEYS:
                if key in obj:
                    label = AIPredictor._normalize_label(obj[key])
                    if label:
                        break
            if not label:
                continue  # 该候选无有效 label，试下一个
            # 提取 confidence
            conf = 0.5
            for key in _CONF_KEYS:
                if key in obj:
                    try:
                        conf = float(obj[key])
                        break
                    except (ValueError, TypeError):
                        pass
            conf = max(0.0, min(1.0, conf))
            # 提取 reason
            reason = ""
            for key in _REASON_KEYS:
                if key in obj:
                    reason = str(obj[key]).strip()
                    if reason:
                        break
            return {"label": label, "confidence": conf, "reason": reason}

        # 所有 JSON 候选失败：全文关键词兜底
        lower = cleaned.lower()
        pass_count = sum(lower.count(kw.lower()) for kw in _PASS_KEYWORDS)
        reject_count = sum(lower.count(kw.lower()) for kw in _REJECT_KEYWORDS)
        if pass_count > reject_count and pass_count > 0:
            label = "pass"
        elif reject_count > pass_count and reject_count > 0:
            label = "reject"
        elif pass_count > 0 and reject_count > 0:
            # 出现次数相等，无法可靠判断 → error
            return {
                "error": True,
                "reason": "LLM返回同时含通过/拒绝关键词，无法判断倾向",
                "raw": text[:500],
            }
        else:
            # 完全无法判断 → error，携带原始返回便于诊断
            return {
                "error": True,
                "reason": "无法从LLM返回中提取判断结果",
                "raw": text[:500],
            }
        # 关键词兜底提取置信度
        conf_match = re.search(r"(0?\.\d+|[01](?:\.0+)?)", cleaned)
        conf = float(conf_match.group(1)) if conf_match else 0.5
        conf = max(0.0, min(1.0, conf))
        return {"label": label, "confidence": conf, "reason": "从文本关键词提取（JSON解析失败）"}

    async def predict_one(self, sample: dict) -> dict:
        """预测单条样本。

        成功返回 {label, confidence, reason}；
        失败（网络/HTTP/解析错误）返回 {error: True, reason}，不包含有效 label/confidence。
        """
        # 解析失败的样本无法预测
        if isinstance(sample, dict) and sample.get("__parse_error__"):
            return {"error": True, "reason": "样本解析失败，无法预测"}

        examples = self._current_examples
        messages = self.build_messages(examples, sample)
        try:
            raw = await self._call_llm(messages)
            result = self.parse_response(raw)
            if result.get("error"):
                LOGGER.warning(
                    "AI 响应解析失败，原始返回（前500字符）: %s", result.get("raw", raw[:500])
                )
                return result
            result["raw"] = raw
            return result
        except Exception as exc:  # noqa: BLE001
            LOGGER.warning("AI 预测失败: %s", exc)
            return {"error": True, "reason": f"调用失败: {exc}"}

    async def predict_one_stream(self, sample: dict):
        """流式预测单条样本，yield 事件 dict，供前端实时展示推理过程。

        事件类型：
        - {type: 'thinking', text: str}    思维链片段（reasoning 模型）
        - {type: 'content', text: str}     回答内容片段
        - {type: 'result', result: dict, raw: str, reasoning: str}  解析后的最终结果
        - {type: 'error', reason: str}     调用失败
        """
        if isinstance(sample, dict) and sample.get("__parse_error__"):
            yield {"type": "error", "reason": "样本解析失败，无法预测"}
            return

        examples = self._current_examples
        messages = self.build_messages(examples, sample)

        full_content = ""
        full_reasoning = ""
        try:
            async for evt_type, text in self._call_llm_stream(messages):
                if evt_type == "thinking":
                    full_reasoning += text
                    yield {"type": "thinking", "text": text}
                elif evt_type == "content":
                    full_content += text
                    yield {"type": "content", "text": text}
        except httpx.ReadTimeout:
            yield {
                "type": "error",
                "reason": f"请求超时（推理超过 {config.AI_REQUEST_TIMEOUT}s）",
            }
            return
        except httpx.ConnectError as exc:
            yield {"type": "error", "reason": f"无法连接 {self.api_base}：{exc}"}
            return
        except httpx.HTTPError as exc:
            yield {"type": "error", "reason": f"HTTP 错误: {exc}"}
            return
        except RuntimeError as exc:
            yield {"type": "error", "reason": str(exc)}
            return
        except Exception as exc:  # noqa: BLE001
            yield {"type": "error", "reason": f"调用失败: {exc}"}
            return

        # 解析最终结果（优先用 content，为空则用 reasoning）
        raw_to_parse = full_content or full_reasoning
        result = self.parse_response(raw_to_parse)
        if result.get("error"):
            result["raw"] = raw_to_parse[:500]
            LOGGER.warning(
                "AI 响应解析失败，原始返回（前500字符）: %s", raw_to_parse[:500]
            )
        yield {
            "type": "result",
            "result": result,
            "raw": full_content,
            "reasoning": full_reasoning,
        }

    # few-shot 示例缓存（批量预测前设置，避免每条都重新收集）
    _current_examples: dict[str, list[dict]] = {"pass": [], "reject": []}

    def set_examples(self, examples: dict[str, list[dict]]) -> None:
        """设置 few-shot 示例（批量预测前调用一次）。"""
        self._current_examples = examples

    async def predict_batch(
        self,
        indices: list[int],
        reader,
        on_progress=None,
        cancel_event: asyncio.Event | None = None,
    ) -> dict[int, dict]:
        """批量预测，返回 {index: {label, confidence, reason}}。

        并发受 AI_CONCURRENCY 控制。单条失败不中断整体。
        """
        sem = asyncio.Semaphore(max(1, config.AI_CONCURRENCY))
        results: dict[int, dict] = {}

        async def _predict_one(idx: int) -> None:
            if cancel_event and cancel_event.is_set():
                return
            try:
                data = reader.read(idx)
            except Exception as exc:  # noqa: BLE001
                results[idx] = {"error": True, "reason": f"读取失败: {exc}"}
                if on_progress:
                    on_progress(idx, False)
                return
            async with sem:
                result = await self.predict_one(data)
            results[idx] = result
            if on_progress:
                # error 结果不算自动采纳
                auto = not result.get("error") and result.get("confidence", 0.0) >= config.AI_CONFIDENCE_THRESHOLD
                on_progress(idx, auto)

        tasks = [asyncio.create_task(_predict_one(i)) for i in indices]
        await asyncio.gather(*tasks, return_exceptions=True)
        return results
