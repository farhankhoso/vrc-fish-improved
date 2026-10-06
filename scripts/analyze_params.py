#!/usr/bin/env python3
"""
对比两组物理参数在 **多步预测** 上的表现。

MPC 使用 sim_horizon=8 步预测，所以单步 RMSE 最优的参数
不一定在 8 步预测上最优。这个脚本直接评估多步预测误差。

同时检查 hold/release 动力学的不对称性。
"""

import math
import os
import statistics
import sys

# Allow importing sibling modules when running from repo root:
#   python scripts/analyze_params.py
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)
from fit_physics import parse_segments, _interval_dt_ms, CtrlFrame
from typing import List, Tuple, Optional

LOG = "data/logs/自动钓鱼日志-长.csv"
BASE_DT = 50.0

# 两组参数
PARAMS_CURRENT = (0.9, 1.0, -2.5)       # 当前 config，实际效果更好
PARAMS_FITTED  = (0.8792, 1.1075, -2.7627)  # 截尾拟合推荐

def sim_trajectory(y0: float, v0: float, holds: List[int], params: Tuple[float,float,float], dt_scales: List[float]) -> List[float]:
    """模拟多步轨迹，返回每步的 y 值。"""
    drag, gravity, thrust = params
    y, v = y0, v0
    ys = []
    for i, hold in enumerate(holds):
        accel = thrust if hold else gravity
        scale = dt_scales[i] if i < len(dt_scales) else 1.0
        v = v * drag + accel * scale
        y = y + v * scale
        ys.append(y)
    return ys

