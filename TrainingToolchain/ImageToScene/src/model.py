"""ImageToScene 模型：ViT 图像编码器 + 自回归 Transformer 解码器。

默认配置 (configs/default.yaml):
    编码器: ViT-Large/16, d=1024, 24 层, 16 头   ~304M
    解码器: d=1536, 20 层, 24 头 (causal self-attn
            + 对图像 token 的 cross-attn)         ~724M
    合计约 1.03B 参数。
"""

from __future__ import annotations

import torch
import torch.nn as nn
import torch.nn.functional as F


def _init_trunc_(m, std: float = 0.02):
    if isinstance(m, nn.Linear):
        nn.init.trunc_normal_(m.weight, std=std)
        if m.bias is not None:
            nn.init.zeros_(m.bias)
    elif isinstance(m, nn.Embedding):
        nn.init.trunc_normal_(m.weight, std=std)


class Mlp(nn.Module):
    def __init__(self, dim, ratio=4.0, dropout=0.0):
        super().__init__()
        hidden = int(dim * ratio)
        self.fc1 = nn.Linear(dim, hidden)
        self.fc2 = nn.Linear(hidden, dim)
        self.drop = nn.Dropout(dropout)

    def forward(self, x):
        return self.drop(self.fc2(F.gelu(self.fc1(x))))


class SelfAttention(nn.Module):
    def __init__(self, dim, heads, causal=False):
        super().__init__()
        assert dim % heads == 0, f"dim {dim} 不能被 heads {heads} 整除"
        self.heads, self.head_dim, self.causal = heads, dim // heads, causal
        self.qkv = nn.Linear(dim, dim * 3)
        self.proj = nn.Linear(dim, dim)

    def forward(self, x):
        B, L, D = x.shape
        qkv = self.qkv(x).view(B, L, 3, self.heads, self.head_dim).permute(2, 0, 3, 1, 4)
        q, k, v = qkv[0], qkv[1], qkv[2]
        x = F.scaled_dot_product_attention(q, k, v, is_causal=self.causal)
        return self.proj(x.transpose(1, 2).reshape(B, L, D))


class CrossAttention(nn.Module):
    def __init__(self, dim, kv_dim, heads):
        super().__init__()
        assert dim % heads == 0
        self.heads, self.head_dim = heads, dim // heads
        self.q = nn.Linear(dim, dim)
        self.kv = nn.Linear(kv_dim, dim * 2)
        self.proj = nn.Linear(dim, dim)

    def forward(self, x, mem):
        B, L, D = x.shape
        M = mem.shape[1]
        q = self.q(x).view(B, L, self.heads, self.head_dim).transpose(1, 2)
        kv = self.kv(mem).view(B, M, 2, self.heads, self.head_dim).permute(2, 0, 3, 1, 4)
        k, v = kv[0], kv[1]
        out = F.scaled_dot_product_attention(q, k, v)
        return self.proj(out.transpose(1, 2).reshape(B, L, D))


class EncoderBlock(nn.Module):
    def __init__(self, dim, heads, mlp_ratio=4.0, dropout=0.0):
        super().__init__()
        self.ln1 = nn.LayerNorm(dim)
        self.attn = SelfAttention(dim, heads)
        self.ln2 = nn.LayerNorm(dim)
        self.mlp = Mlp(dim, mlp_ratio, dropout)
        self.drop = nn.Dropout(dropout)

    def forward(self, x):
        x = x + self.drop(self.attn(self.ln1(x)))
        x = x + self.drop(self.mlp(self.ln2(x)))
        return x


class DecoderBlock(nn.Module):
    def __init__(self, dim, enc_dim, heads, mlp_ratio=4.0, dropout=0.0):
        super().__init__()
        self.ln1 = nn.LayerNorm(dim)
        self.self_attn = SelfAttention(dim, heads, causal=True)
        self.ln2 = nn.LayerNorm(dim)
        self.cross_attn = CrossAttention(dim, enc_dim, heads)
        self.ln3 = nn.LayerNorm(dim)
        self.mlp = Mlp(dim, mlp_ratio, dropout)
        self.drop = nn.Dropout(dropout)

    def forward(self, x, memory):
        x = x + self.drop(self.self_attn(self.ln1(x)))
        x = x + self.drop(self.cross_attn(self.ln2(x), memory))
        x = x + self.drop(self.mlp(self.ln3(x)))
        return x


