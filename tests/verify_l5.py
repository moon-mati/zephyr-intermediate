"""Verify the L5 native_sim trace, including the healthy control case."""

import argparse
import os
from pathlib import Path
import re
import subprocess


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--mode", choices=("stuck", "healthy"), default="stuck")
    args = parser.parse_args()

    env = dict(os.environ)
    env.pop("LD_LIBRARY_PATH", None)
    result = subprocess.run(
        [str(args.binary.resolve()), "-stop_at=6"],
        capture_output=True, text=True, timeout=20, env=env,
    )
    log = re.sub(r"\x1b\[[0-9;]*m", "", result.stdout + result.stderr)
    log_path = args.binary.resolve().parent.parent / f"l5-{args.mode}.log"
    log_path.write_text(log)
    check(result.returncode == 0, f"Simulator failed: {log}")
    check(f"Mode={args.mode}" in log, "Wrong firmware configuration")
    check("[SUMMARY] verification passed" in log, f"Demo did not finish successfully: {log}")

    published = [tuple(map(int, row)) for row in re.findall(
        r"\[PRODUCER\] publish seq=(\d+) tick=(\d+)", log)]
    displayed = [int(seq) for seq in re.findall(r"\[DISPLAY\] seq=(\d+)", log)]
    processed = [tuple(map(int, row)) for row in re.findall(
        r"\[CONSUMER\] processed seq=(\d+) latency=(\d+)ms tick=(\d+)", log)]
    dropped = [tuple(map(int, row)) for row in re.findall(
        r"\[QUEUE\] full used=(\d+)/8 dropped_seq=(\d+) tick=(\d+)", log)]
    callbacks = [tuple(map(int, row)) for row in re.findall(
        r"\[WATCHDOG\] callback channel=(\d+) queue=(\d+)/8 isr=(\d+) tick=(\d+)", log)]
    warnings = [tuple(map(int, row)) for row in re.findall(
        r"\[HEALTH\] WARNING queue >= 75%: (\d+)/8 tick=(\d+)", log)]

    expected = list(range(40))
    check([seq for seq, tick in published] == expected, "Producer must publish 40 samples in order")
    check(displayed == expected, "Display must keep updating during the stall")
    check(all(b[1] - a[1] == 100 for a, b in zip(published, published[1:])),
          "Producer period must remain 100 ms under pressure")
    lost = {seq for used, seq, tick in dropped}
    check(len(lost) == len(dropped), "A dropped sample was counted twice")
    check([seq for seq, latency, tick in processed] == [seq for seq in expected if seq not in lost],
          "Consumer must drain exactly the accepted samples in order")
    check(all(used == 8 for used, seq, tick in dropped), "Drops must occur only at full capacity")
    check(all(used >= 6 for used, tick in warnings), "Health warning fired below 75%")

    if args.mode == "stuck":
        stall = re.search(r"\[CONSUMER\] stuck: sleep=2500ms watchdog=1500ms tick=(\d+)", log)
        resumed = re.search(r"\[CONSUMER\] resumed tick=(\d+)", log)
        check(stall is not None and resumed is not None, "Missing simulated stall or resumption")
        check(dropped and warnings and len(callbacks) == 1,
              "Expected queue drops, health warning and exactly one watchdog callback")
        channel, used, isr, fired_at = callbacks[0]
        check(used == 8 and isr == 1, "Watchdog must observe full queue in interrupt context")
        check(warnings[0][0] == 6, "First warning must occur at 6/8 capacity")
        check(int(stall[1]) < warnings[0][1] < dropped[0][2] < fired_at < int(resumed[1]),
              "Expected stall -> 75% warning -> full queue -> watchdog -> resumption")
        check(1500 <= fired_at - int(stall[1]) <= 1501,
              "Watchdog did not fire 1500 ms after the last consumer progress")
    else:
        check(not callbacks and not dropped and not warnings,
              "Healthy consumer must not cause watchdog callbacks, drops or pressure warnings")
        check("[CONSUMER] stuck:" not in log, "Fault injection must be disabled")

    for line in log.splitlines():
        if "[SUMMARY]" in line or "[WATCHDOG]" in line or "WARNING queue" in line:
            print(line)
    print(f"PASS ({args.mode}): timing, delivery, queue pressure and watchdog verified.")
    print(f"Full log: {log_path}")


if __name__ == "__main__":
    main()
