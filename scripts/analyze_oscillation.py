#!/usr/bin/env python3
"""分析 MPC 控制器过冲/震荡行为，聚焦物理模型匹配度"""
import re

LOG_FILE = "data/logs/自动钓鱼日志.csv"

mpc_re = re.compile(
    r'\[MPC\] dt=(\d+)ms fishY=(\d+) sCY=(\d+) sH=(\d+) sv=(-?\d+) fv=(-?\d+) cP=(\d+) cR=(\d+) hold=(\d)'
)
track_re = re.compile(r'\[ctrl\] track locked.*y=(\d+).*h=(\d+)')

with open(LOG_FILE, encoding='utf-8') as f:
    lines = f.readlines()

# 找轨道
track_y, track_h = None, None
for line in lines:
    m = track_re.search(line)
    if m:
        track_y, track_h = int(m.group(1)), int(m.group(2))
        break

track_top = track_y + 30
track_bot = track_y + track_h - 30
track_mid = (track_top + track_bot) / 2
print(f"轨道: top={track_top} mid={track_mid:.0f} bot={track_bot}")
print()

# 收集 MPC 帧
mpc = []
for i, line in enumerate(lines):
    m = mpc_re.search(line)
    if m:
        mpc.append({
            'line': i+1, 'dt': int(m.group(1)),
            'fishY': int(m.group(2)), 'sCY': int(m.group(3)),
            'sH': int(m.group(4)), 'sv': int(m.group(5)),
            'fv': int(m.group(6)), 'cP': int(m.group(7)),
            'cR': int(m.group(8)), 'hold': int(m.group(9)),
        })

# ── 1. 物理模型准确度验证 ──
# 用 MPC 的物理参数模拟下一帧位置，和实际比较
gravity = 3.25
thrust = -3.30
drag = 0.60

print("=== 物理模型验证: 预测 vs 实际 ===")
print("(预测下一帧的 sCY，与实际对比)")
errors = []
big_errors = []
for i in range(len(mpc) - 1):
    cur = mpc[i]
    nxt = mpc[i + 1]

    # 用当前状态 + 当前动作预测下一帧
    dt_steps = max(1, round(nxt['dt'] / 16.0))  # 约 16ms/步
    sv = cur['sv']
    sCY = float(cur['sCY'])
    accel = thrust if cur['hold'] else gravity

    for _ in range(dt_steps):
        sv = sv * drag + accel
        sCY += sv
        # 轨道反弹
        if sCY < track_top:
            sCY = track_top
            sv = -sv * 0.3
        elif sCY > track_bot:
            sCY = track_bot
            sv = -sv * 0.3

    pred_sCY = sCY
    actual_sCY = nxt['sCY']
    err = actual_sCY - pred_sCY
    errors.append(err)

    if abs(err) > 60:
        big_errors.append((cur, nxt, pred_sCY, err))

avg_abs_err = sum(abs(e) for e in errors) / len(errors)
max_err = max(errors, key=abs)
print(f"平均|误差|: {avg_abs_err:.1f}px")
print(f"最大误差: {max_err:+.0f}px")
print(f"|误差|>60px: {len(big_errors)}/{len(mpc)-1}")
print()

print("大误差帧详情 (前20):")
for cur, nxt, pred, err in big_errors[:20]:
    action = "HOLD" if cur['hold'] else "REL "
    print(f"  L{cur['line']}: sCY={cur['sCY']} sv={cur['sv']:+d} {action} "
          f"-> 预测{pred:.0f} 实际{nxt['sCY']} 误差{err:+.0f}px "
          f"(fishY={cur['fishY']} gap={cur['fishY']-cur['sCY']:+d})")
print()

# ── 2. 过冲分析 ──
# 找鱼和滑块之间差距最大的帧
print("=== 鱼-滑块偏差最大帧 (|fishY - sCY| > 130px) ===")
big_gaps = [(m, m['fishY'] - m['sCY']) for m in mpc if abs(m['fishY'] - m['sCY']) > 130]
for m, gap in big_gaps[:30]:
    zone = ""
    if m['sCY'] < track_top + 40: zone = " [近顶]"
    elif m['sCY'] > track_bot - 40: zone = " [近底]"
    action = "HOLD" if m['hold'] else "REL "
    print(f"  L{m['line']}: fishY={m['fishY']} sCY={m['sCY']} gap={gap:+d} "
          f"sv={m['sv']:+d} {action}{zone}")