class PatchEmbed(nn.Module):
    def __init__(self, img_size, patch, in_chans, dim):
        super().__init__()
        assert img_size % patch == 0
        self.grid = img_size // patch
        self.proj = nn.Conv2d(in_chans, dim, kernel_size=patch, stride=patch)
        self.pos = nn.Parameter(torch.zeros(1, self.grid * self.grid, dim))
        nn.init.trunc_normal_(self.pos, std=0.02)

    def forward(self, x):                      # x: (B, C, H, W)
        x = self.proj(x).flatten(2).transpose(1, 2)   # (B, N, dim)
        return x + self.pos


class ImageToSceneModel(nn.Module):
    def __init__(self, vocab_size: int, img_size: int = 256, patch_size: int = 16,
                 enc_dim: int = 1024, enc_depth: int = 24, enc_heads: int = 16,
                 dec_dim: int = 1536, dec_depth: int = 20, dec_heads: int = 24,
                 max_len: int = 44, mlp_ratio: float = 4.0, dropout: float = 0.0):
        super().__init__()
        self.max_len = max_len
        self.patch = PatchEmbed(img_size, patch_size, 1, enc_dim)
        self.enc = nn.ModuleList(
            [EncoderBlock(enc_dim, enc_heads, mlp_ratio, dropout)
             for _ in range(enc_depth)])
        self.enc_norm = nn.LayerNorm(enc_dim)

        self.tok_emb = nn.Embedding(vocab_size, dec_dim)
        self.pos_emb = nn.Embedding(max_len, dec_dim)
        self.dec = nn.ModuleList(
            [DecoderBlock(dec_dim, enc_dim, dec_heads, mlp_ratio, dropout)
             for _ in range(dec_depth)])
        self.dec_norm = nn.LayerNorm(dec_dim)
        self.head = nn.Linear(dec_dim, vocab_size)
        self.apply(_init_trunc_)

    def encode_image(self, images):
        x = self.patch(images)
        for blk in self.enc:
            x = blk(x)
        return self.enc_norm(x)                # (B, N_img, enc_dim)

    def decode_tokens(self, tokens, memory):
        B, L = tokens.shape
        assert L <= self.max_len, f"序列长度 {L} 超过 max_len {self.max_len}"
        pos = torch.arange(L, device=tokens.device)
        x = self.tok_emb(tokens) + self.pos_emb(pos)[None]
        for blk in self.dec:
            x = blk(x, memory)
        return self.head(self.dec_norm(x))     # (B, L, vocab)

    def forward(self, images, tokens):
        """teacher-forcing 训练前向。

        images: (B, 1, H, W)；tokens: (B, L) 完整序列（BOS...EOS/PAD）
        返回 logits (B, L-1, V)，第 i 位预测 tokens[:, i+1]。
        """
        memory = self.encode_image(images)
        return self.decode_tokens(tokens[:, :-1], memory)

    @torch.no_grad()
    def generate(self, images, eos_id: int = 2, pad_id: int = 0):
        """贪心自回归生成，返回 (B, <=max_len) 的 token 序列。"""
        memory = self.encode_image(images)
        B = images.shape[0]
        device = images.device
        tokens = torch.full((B, 1), 1, dtype=torch.long, device=device)  # BOS
        finished = torch.zeros(B, dtype=torch.bool, device=device)
        for _ in range(self.max_len - 1):
            logits = self.decode_tokens(tokens, memory)[:, -1]
            nxt = logits.argmax(-1)
            nxt = torch.where(finished, torch.full_like(nxt, pad_id), nxt)
            tokens = torch.cat([tokens, nxt[:, None]], dim=1)
            finished |= nxt == eos_id
            if bool(finished.all()):
                break
        return tokens


def count_parameters(model: nn.Module) -> int:
    return sum(p.numel() for p in model.parameters())


def build_model(cfg: dict, tokenizer) -> ImageToSceneModel:
    """从配置构建模型（train / infer 共用）。"""
    m, d = cfg["model"], cfg["data"]
    return ImageToSceneModel(
        vocab_size=tokenizer.vocab_size,
        img_size=d["image_size"],
        patch_size=m["patch_size"],
        enc_dim=m["enc_dim"],
        enc_depth=m["enc_depth"],
        enc_heads=m["enc_heads"],
        dec_dim=m["dec_dim"],
        dec_depth=m["dec_depth"],
        dec_heads=m["dec_heads"],
        max_len=tokenizer.seq_len(d["max_objects"]),
        mlp_ratio=m["mlp_ratio"],
        dropout=m["dropout"],
    )