def main():
    segs = parse_segments(LOG)
    if not segs:
        print("No segments found")
        return

    # 计算 assume_dt_ms
    all_dts = []
    for s in segs:
        all_dts.extend(s.mpc_dts)
    assume_dt = statistics.median(all_dts) if all_dts else BASE_DT
    print(f"assume_dt={assume_dt:.0f}ms  segments={len(segs)}")

    # 构建连续帧序列（只用 color, sH >= 80）
    all_frames: List[CtrlFrame] = []
    for seg in segs:
        seg_frames = [f for f in seg.frames if f.src == "color" and f.sH >= 80]
        all_frames.extend(seg_frames)

    print(f"Total color frames (sH>=80): {len(all_frames)}")

    # ── 1. 单步 vs 多步预测误差对比 ──
    print("\n=== 单步 & 多步预测误差对比 ===")
    for horizon in [1, 2, 4, 8]:
        errs_cur = []
        errs_fit = []
        n_eval = 0

        for seg in segs:
            frames = [f for f in seg.frames if f.src == "color" and f.sH >= 80]
            if len(frames) < horizon + 2:
                continue

            for i in range(1, len(frames) - horizon):
                f_prev = frames[i-1]
                f_start = frames[i]

                # 检查连续性（行号间隔不要太大）
                ok = True
                for j in range(horizon):
                    if i+1+j >= len(frames):
                        ok = False
                        break
                    if frames[i+j+1].line_no - frames[i+j].line_no > 8:
                        ok = False
                        break
                    if abs(frames[i+j+1].sCY - frames[i+j].sCY) > 100:
                        ok = False
                        break
                if not ok:
                    continue

                # 初始速度估计
                dt0 = _interval_dt_ms(f_prev, f_start, assume_dt)
                v0 = (f_start.sCY - f_prev.sCY) * BASE_DT / dt0

                # 收集 hold 和 dt_scale
                holds = []
                dt_scales = []
                for j in range(horizon):
                    f_next = frames[i+1+j]
                    holds.append(f_next.hold)
                    dt_j = _interval_dt_ms(frames[i+j], f_next, assume_dt)
                    dt_scales.append(dt_j / BASE_DT)

                # 真实终点
                actual_y = frames[i + horizon].sCY

                # 预测
                pred_cur = sim_trajectory(float(f_start.sCY), v0, holds, PARAMS_CURRENT, dt_scales)
                pred_fit = sim_trajectory(float(f_start.sCY), v0, holds, PARAMS_FITTED, dt_scales)

                errs_cur.append(abs(pred_cur[-1] - actual_y))
                errs_fit.append(abs(pred_fit[-1] - actual_y))
                n_eval += 1

        if n_eval > 0:
            mean_cur = sum(errs_cur) / len(errs_cur)
            mean_fit = sum(errs_fit) / len(errs_fit)
            med_cur = statistics.median(errs_cur)
            med_fit = statistics.median(errs_fit)
            p90_cur = sorted(errs_cur)[int(0.9 * len(errs_cur))]
            p90_fit = sorted(errs_fit)[int(0.9 * len(errs_fit))]
            print(f"  horizon={horizon:2d}  n={n_eval:5d}  "
                  f"current: mean={mean_cur:6.1f} med={med_cur:5.1f} p90={p90_cur:6.1f}  |  "
                  f"fitted:  mean={mean_fit:6.1f} med={med_fit:5.1f} p90={p90_fit:6.1f}  "
                  f"{'<- current better' if mean_cur < mean_fit else '<- fitted better' if mean_fit < mean_cur else 'tie'}")

    # ── 2. hold/release 动力学不对称分析 ──
    print("\n=== hold/release 动力学不对称分析 ===")
    hold_residuals_cur = []
    hold_residuals_fit = []
    rel_residuals_cur = []
    rel_residuals_fit = []

    for seg in segs:
        frames = [f for f in seg.frames if f.src == "color" and f.sH >= 80]
        for i in range(2, len(frames)):
            f0, f1, f2 = frames[i-2], frames[i-1], frames[i]
            if f1.line_no - f0.line_no > 8 or f2.line_no - f1.line_no > 8:
                continue
            if abs(f1.sCY - f0.sCY) > 100 or abs(f2.sCY - f1.sCY) > 100:
                continue
            if f0.hold != f1.hold or f1.hold != f2.hold:
                continue

            dt01 = _interval_dt_ms(f0, f1, assume_dt)
            dt12 = _interval_dt_ms(f1, f2, assume_dt)
            d1 = (f1.sCY - f0.sCY) * BASE_DT / dt01
            d2 = (f2.sCY - f1.sCY) * BASE_DT / dt12
            hold = f2.hold

            for params, name, h_list, r_list in [
                (PARAMS_CURRENT, "cur", hold_residuals_cur, rel_residuals_cur),
                (PARAMS_FITTED, "fit", hold_residuals_fit, rel_residuals_fit),
            ]:
                drag, gravity, thrust = params
                accel = thrust if hold else gravity
                pred = d1 * drag + accel
                err = d2 - pred
                if hold:
                    h_list.append(err)
                else:
                    r_list.append(err)

    for name, h_res, r_res in [
        ("current (0.9,1.0,-2.5)", hold_residuals_cur, rel_residuals_cur),
        ("fitted  (0.88,1.11,-2.76)", hold_residuals_fit, rel_residuals_fit),
    ]:
        h_mean = sum(h_res)/len(h_res) if h_res else 0
        r_mean = sum(r_res)/len(r_res) if r_res else 0
        h_std = (sum((x-h_mean)**2 for x in h_res)/len(h_res))**0.5 if h_res else 0
        r_std = (sum((x-r_mean)**2 for x in r_res)/len(r_res))**0.5 if r_res else 0
        print(f"  {name}")
        print(f"    hold:    n={len(h_res):5d}  bias={h_mean:+.3f}  std={h_std:.3f}")
        print(f"    release: n={len(r_res):5d}  bias={r_mean:+.3f}  std={r_std:.3f}")

    # ── 3. 速度区间分布分析（drag 是否非线性） ──
    print("\n=== 速度区间残差分析（检查 drag 非线性） ===")
    vel_bins = [(-30,-10), (-10,-3), (-3,3), (3,10), (10,30)]
    for params, name in [(PARAMS_CURRENT, "current"), (PARAMS_FITTED, "fitted")]:
        drag, gravity, thrust = params
        print(f"  {name}:")
        for vlo, vhi in vel_bins:
            errs = []
            for seg in segs:
                frames = [f for f in seg.frames if f.src == "color" and f.sH >= 80]
                for i in range(2, len(frames)):
                    f0, f1, f2 = frames[i-2], frames[i-1], frames[i]
                    if f1.line_no - f0.line_no > 8 or f2.line_no - f1.line_no > 8:
                        continue
                    if abs(f1.sCY - f0.sCY) > 100 or abs(f2.sCY - f1.sCY) > 100:
                        continue
                    if f0.hold != f1.hold or f1.hold != f2.hold:
                        continue
                    dt01 = _interval_dt_ms(f0, f1, assume_dt)
                    dt12 = _interval_dt_ms(f1, f2, assume_dt)
                    d1 = (f1.sCY - f0.sCY) * BASE_DT / dt01
                    if d1 < vlo or d1 >= vhi:
                        continue
                    d2 = (f2.sCY - f1.sCY) * BASE_DT / dt12
                    accel = thrust if f2.hold else gravity
                    pred = d1 * drag + accel
                    errs.append(d2 - pred)
            if errs:
                bias = sum(errs)/len(errs)
                std = (sum((x-bias)**2 for x in errs)/len(errs))**0.5
                print(f"    v=[{vlo:+3d},{vhi:+3d})  n={len(errs):5d}  bias={bias:+.3f}  std={std:.3f}")

    # ── 4. 分开拟合 drag 差异深入分析 ──
    print("\n=== hold vs release 分别拟合 drag 对比 ===")
    print("  (fit_physics.py 显示 hold drag=0.70, release drag=0.86)")
    print("  如果差异显著，说明游戏对 hold/release 使用不同的物理参数")
    print("  当前 config 用统一 drag=0.9 可能在 release 上更准，")
    print("  但在 hold 上偏差更大。联合拟合是折中。")

    # ── 5. 总结 ──
    print("\n=== 总结 ===")
    print("  分析完毕。查看上面的 horizon 对比来确认哪组参数在 MPC 实际使用的")
    print("  多步预测上更优。")


if __name__ == "__main__":
    main()
