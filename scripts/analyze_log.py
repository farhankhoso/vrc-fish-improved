#!/usr/bin/env python3
"""分析自动钓鱼日志，重点关注冲顶/冲底时的震荡行为"""
import re
import sys

LOG_FILE = "data/logs/自动钓鱼日志.csv"

# 解析 MPC 行和 ctrl 行
# [MPC] dt=63ms fishY=572 sCY=571 sH=163 sv=0 fv=0 cP=20 cR=13 hold=0
# [ctrl] 0ms [fast] fishY=572 sCY=571 sH=163 [color]
mpc_re = re.compile(
    r'\[MPC\] dt=(\d+)ms fishY=(\d+) sCY=(\d+) sH=(\d+) sv=(-?\d+) fv=(-?\d+) cP=(\d+) cR=(\d+) hold=(\d)'
)
ctrl_re = re.compile(
    r'\[ctrl\] (\d+)ms \[(fast|full)\] fishY=(\d+) sCY=(\d+) sH=(\d+) \[(color|tpl)\]'
)
track_re = re.compile(
    r'\[ctrl\] track locked.*x=(\d+) y=(\d+) w=(\d+) h=(\d+)'
)

# 读取日志
with open(LOG_FILE, encoding='utf-8') as f:
    lines = f.readlines()

# 找 track 信息
track_y = None
track_h = None
for line in lines:
    m = track_re.search(line)
    if m:
        track_y = int(m.group(2))
        track_h = int(m.group(4))
        break

if track_y is not None:
    # 轨道物理边界
    track_top = track_y + 30
    track_bot = track_y + track_h - 30
    print(f"轨道信息: y={track_y} h={track_h}")
    print(f"物理边界: top={track_top} bottom={track_bot}")
    print(f"轨道中点: {(track_top + track_bot) / 2:.0f}")
    print()

# 收集所有 ctrl 检测帧（含 MPC 决策帧）
frames = []
for i, line in enumerate(lines):
    m = ctrl_re.search(line)
    if m:
        frames.append({
            'line': i + 1,
            'dt': int(m.group(1)),
            'mode': m.group(2),
            'fishY': int(m.group(3)),
            'sCY': int(m.group(4)),
            'sH': int(m.group(5)),
            'detect': m.group(6),
        })

mpc_frames = []
for i, line in enumerate(lines):
    m = mpc_re.search(line)
    if m:
        mpc_frames.append({
            'line': i + 1,
            'dt': int(m.group(1)),
            'fishY': int(m.group(2)),
            'sCY': int(m.group(3)),
            'sH': int(m.group(4)),
            'sv': int(m.group(5)),
            'fv': int(m.group(6)),
            'cP': int(m.group(7)),
            'cR': int(m.group(8)),
            'hold': int(m.group(9)),
        })

print(f"总帧数: {len(frames)} (MPC决策帧: {len(mpc_frames)})")
print()

# ── 1. 基础统计 ──
if frames:
    sCY_vals = [f['sCY'] for f in frames]
    sH_vals = [f['sH'] for f in frames]
    fishY_vals = [f['fishY'] for f in frames]
    print("=== 基础统计 ===")
    print(f"sCY  范围: {min(sCY_vals)} ~ {max(sCY_vals)}")
    print(f"sH   范围: {min(sH_vals)} ~ {max(sH_vals)}")
    print(f"fishY范围: {min(fishY_vals)} ~ {max(fishY_vals)}")
    print()

# ── 2. sCY 帧间跳变分析 ──
print("=== sCY 帧间跳变 (|delta| > 80px) ===")
jump_count = 0
for i in range(1, len(frames)):
    delta = frames[i]['sCY'] - frames[i-1]['sCY']
    if abs(delta) > 80:
        jump_count += 1
        f = frames[i]
        fp = frames[i-1]
        print(f"  行{fp['line']}->{f['line']}: sCY {fp['sCY']}->{f['sCY']} (Δ={delta:+d}) sH {fp['sH']}->{f['sH']}")
print(f"跳变总数: {jump_count}")
print()

# ── 3. 冲顶/冲底分析 ──
if track_y is not None:
    margin = 40  # 距边界多近算"冲顶/冲底"
    near_top = track_top + margin
    near_bot = track_bot - margin

    print(f"=== 冲顶/冲底分析 (sCY < {near_top} 或 sCY > {near_bot}) ===")

    # 找连续的冲顶/冲底区间
    episodes = []  # (type, start_idx, end_idx, frames)
    current_type = None
    ep_start = None

    for i, mf in enumerate(mpc_frames):
        if mf['sCY'] < near_top:
            t = 'TOP'
        elif mf['sCY'] > near_bot:
            t = 'BOT'
        else:
            t = None

        if t != current_type:
            if current_type is not None and ep_start is not None:
                episodes.append((current_type, ep_start, i - 1))
            current_type = t
            ep_start = i if t else None

    if current_type is not None and ep_start is not None:
        episodes.append((current_type, ep_start, len(mpc_frames) - 1))

    print(f"冲顶/冲底事件数: {len(episodes)}")
    for ep_type, si, ei in episodes:
        mfs = mpc_frames[si:ei+1]
        dur = sum(m['dt'] for m in mfs)
        sCYs = [m['sCY'] for m in mfs]
        svs = [m['sv'] for m in mfs]
        holds = [m['hold'] for m in mfs]
        hold_pct = sum(holds) / len(holds) * 100
        label = "冲顶" if ep_type == 'TOP' else "冲底"
        print(f"  {label} 行{mfs[0]['line']}-{mfs[-1]['line']}: "
              f"持续{len(mfs)}帧/{dur}ms  "
              f"sCY={min(sCYs)}~{max(sCYs)} sv={min(svs)}~{max(svs)} "
              f"hold率={hold_pct:.0f}%")
    print()