print(f"总计: {len(big_gaps)}/{len(mpc)}")
print()

# ── 3. 大摆幅检测 ──
# 找 sCY 从一个极端到另一个极端的完整摆动
print("=== 大摆幅 (sCY 从极端到另一极端) ===")
swing_count = 0
for i in range(2, len(mpc)):
    # 检查是否经历了"高->低->高"或"低->高->低"
    # 简化: 找连续3帧中 max-min > 250px
    window = mpc[max(0,i-4):i+1]
    sCYs = [m['sCY'] for m in window]
    swing = max(sCYs) - min(sCYs)
    if swing > 250:
        # 确认这不是重复计数
        if i == 2 or (max(m['sCY'] for m in mpc[max(0,i-6):i-4+1]) - min(m['sCY'] for m in mpc[max(0,i-6):i-4+1])) <= 250:
            swing_count += 1
            svs = [m['sv'] for m in window]
            holds = [m['hold'] for m in window]
            print(f"  L{window[0]['line']}-{window[-1]['line']}: "
                  f"sCY范围 {min(sCYs)}~{max(sCYs)} (摆幅{swing}px) "
                  f"sv={min(svs)}~{max(svs)} "
                  f"fishY={window[len(window)//2]['fishY']}")
print(f"大摆幅事件: {swing_count}")
print()

# ── 4. hold 决策与边界距离分析 ──
print("=== 边界附近(50px内)的 MPC 决策 ===")
print("看 MPC 在接近边界时是否还在推向边界:")
near_boundary = []
for m in mpc:
    # 接近顶部 (sCY < top + 50) 但 sv < 0 (还在往上) 且 hold=0 (松开=重力向下)
    if m['sCY'] < track_top + 50:
        approaching = m['sv'] < -5  # 正在快速接近顶部
        action = "REL(重力↓)" if not m['hold'] else "HOLD(推力↑)"
        problem = " ⚠️" if (m['sv'] < -5 and m['hold']) else ""
        near_boundary.append(('TOP', m, approaching, action, problem))
    # 接近底部 (sCY > bot - 50) 但 sv > 0 (还在往下) 且 hold=1 (按住=推力向上)
    elif m['sCY'] > track_bot - 50:
        approaching = m['sv'] > 5
        action = "HOLD(推力↑)" if m['hold'] else "REL(重力↓)"
        problem = " ⚠️" if (m['sv'] > 5 and not m['hold']) else ""
        near_boundary.append(('BOT', m, approaching, action, problem))

for btype, m, approaching, action, problem in near_boundary:
    direction = "↑快速" if m['sv'] < -5 else ("↓快速" if m['sv'] > 5 else "≈静止")
    label = "近顶" if btype == 'TOP' else "近底"
    print(f"  L{m['line']}: {label} sCY={m['sCY']} sv={m['sv']:+d}({direction}) "
          f"fishY={m['fishY']} {action}{problem}")
print()

# ── 5. 每回合总结 ──
print("=== 每回合 sCY 统计 ===")
# 按 state 3->4 分割回合
round_starts = []
for i, line in enumerate(lines):
    if 'state 2 -> 3' in line:
        round_starts.append(i + 1)

for ri, start in enumerate(round_starts):
    end = round_starts[ri + 1] if ri + 1 < len(round_starts) else len(lines)
    round_mpc = [m for m in mpc if start <= m['line'] <= end]
    if not round_mpc:
        continue
    sCYs = [m['sCY'] for m in round_mpc]
    svs = [m['sv'] for m in round_mpc]
    fishYs = [m['fishY'] for m in round_mpc]
    gaps = [abs(m['fishY'] - m['sCY']) for m in round_mpc]

    avg_gap = sum(gaps) / len(gaps)
    max_gap = max(gaps)
    near_top = sum(1 for s in sCYs if s < track_top + 40)
    near_bot = sum(1 for s in sCYs if s > track_bot - 40)

    print(f"  回合{ri+1} (L{start}~{end}): {len(round_mpc)}帧 "
          f"sCY={min(sCYs)}~{max(sCYs)} "
          f"avg|gap|={avg_gap:.0f} max|gap|={max_gap} "
          f"近顶{near_top}帧 近底{near_bot}帧")
