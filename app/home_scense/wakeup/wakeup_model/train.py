#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""训练 3 类唤醒词 CNN: target/unknown/background。

输入 log-mel (49,40,1); 小 CNN; 训练时随机裁剪 + 噪声混合增强。
输出 model.keras, metrics.json, confusion_matrix.png。
"""
import os
import json
import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers
from sklearn.utils.class_weight import compute_class_weight
from sklearn.metrics import (classification_report, confusion_matrix,
                             precision_recall_curve)

import features as F

EPOCHS = 200
BATCH = 64
LR = 1e-3
SEED = 42
NOISE_MIX_PROB = 0.6
SNR_RANGE = (-5.0, 15.0)
# TF CPU 训练存在线程级非确定性, 单次训练结果有波动;
# run7: target 裁剪改末尾锚定 (短语 1.05-1.7s 装不进 1s 窗,
# 随机裁剪教出"任意 60% 短语 = target"的模糊边界)。
# run8: 语音负样本 50% 尾锚 (段尾对齐窗不再是盲区)。
# run9: 6 种子全存 model_s<seed>.keras, 基准选型 (select_params.py)。
TRAIN_SEEDS = (42, 7, 2026, 123, 999, 3407)
# 训练窗口归一后的随机 RMS 电平范围 (dBFS)。
# target 录音 RMS 集中在 -24.6dBFS (跨度仅 ~2.6dB), 模型会学到
# "够响的语音 = target" 的捷径: 音量稍低即漏唤醒, 噪声够响即误唤醒。
LEVEL_RMS_DB = (-40.0, -12.0)
# SpecAug 风格时/频屏蔽 + 变速: target 仅 ~30 个唯一段, 靠增强放大多样性
# (同嗓音负样本加入后, 模型必须在同一嗓音内分辨短语, 需要更鲁棒的特征)。
SPECAUG_PROB = 0.5
TIME_MASK_MAX = 8    # 最多遮 8 帧 (共 49 帧)
FREQ_MASK_MAX = 8    # 最多遮 8 个 mel 带 (共 40 带)
SPEED_PROB = 0.3
SPEED_RANGE = (0.9, 1.1)


def load_samples(path):
    """读 wav 返回 float32 全长样本 (缓存避免重复 IO)。"""
    if not hasattr(load_samples, "_cache"):
        load_samples._cache = {}
    c = load_samples._cache
    if path not in c:
        _, x = F.read_wav(path)
        c[path] = x
    return c[path]


def random_window(x):
    if len(x) >= F.N_SAMPLES:
        off = np.random.randint(0, len(x) - F.N_SAMPLES + 1)
        return x[off:off + F.N_SAMPLES]
    return np.pad(x, (0, F.N_SAMPLES - len(x)))


def end_window(x, max_jitter=0.30 * 16000):
    """取语音末尾 1s 窗 (起点可向前抖动 max_jitter)。

    唤醒词"你好 openvela"语音时长 1.05~1.7s, 1s 窗装不下整句;
    随机裁剪会教模型"短语任意 60% = target", 把同嗓音相近负样本拉高
    (run5/6: 真唤醒只撑 3 窗, 负样本反撑 6 窗)。判别性音段是尾部的
    "openvela" ("你好"是通用问候), 锚定末尾让正样本模式一致;
    部署滑动流必然扫过该对齐, 且 openvela 跨 ~0.7s ≈ 7 窗。
    """
    if len(x) <= F.N_SAMPLES:
        return np.pad(x, (0, F.N_SAMPLES - len(x)))
    jitter = np.random.randint(0, min(int(max_jitter), len(x) - F.N_SAMPLES) + 1)
    start = len(x) - F.N_SAMPLES - jitter
    return x[start:start + F.N_SAMPLES]


def center_window(x):
    """eval 用末尾锚定窗 (与训练基础裁剪一致 = 部署可达的对齐)。"""
    return end_window(x, max_jitter=0)


def mix_noise(x, bg_paths):
    """按随机 SNR 混入一段背景噪声。"""
    p = bg_paths[np.random.randint(len(bg_paths))]
    n = load_samples(p)
    if len(n) >= F.N_SAMPLES:
        off = np.random.randint(0, len(n) - F.N_SAMPLES + 1)
        n = n[off:off + F.N_SAMPLES]
    else:
        n = np.pad(n, (0, F.N_SAMPLES - len(n)))
    sp = np.mean(x ** 2) + 1e-12
    np_ = np.mean(n ** 2) + 1e-12
    snr_db = np.random.uniform(*SNR_RANGE)
    gain = np.sqrt(sp / (np_ * (10 ** (snr_db / 10.0))))
    mix = x + gain * n
    return np.clip(mix, -1.0, 1.0).astype(np.float32)


def set_random_level(x):
    """把窗口 RMS 归一到随机电平, 切断类别与音量的相关性。"""
    rms = np.sqrt(np.mean(x ** 2) + 1e-12)
    target_rms = 10.0 ** (np.random.uniform(*LEVEL_RMS_DB) / 20.0)
    return np.clip(x * (target_rms / rms), -1.0, 1.0).astype(np.float32)


def speed_perturb(x):
    """随机变速 (线性重采样, 音调随之变化的简化版, KWS 常规做法)。"""
    r = np.random.uniform(*SPEED_RANGE)
    n = max(2, int(len(x) / r))
    return np.interp(np.arange(n) * r, np.arange(len(x)), x).astype(np.float32)


def spec_augment(m):
    """SpecAug 风格时/频屏蔽, 填充为窗口均值。m: (N_FRAMES, N_MELS, 1), 就地改。"""
    w = m[..., 0]
    if np.random.rand() < SPECAUG_PROB:
        t = np.random.randint(1, TIME_MASK_MAX + 1)
        t0 = np.random.randint(0, w.shape[0] - t)
        w[t0:t0 + t, :] = w.mean()
    if np.random.rand() < SPECAUG_PROB:
        f = np.random.randint(1, FREQ_MASK_MAX + 1)
        f0 = np.random.randint(0, w.shape[1] - f)
        w[:, f0:f0 + f] = w.mean()
    return m


class DataSeq(keras.utils.Sequence):
    """train: 随机裁剪+噪声混合; eval: 居中裁剪, 不增强。"""

    def __init__(self, paths, labels, bg_paths, train=True, batch=BATCH):
        self.paths = paths
        self.labels = labels
        self.bg_paths = bg_paths
        self.train = train
        self.batch = batch
        rng = np.random.RandomState(SEED)
        self.idx = np.arange(len(paths))
        if train:
            rng.shuffle(self.idx)

    def __len__(self):
        return int(np.ceil(len(self.idx) / self.batch))

    def on_epoch_end(self):
        if self.train:
            np.random.shuffle(self.idx)

    def __getitem__(self, i):
        b = self.idx[i * self.batch:(i + 1) * self.batch]
        xs = np.empty((len(b), F.N_FRAMES, F.N_MELS, 1), dtype=np.float32)
        ys = np.empty((len(b),), dtype=np.int32)
        for j, k in enumerate(b):
            x = load_samples(self.paths[k])
            if self.train:
                if self.labels[k] in (0, 1) and np.random.rand() < SPEED_PROB:
                    x = speed_perturb(x)
                # target 末尾锚定 (短语 > 1s 窗, 学一致的部分模式);
                # 语音负样本 50% 也用尾锚 — run7 教训: 负样本只用随机裁剪时,
                # 段尾对齐窗是盲区 (seg_019 尾窗 0.98 但在 train 里压不住),
                # 且模型会学到"句尾能量衰减=target"捷径 (流误报全在段边界)。
                # 尾锚负样本教会模型"openvela 前面不是你好 = 负"。
                if self.labels[k] == 0:
                    x = end_window(x)
                # 长流文件 (整录音) 一律随机裁剪: 覆盖全部对齐含跨段窗;
                # 短负样本段 50% 尾锚 (段尾对齐窗不再盲区)
                elif (self.labels[k] == 1 and np.random.rand() < 0.5
                      and len(x) <= 10 * 16000):
                    x = end_window(x)
                else:
                    x = random_window(x)
                if self.labels[k] in (0, 1) and np.random.rand() < NOISE_MIX_PROB:
                    x = mix_noise(x, self.bg_paths)
                x = set_random_level(x)
            else:
                x = center_window(x)
            m = F.samples_to_mel(x)[..., None]
            if self.train:
                m = spec_augment(m)
            xs[j] = m
            ys[j] = self.labels[k]
        return xs, ys


def build_model():
    """同嗓音负样本加入后需要更大容量在"同一嗓音内分辨短语":
    Conv 16/32/64 (~25k 参数, 原 8/16/32 为 6.5k)。"""
    inp = keras.Input(shape=(F.N_FRAMES, F.N_MELS, 1))
    x = layers.Conv2D(16, 3, padding="same", activation="relu")(inp)
    x = layers.MaxPooling2D(2)(x)
    x = layers.Conv2D(32, 3, padding="same", activation="relu")(x)
    x = layers.MaxPooling2D(2)(x)
    x = layers.Conv2D(64, 3, padding="same", activation="relu")(x)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dense(32, activation="relu")(x)
    out = layers.Dense(3, activation="softmax")(x)
    m = keras.Model(inp, out)
    m.compile(optimizer=keras.optimizers.Adam(LR),
              loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    return m


def eval_cache(paths, labels, bg_paths):
    """计算 eval 特征 (居中裁剪, 不增强), 返回 (X, y)。"""
    xs = np.empty((len(paths), F.N_FRAMES, F.N_MELS, 1), dtype=np.float32)
    for i, p in enumerate(paths):
        x = center_window(load_samples(p))
        xs[i] = F.samples_to_mel(x)[..., None]
    return xs, np.array(labels)


def eval_probs(model, paths, gains_db):
    """多增益批量推理 (不加噪声/归一, 只放大), 返回 (len(gains_db), len(paths))。"""
    X = np.empty((len(gains_db) * len(paths), F.N_FRAMES, F.N_MELS, 1), np.float32)
    i = 0
    for db in gains_db:
        for p in paths:
            x = F.to_1s_window(load_samples(p))
            if db:
                x = np.clip(x * 10.0 ** (db / 20.0), -1.0, 1.0).astype(np.float32)
            X[i] = F.samples_to_mel(x)[..., None]
            i += 1
    return model.predict(X, verbose=0)[:, 0].reshape(len(gains_db), len(paths))


def choose_threshold(model, val_paths, val_labels):
    """在 val 上选部署阈值 (不在 test 上选, 避免泄漏)。

    F1 最优阈值偏向召回, 对唤醒词偏松; 用多增益 (0/+6/+12dB) 负样本的
    p99.5 抬高阈值压误报, 且不低于 F1 最优阈值兜底召回。
    返回 (thr, info)。
    """
    tgt_paths = [p for p, l in zip(val_paths, val_labels) if l == 0]
    neg_paths = [p for p, l in zip(val_paths, val_labels) if l != 0]
    pn = eval_probs(model, neg_paths, (0.0, 6.0, 12.0)).ravel()
    pt = eval_probs(model, tgt_paths, (0.0, -6.0))

    # F1 最优 (val, 0dB)
    y = np.concatenate([np.ones(pt[0].size), np.zeros(pn.size)])
    prec, rec, thr = precision_recall_curve(y, np.concatenate([pt[0], pn]))
    f1 = 2 * prec * rec / (prec + rec + 1e-12)
    k = int(np.argmax(f1))
    f1_thr = float(thr[k]) if k < len(thr) else 0.5

    dep_thr = max(float(np.percentile(pn, 99.5)), f1_thr)
    info = {
        "method": "max(val 负样本(0/+6/+12dB) p99.5, val F1 最优阈值)",
        "deployment_threshold": dep_thr,
        "f1_optimal_threshold": f1_thr,
        "f1_optimal_f1": float(f1[k]),
        "val_neg_target_prob": {"p50": float(np.percentile(pn, 50)),
                                "p99": float(np.percentile(pn, 99)),
                                "max": float(pn.max())},
        "val_target_recall_at_deployment": {
            "0dB": float((pt[0] > dep_thr).mean()),
            "-6dB": float((pt[1] > dep_thr).mean())},
    }
    return dep_thr, info


def train_once(seed, tr, va, cw):
    """单种子训练, 返回 model。"""
    np.random.seed(seed)
    tf.random.set_seed(seed)
    keras.utils.set_random_seed(seed)
    model = build_model()
    cbs = [
        # 早停盯 val_loss 而非 val_accuracy: val 里 target 仅 ~6/258 (2.3%),
        # 全 reject 的模型 val_accuracy 也有 97.7%, 监控对漏 target 失明会过早停。
        # patience 20: 重增强下 target 拟合慢, patience 6 曾在拟合完成前停住
        # (run5 训练集 target 中位 0.87, 欠拟合)。
        keras.callbacks.EarlyStopping(patience=20, restore_best_weights=True,
                                      monitor="val_loss", mode="min"),
        keras.callbacks.ReduceLROnPlateau(factor=0.5, patience=5,
                                          monitor="val_loss", min_lr=1e-5),
    ]
    model.fit(tr, validation_data=va, epochs=EPOCHS, class_weight=cw,
              callbacks=cbs, verbose=0)
    return model


def main():
    d = np.load("data.npz", allow_pickle=True)
    classes = list(d["classes"])
    bg_paths = d["bg_paths"]
    tr = DataSeq(d["train_paths"], d["train_labels"], bg_paths, train=True)
    va = DataSeq(d["val_paths"], d["val_labels"], bg_paths, train=False)

    cw = compute_class_weight("balanced", classes=np.arange(3),
                              y=d["train_labels"])
    cw = dict(enumerate(cw))
    print("class_weight:", cw, "classes:", classes)

    # 多种子训练, val 上按 (0dB + -6dB 召回)/2 选优
    best = None  # (score, model, thr_info, seed, scores)
    scores = {}
    zh_val = [p for p in d["val_paths"] if "neg_zh" in str(p)]
    for seed in TRAIN_SEEDS:
        model = train_once(seed, tr, va, cw)
        _, info = choose_threshold(model, d["val_paths"], d["val_labels"])
        rec = info["val_target_recall_at_deployment"]
        score = 0.5 * (rec["0dB"] + rec["-6dB"])
        # 同嗓音中文负样本 (最难负样本) 在部署阈值下的误报率
        zh_info = {}
        if zh_val:
            pzh = eval_probs(model, zh_val, (0.0, 6.0, 12.0))
            zh_info = {"max": float(pzh.max()),
                       "p99": float(np.percentile(pzh, 99)),
                       "fa_at_deployment": float((pzh > info["deployment_threshold"]).mean())}
        scores[seed] = {"score": score, "threshold": info["deployment_threshold"],
                        "recall_0db": rec["0dB"], "recall_-6db": rec["-6dB"],
                        **({"neg_zh_val": zh_info} if zh_info else {})}
        # 保存每个种子: 最终选型按基准测试 FA-first (select_params.py),
        # 不按 val 召回 (val 仅 6 个 target, 噪声大; run8 教训: seed 3407
        # 负样本 max=0.013 但 val 分低而被丢弃)
        model.save(f"model_s{seed}.keras")
        print(f"seed {seed}: 部署阈值={info['deployment_threshold']:.4f} "
              f"val召回 0dB={rec['0dB']:.3f} -6dB={rec['-6dB']:.3f} score={score:.3f}"
              + (f" 同嗓音负样本 max={zh_info['max']:.3f} "
                 f"误报率@部署阈值={zh_info['fa_at_deployment']:.1%}" if zh_info else ""))
        if best is None or score > best[0]:
            best = (score, model, info, seed)
    score, model, thr_info, seed = best
    dep_thr = thr_info["deployment_threshold"]
    print(f"选用 seed {seed} (score={score:.3f})")

    model.save("model.keras")

    # ---- 测试集评估 ----
    Xte, yte = eval_cache(d["test_paths"], d["test_labels"], bg_paths)
    prob = model.predict(Xte, verbose=0)
    pred = np.argmax(prob, axis=1)
    acc = float((pred == yte).mean())
    cm = confusion_matrix(yte, pred).tolist()
    rep = classification_report(yte, pred, target_names=classes,
                                digits=4, output_dict=True)
    # 测试集在部署阈值下的误报/漏报
    fa = int(((prob[:, 0] > dep_thr) & (yte != 0)).sum())
    miss = int(((prob[:, 0] <= dep_thr) & (yte == 0)).sum())
    print(f"\nTEST acc={acc:.4f}")
    print(f"部署阈值={dep_thr:.4f} (F1最优={thr_info['f1_optimal_threshold']:.4f}) "
          f"test@部署阈值: 误报={fa} 漏报={miss}")
    print(f"val target 召回@部署阈值: 0dB={thr_info['val_target_recall_at_deployment']['0dB']:.3f} "
          f"-6dB={thr_info['val_target_recall_at_deployment']['-6dB']:.3f}")
    print(classification_report(yte, pred, target_names=classes, digits=4))

    metrics = {
        "classes": classes,
        "test_accuracy": acc,
        "confusion_matrix": cm,
        "report": rep,
        "threshold": thr_info,
        "target_best_threshold": dep_thr,
        "selected_seed": seed,
        "seed_scores": scores,
        "test_at_deployment_threshold": {
            "false_accepts": fa, "misses": miss,
            "target_recall": float(1.0 - miss / max(1, int((yte == 0).sum())))},
        "feature": {"sr": F.SR, "n_fft": F.N_FFT, "frame_len": F.FRAME_LEN,
                    "frame_step": F.FRAME_STEP, "n_mels": F.N_MELS,
                    "n_frames": F.N_FRAMES, "fmin": F.FMIN, "fmax": F.FMAX},
    }
    with open("metrics.json", "w") as fp:
        json.dump(metrics, fp, indent=2, ensure_ascii=False)
    print("已保存 metrics.json")

    # 混淆矩阵图
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(4, 4))
        im = ax.imshow(np.array(cm), cmap="Blues")
        ax.set_xticks(range(3)); ax.set_yticks(range(3))
        ax.set_xticklabels(classes, rotation=45, ha="right")
        ax.set_yticklabels(classes)
        ax.set_xlabel("pred"); ax.set_ylabel("true")
        for a in range(3):
            for b in range(3):
                ax.text(b, a, cm[a][b], ha="center", va="center",
                        color="white" if cm[a][b] > max(cm[a]) / 2 else "black")
        fig.colorbar(im, fraction=0.046)
        plt.tight_layout()
        plt.savefig("confusion_matrix.png", dpi=120)
        print("已保存 confusion_matrix.png")
    except Exception as e:
        print("绘图失败:", e)


if __name__ == "__main__":
    main()