# ── 4. MPC 决策翻转分析（震荡检测）──
print("=== MPC hold 翻转（连续翻转 = 震荡）===")
flip_runs = []  # 连续翻转的区间
flip_start = None
flip_count = 0
for i in range(1, len(mpc_frames)):
    if mpc_frames[i]['hold'] != mpc_frames[i-1]['hold']:
        if flip_start is None:
            flip_start = i - 1
        flip_count += 1
    else:
        if flip_count >= 3:  # 至少连续翻转3次才算震荡
            flip_runs.append((flip_start, i - 1, flip_count))
        flip_start = None
        flip_count = 0

if flip_count >= 3:
    flip_runs.append((flip_start, len(mpc_frames) - 1, flip_count))

print(f"震荡区间数（连续翻转≥3次）: {len(flip_runs)}")
for si, ei, cnt in flip_runs:
    mfs = mpc_frames[si:ei+1]
    sCYs = [m['sCY'] for m in mfs]
    svs = [m['sv'] for m in mfs]
    fishYs = [m['fishY'] for m in mfs]
    dur = sum(m['dt'] for m in mfs)
    print(f"  行{mfs[0]['line']}-{mfs[-1]['line']}: "
          f"翻转{cnt}次/{dur}ms  "
          f"sCY={min(sCYs)}~{max(sCYs)} fishY={min(fishYs)}~{max(fishYs)} "
          f"sv={min(svs)}~{max(svs)}")
    # 详细打印这个区间的每帧
    if cnt <= 20:
        for m in mfs:
            dist = ""
            if track_y is not None:
                if m['sCY'] < near_top:
                    dist = f" [距顶{m['sCY'] - track_top}px]"
                elif m['sCY'] > near_bot:
                    dist = f" [距底{track_bot - m['sCY']}px]"
            print(f"    L{m['line']}: fishY={m['fishY']} sCY={m['sCY']} sH={m['sH']} "
                  f"sv={m['sv']:+d} fv={m['fv']:+d} cP={m['cP']} cR={m['cR']} "
                  f"hold={m['hold']}{dist}")
print()

# ── 5. sv（平滑速度）极值分析 ──
print("=== sv 极值 (|sv| >= 20) ===")
high_sv_count = 0
for m in mpc_frames:
    if abs(m['sv']) >= 20:
        high_sv_count += 1
        dist = ""
        if track_y is not None:
            if m['sCY'] <= track_top + 10:
                dist = " [顶部]"
            elif m['sCY'] >= track_bot - 10:
                dist = " [底部]"
        print(f"  L{m['line']}: sCY={m['sCY']} sv={m['sv']:+d} sH={m['sH']} "
              f"fishY={m['fishY']} hold={m['hold']}{dist}")
print(f"高速帧总数: {high_sv_count}/{len(mpc_frames)}")
print()

# ── 6. sH 异常分析 ──
print("=== sH 异常 (sH < 80) ===")
small_sh = [(m['line'], m['sH'], m['sCY']) for m in mpc_frames if m['sH'] < 80]
print(f"sH<80 帧数: {len(small_sh)}/{len(mpc_frames)}")
for line, sh, scy in small_sh[:20]:
    print(f"  L{line}: sH={sh} sCY={scy}")
print()

# ── 7. fishY vs sCY 偏差统计 ──
print("=== fishY 与 sCY 偏差 ===")
diffs = [m['fishY'] - m['sCY'] for m in mpc_frames]
abs_diffs = [abs(d) for d in diffs]
avg_diff = sum(abs_diffs) / len(abs_diffs)
max_diff = max(abs_diffs)
big_diff = sum(1 for d in abs_diffs if d > 100)
print(f"平均|偏差|: {avg_diff:.1f}px")
print(f"最大|偏差|: {max_diff}px")
print(f"|偏差|>100帧: {big_diff}/{len(mpc_frames)}")
print()

# ── 8. 每回合成功率 ──
print("=== 回合信息 ===")
for i, line in enumerate(lines):
    if 'state' in line and '->' in line:
        print(f"  L{i+1}: {line.strip()}")
    if 'cleanup' in line:
        print(f"  L{i+1}: {line.strip()}")
    if 'MISS' in line:
        print(f"  L{i+1}: {line.strip()}")
