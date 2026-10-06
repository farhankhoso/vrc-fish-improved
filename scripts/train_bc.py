"""
行为克隆训练脚本 - VRChat FISH! 滑块控制
用法: python scripts/train_bc.py [--csv data/record_data.csv] [--out data/ml_weights.txt]
"""

import argparse
import os

try:
    import numpy as np
except ImportError:
    np = None

# ── 配置 ──
FEATURES = ["dy", "sliderVel", "fishVel", "sliderY_norm"]
LABEL = "duty_label"

# 网络结构: 输入4 -> 16 -> 8 -> 1
HIDDEN_LAYERS = [16, 8]
LEARNING_RATE = 1e-3
EPOCHS = 500
BATCH_SIZE = 64
TRAIN_SPLIT = 0.8
SEED = 42


# ── 激活函数 ──
def relu(x):
    return np.maximum(0, x)

def relu_deriv(x):
    return (x > 0).astype(float)

def sigmoid(x):
    x = np.clip(x, -500, 500)
    return 1.0 / (1.0 + np.exp(-x))

def sigmoid_deriv(s):
    return s * (1.0 - s)


# ── MLP ──
class MLP:
    def __init__(self, layer_sizes, seed=42):
        """layer_sizes: [input_dim, hidden1, hidden2, ..., output_dim]"""
        rng = np.random.RandomState(seed)
        self.weights = []
        self.biases = []
        for i in range(len(layer_sizes) - 1):
            fan_in = layer_sizes[i]
            fan_out = layer_sizes[i + 1]
            # He initialization
            w = rng.randn(fan_out, fan_in) * np.sqrt(2.0 / fan_in)
            b = np.zeros(fan_out)
            self.weights.append(w)
            self.biases.append(b)
        self.n_layers = len(self.weights)

    def forward(self, x):
        """前向传播, 返回各层激活值(含输入)"""
        activations = [x]
        for i in range(self.n_layers):
            z = activations[-1] @ self.weights[i].T + self.biases[i]
            if i == self.n_layers - 1:
                a = sigmoid(z)
            else:
                a = relu(z)
            activations.append(a)
        return activations

    def backward(self, activations, y_true, lr):
        """反向传播 + 参数更新"""
        m = y_true.shape[0]
        # 输出层 (sigmoid + MSE)
        y_pred = activations[-1]
        delta = (y_pred - y_true) * sigmoid_deriv(y_pred)  # (m, 1)

        for i in range(self.n_layers - 1, -1, -1):
            a_prev = activations[i]
            w = self.weights[i]  # 使用更新前权重计算上一层 delta
            dw = (delta.T @ a_prev) / m
            db = delta.mean(axis=0)

            if i > 0:
                delta = (delta @ w) * relu_deriv(activations[i])

            self.weights[i] = w - lr * dw
            self.biases[i] -= lr * db

    def predict(self, x):
        return self.forward(x)[-1]

    def save_weights(self, path):
        """保存权重为 C++ 可读的纯文本格式"""
        with open(path, "w") as f:
            for i in range(self.n_layers):
                w = self.weights[i]
                b = self.biases[i]
                f.write(f"# layer {i}\n")
                f.write(f"{w.shape[1]} {w.shape[0]}\n")
                for row in w:
                    f.write(" ".join(f"{v:.8f}" for v in row) + "\n")
                f.write(" ".join(f"{v:.8f}" for v in b) + "\n")
        print(f"[OK] 权重已保存: {path}")


