"""
Measure slider physics from data/record_data.csv.
Test two models:
  Model A: velocity-based (press=constant up speed, release=constant down speed)
  Model B: acceleration-based (press=thrust accel, release=gravity accel)
"""
import argparse, csv
import numpy as np

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", default="data/record_data.csv")
    args = parser.parse_args()

    rows = []
    with open(args.csv, "r") as f:
        for r in csv.DictReader(f):
            rows.append(r)

    n = len(rows)
    t_ms   = np.array([float(r["timestamp_ms"]) for r in rows])
    sY     = np.array([float(r["sliderY"]) for r in rows])
    mouse  = np.array([int(r["mousePressed"]) for r in rows])

    dt = np.diff(t_ms)  # ms

    # Filter out session breaks (dt > 2000ms)
    valid = dt < 2000
    print(f"Total frames: {n}")
    print(f"Valid transitions (dt<2s): {valid.sum()}")
    print(f"Frame interval (valid): median={np.median(dt[valid]):.0f}ms")

    # Compute per-transition velocity (px / frame, normalized to 30ms)
    dy = np.diff(sY)
    vel_per_30ms = dy * 30.0 / (dt + 1e-9)

    # Separate by mouse state (use state at START of transition)
    pressed_vel = vel_per_30ms[valid & (mouse[:-1] == 1)]
    released_vel = vel_per_30ms[valid & (mouse[:-1] == 0)]

    def robust_stats(arr, name):
        if len(arr) < 3:
            print(f"  {name}: not enough data ({len(arr)})")
            return 0.0
        # Trim outliers
        q1, q3 = np.percentile(arr, [25, 75])
        iqr = q3 - q1
        mask = (arr >= q1 - 1.5*iqr) & (arr <= q3 + 1.5*iqr)
        clean = arr[mask]
        med = np.median(clean)
        print(f"  {name}: samples={len(arr)} clean={len(clean)}")
        print(f"    median={med:+.2f}  mean={np.mean(clean):+.2f} +/- {np.std(clean):.2f} px/frame(@30ms)")
        return med

    print(f"\n== Model A: Average velocity by state ==")
    v_released = robust_stats(released_vel, "Released (gravity)")
    v_pressed = robust_stats(pressed_vel, "Pressed (thrust)")

    print(f"\n  Summary:")
    print(f"    Release -> slider drifts {v_released:+.2f} px/frame (positive=down)")
    print(f"    Press   -> slider moves  {v_pressed:+.2f} px/frame (negative=up)")

    # Model B: Look at velocity CHANGE within consecutive same-state runs
    print(f"\n== Model B: Acceleration (velocity change within runs) ==")
    # Find runs of same mouse state
    run_accels_pressed = []
    run_accels_released = []

    i = 0
    while i < n - 2:
        if not valid[i]:
            i += 1
            continue
        # Find run of same state
        state = mouse[i]
        j = i + 1
        while j < n - 1 and valid[j] and mouse[j] == state:
            j += 1
        run_len = j - i
        if run_len >= 3:
            # Compute velocities within this run
            run_vels = vel_per_30ms[i:j]
            # Acceleration = velocity change per frame
            run_acc = np.diff(run_vels)
            if state == 1:
                run_accels_pressed.extend(run_acc.tolist())
            else:
                run_accels_released.extend(run_acc.tolist())
        i = j

    if run_accels_released:
        arr = np.array(run_accels_released)
        a_rel = robust_stats(arr, "Released accel")
    else:
        a_rel = 0
        print("  No released runs >= 3 frames")

    if run_accels_pressed:
        arr = np.array(run_accels_pressed)
        a_pr = robust_stats(arr, "Pressed accel")
    else:
        a_pr = 0
        print("  No pressed runs >= 3 frames")

    # Decide which model fits better
    print(f"\n{'='*50}")
    print(f"Model comparison:")
    if abs(a_rel) < 0.3 and abs(a_pr) < 0.3:
        print(f"  Acceleration ~0 -> Game uses VELOCITY model (constant speed)")
        print(f"  bb_gravity = {v_released:.3f}   (px/frame, positive=down)")
        print(f"  bb_thrust  = {v_pressed:.3f}   (px/frame, negative=up)")
        print(f"  bb_model = velocity")
    else:
        print(f"  Significant acceleration detected -> Game uses ACCELERATION model")
        print(f"  bb_gravity = {a_rel:.4f}  (px/frame^2)")
        print(f"  bb_thrust  = {a_pr:.4f}  (px/frame^2)")
        print(f"  bb_model = acceleration")

    # Show transition behavior: what happens in the first few frames after state change
    print(f"\n== Transition analysis: velocity after state change ==")
    for label, from_state, to_state in [("Release->Press", 0, 1), ("Press->Release", 1, 0)]:
        transitions = []
        for i in range(1, n-5):
            if mouse[i-1] == from_state and mouse[i] == to_state and valid[i]:
                # Collect next few frames velocity
                vels = []
                for k in range(5):
                    if i+k < n-1 and valid[i+k]:
                        vels.append(vel_per_30ms[i+k])
                    else:
                        break
                if len(vels) >= 3:
                    transitions.append(vels[:5])
        if transitions:
            # Pad to same length
            max_len = max(len(t) for t in transitions)
            print(f"  {label} ({len(transitions)} transitions):")
            for frame_idx in range(min(max_len, 5)):
                vals = [t[frame_idx] for t in transitions if len(t) > frame_idx]
                if vals:
                    print(f"    frame+{frame_idx}: vel = {np.median(vals):+.2f} (median of {len(vals)})")
        else:
            print(f"  {label}: no transitions found")

if __name__ == "__main__":
    main()
