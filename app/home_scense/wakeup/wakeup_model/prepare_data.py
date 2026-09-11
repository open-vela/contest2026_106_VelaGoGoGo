#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""扫描 dataset/, 构建训练划分, 缓存路径与标签到 data.npz。

关键规则:
- target (唤醒词): dataset/target/ 里 1000 个文件实为 31 个唯一录音的复制
  (md5 相同)。必须按内容去重后以唯一段为单位 80/10/10 划分, 否则同一波形
  同时进 train/val/test, val 指标是背诵不是泛化 (教训: 曾给出 val 召回 90%
  但对板端新念法只有 0.3 分)。可放多个目录混信道: target (板外),
  target_board (板端补录) 等。
- 负样本专用目录 (同嗓音/特定信道): 每文件一个唯一段, 同样按唯一段划分,
  train 路径重复加权采样, val/test 只放 1 份:
    neg_zh     1.pcm 切片, 同嗓音中文语音
    neg_jul    7月29 124s 录音切片, 同嗓音其他短语 (旧模型曾打 0.75-0.93,
               最难负样本)
    neg_board  板端 rec.pcm 的 8 段假语音 (真唤醒词 2 段不在此, 留作验收)
  neg_stereo (7月28 立体声) 为纯噪声, 不入训练, 只作 check_margins 基准。