# ── 数据加载 ──
def load_csv(path):
    """加载 CSV, 过滤 duty_label == -1 的行"""
    import csv
    rows = []
    with open(path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            dl = float(row[LABEL])
            if dl < 0:
                continue  # 跳过窗口未填满的帧
            rows.append(row)
    print(f"[OK] 加载 {len(rows)} 条有效样本 (从 {path})")
    return rows


def prepare_data(rows):
    """提取特征和标签, 返回 numpy 数组"""
    X = np.array([[float(r[f]) for f in FEATURES] for r in rows], dtype=np.float64)
    y = np.array([[float(r[LABEL])] for r in rows], dtype=np.float64)
    return X, y


def normalize(X_train, X_val):
    """Z-score 标准化, 返回归一化后的数据和 (mean, std)"""
    mean = X_train.mean(axis=0)
    std = X_train.std(axis=0)
    std[std < 1e-8] = 1.0  # 防止除零
    return (X_train - mean) / std, (X_val - mean) / std, mean, std


# ── 训练 ──
def train(model, X_train, y_train, X_val, y_val, epochs, batch_size, lr):
    n = X_train.shape[0]
    best_val_loss = float("inf")
    best_weights = None
    best_biases = None
    patience = 50
    wait = 0

    for epoch in range(1, epochs + 1):
        # 打乱
        idx = np.random.permutation(n)
        X_shuf = X_train[idx]
        y_shuf = y_train[idx]

        # mini-batch SGD
        epoch_loss = 0.0
        n_batches = 0
        for start in range(0, n, batch_size):
            xb = X_shuf[start:start + batch_size]
            yb = y_shuf[start:start + batch_size]
            acts = model.forward(xb)
            loss = np.mean((acts[-1] - yb) ** 2)
            epoch_loss += loss
            n_batches += 1
            model.backward(acts, yb, lr)

        train_loss = epoch_loss / n_batches

        # 验证
        val_pred = model.predict(X_val)
        val_loss = np.mean((val_pred - y_val) ** 2)

        if epoch % 50 == 0 or epoch == 1:
            print(f"  epoch {epoch:4d}  train_mse={train_loss:.6f}  val_mse={val_loss:.6f}")

        # early stopping
        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_weights = [w.copy() for w in model.weights]
            best_biases = [b.copy() for b in model.biases]
            wait = 0
        else:
            wait += 1
            if wait >= patience:
                print(f"  early stopping at epoch {epoch} (val_mse={best_val_loss:.6f})")
                break

    # 恢复最佳权重
    if best_weights:
        model.weights = best_weights
        model.biases = best_biases
    return best_val_loss


def save_norm_params(path, mean, std):
    """保存归一化参数, 供推理时使用"""
    with open(path, "w") as f:
        f.write("# normalization: mean std (per feature)\n")
        for m, s in zip(mean, std):
            f.write(f"{m:.8f} {s:.8f}\n")
    print(f"[OK] 归一化参数已保存: {path}")


# ── 主流程 ──
def main():
    parser = argparse.ArgumentParser(description="行为克隆训练 - VRChat FISH!")
    parser.add_argument("--csv", default="data/record_data.csv", help="录制数据 CSV 路径")
    parser.add_argument("--out", default="data/ml_weights.txt", help="输出权重文件路径")
    parser.add_argument("--epochs", type=int, default=EPOCHS)
    parser.add_argument("--lr", type=float, default=LEARNING_RATE)
    parser.add_argument("--batch", type=int, default=BATCH_SIZE)
    parser.add_argument("--layers", nargs="+", type=int, default=HIDDEN_LAYERS,
                        help="隐藏层大小, 例如: --layers 16 8")
    args = parser.parse_args()

    if not os.path.exists(args.csv):
        print(f"[ERROR] CSV 文件不存在: {args.csv}")
        print("请先用 ml_mode=1 录制数据")
        return

    if np is None:
        print("[ERROR] 缺少依赖 numpy")
        print("请先安装：pip install numpy")
        return

    # 加载数据
    rows = load_csv(args.csv)
    if len(rows) < 20:
        print(f"[ERROR] 数据太少 ({len(rows)} 条), 至少需要 20 条")
        return

    X, y = prepare_data(rows)
    print(f"[INFO] 特征: {FEATURES}")
    print(f"[INFO] X shape={X.shape}, y shape={y.shape}")
    print(f"[INFO] y 范围: [{y.min():.3f}, {y.max():.3f}], 均值={y.mean():.3f}")

    # 划分训练/验证
    np.random.seed(SEED)
    n = len(X)
    idx = np.random.permutation(n)
    split = int(n * TRAIN_SPLIT)
    X_train, X_val = X[idx[:split]], X[idx[split:]]
    y_train, y_val = y[idx[:split]], y[idx[split:]]
    print(f"[INFO] 训练集: {len(X_train)}, 验证集: {len(X_val)}")

    # 基线：恒定输出训练集均值
    base = y_train.mean()
    base_mse = np.mean((base - y_val) ** 2)
    base_mae = np.mean(np.abs(base - y_val))
    print(f"[BASE] 常数预测 duty={base:.3f}  val_mse={base_mse:.6f}  val_mae={base_mae:.4f}")

    # 归一化
    X_train_n, X_val_n, mean, std = normalize(X_train, X_val)

    # 构建模型
    layer_sizes = [X.shape[1]] + args.layers + [1]
    print(f"[INFO] 网络结构: {layer_sizes}")
    model = MLP(layer_sizes, seed=SEED)

    # 训练
    print("\n开始训练...")
    best_val = train(model, X_train_n, y_train, X_val_n, y_val,
                     args.epochs, args.batch, args.lr)
    print(f"\n最佳验证 MSE: {best_val:.6f}")

    # 评估
    val_pred = model.predict(X_val_n)
    mae = np.mean(np.abs(val_pred - y_val))
    print(f"验证 MAE: {mae:.4f} (平均绝对误差, duty 尺度 0~1)")

    # 保存
    model.save_weights(args.out)
    norm_path = args.out.replace(".txt", "_norm.txt")
    save_norm_params(norm_path, mean, std)

    print(f"\n下一步:")
    print(f"  1. 将 config.ini 中 ml_mode 改为 2")
    print(f"  2. 确保 ml_weights_file={args.out}")
    print(f"  3. 运行程序, 观察 ML 推理效果")
    print(f"\n注意: 推理时需要用相同的归一化参数 ({norm_path})")


if __name__ == "__main__":
    main()