- unknown (英文 Speech Commands) / background: 文件本身唯一, 分层 80/10/10。
"""
import glob
import hashlib
import os

import numpy as np
from sklearn.model_selection import train_test_split

DATA_ROOT = "dataset"
CLASSES = ["target", "unknown", "background"]  # 标签索引按此顺序
SEED = 42
OUT = "data.npz"

# target 目录 -> 训练路径重复次数 (唯一段少, 靠重复+增强放大多样性)
TARGET_DIRS = ("target", "target_board")
TARGET_TRAIN_DUP = 8
# 负样本目录 -> (标签, 训练重复次数)。标签: 1=unknown(非唤醒词语音), 2=background(噪声)
# neg_stereo (7月28 立体声) 经用户确认为纯噪声, 不加入训练 (background 已有 398 个
# 噪声样本; 它只作 check_margins 的验收基准: 噪声不许唤醒)
NEG_DIRS = {"neg_zh": (1, 12), "neg_jul": (1, 12), "neg_board": (1, 6)}
# train-only 负样本 (不进 val/test):
#   neg_hard  硬负样本挖掘窗 (mine_hard_negs.py: 当前模型高分对齐, 1s 已裁好)
#   neg_stream 完整负样本录音 (随机裁剪覆盖全部对齐, 含 VAD 切片永远产生不了的
#             跨段窗 — run9: 1.pcm 133.1s 跨段负簇 L=7@0.999 只差一窗破防)
TRAIN_ONLY_NEG_DIRS = {"neg_hard": (1, 10), "neg_stream": (1, 40)}
NEG_SPLIT_SEED = 7


def md5(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def split_uniques(paths, seed):
    """唯一路径 80/10/10, 返回 (train, val, test) 三个路径列表。"""
    rng = np.random.RandomState(seed)
    order = rng.permutation(len(paths))
    n_tr = int(len(paths) * 0.8)
    n_va = int(len(paths) * 0.1)
    return ([paths[i] for i in order[:n_tr]],
            [paths[i] for i in order[n_tr:n_tr + n_va]],
            [paths[i] for i in order[n_tr + n_va:]])


def collect_en_bg():
    """英文 unknown + background: 文件唯一, 分层 80/10/10。"""
    items, bg_paths = [], []
    for idx, cls in enumerate(CLASSES[1:], start=1):
        paths = sorted(glob.glob(os.path.join(DATA_ROOT, cls, "*.wav")))
        if cls == "background":
            bg_paths = paths
        for p in paths:
            items.append((p, idx))
        print(f"{cls}: {len(paths)} 个")
    paths = np.array([p for p, _ in items])
    labels = np.array([l for _, l in items])
    p_tr, p_tmp, y_tr, y_tmp = train_test_split(
        paths, labels, test_size=0.2, stratify=labels, random_state=SEED)
    p_va, p_te, y_va, y_te = train_test_split(
        p_tmp, y_tmp, test_size=0.5, stratify=y_tmp, random_state=SEED)
    return ((p_tr, y_tr), (p_va, y_va), (p_te, y_te)), np.array(bg_paths)


def merge_target(splits):
    """各 target 目录: md5 去重 -> 合并唯一段 -> 唯一段级 80/10/10。
    train 路径重复 TARGET_TRAIN_DUP 次。"""
    uniques, seen = [], set()
    for d in TARGET_DIRS:
        files = sorted(glob.glob(os.path.join(DATA_ROOT, d, "*.wav")))
        n_dup = 0
        for p in files:
            h = md5(p)
            if h in seen:
                n_dup += 1
                continue
            seen.add(h)
            uniques.append(p)
        if files:
            print(f"target[{d}]: {len(files)} 个文件 -> {len(files)-n_dup} 唯一 "
                  f"(去重 {n_dup})")
    if not uniques:
        return splits
    tr, va, te = split_uniques(uniques, SEED)
    tr = tr * TARGET_TRAIN_DUP
    print(f"target: 唯一段 {len(uniques)} -> train {len(tr)}(含x{TARGET_TRAIN_DUP}) "
          f"val {len(va)} test {len(te)}")
    (p_tr, y_tr), (p_va, y_va), (p_te, y_te) = splits
    p_tr = np.concatenate([np.array(tr), p_tr])
    y_tr = np.concatenate([np.zeros(len(tr), dtype=y_tr.dtype), y_tr])
    p_va = np.concatenate([np.array(va), p_va])
    y_va = np.concatenate([np.zeros(len(va), dtype=y_va.dtype), y_va])
    p_te = np.concatenate([np.array(te), p_te])
    y_te = np.concatenate([np.zeros(len(te), dtype=y_te.dtype), y_te])
    return (p_tr, y_tr), (p_va, y_va), (p_te, y_te)


def merge_neg_dirs(splits):
    """各负样本目录: 唯一段 80/10/10, train 路径重复加权; train-only 目录全进 train。"""
    (p_tr, y_tr), (p_va, y_va), (p_te, y_te) = splits
    all_dirs = {**NEG_DIRS, **{k: v for k, v in TRAIN_ONLY_NEG_DIRS.items()}}
    for d, (label, dup) in all_dirs.items():
        files = sorted(glob.glob(os.path.join(DATA_ROOT, d, "*.wav")))
        if not files:
            print(f"{d}: 无文件, 跳过")
            continue
        if d in TRAIN_ONLY_NEG_DIRS:
            tr, va, te = files * dup, [], []
        else:
            tr, va, te = split_uniques(files, NEG_SPLIT_SEED)
            tr = tr * dup
        p_tr = np.concatenate([p_tr, np.array(tr)])
        y_tr = np.concatenate([y_tr, np.full(len(tr), label, dtype=y_tr.dtype)])
        if va:
            p_va = np.concatenate([p_va, np.array(va)])
            y_va = np.concatenate([y_va, np.full(len(va), label, dtype=y_va.dtype)])
        if te:
            p_te = np.concatenate([p_te, np.array(te)])
            y_te = np.concatenate([y_te, np.full(len(te), label, dtype=y_te.dtype)])
        print(f"{d}(label={label}): 唯一段 {len(files)} -> train {len(tr)}(x{dup}) "
              f"val {len(va)} test {len(te)}")
    return (p_tr, y_tr), (p_va, y_va), (p_te, y_te)


def main():
    splits, bg_paths = collect_en_bg()
    splits = merge_target(splits)
    splits = merge_neg_dirs(splits)
    (p_tr, y_tr), (p_va, y_va), (p_te, y_te) = splits

    def count(y):
        return {CLASSES[i]: int((y == i).sum()) for i in range(len(CLASSES))}

    print("train:", count(y_tr))
    print("val  :", count(y_va))
    print("test :", count(y_te))

    np.savez(OUT,
             train_paths=p_tr, train_labels=y_tr,
             val_paths=p_va, val_labels=y_va,
             test_paths=p_te, test_labels=y_te,
             bg_paths=bg_paths,
             classes=np.array(CLASSES))
    print(f"已保存 {OUT}")


if __name__ == "__main__":
    main()
