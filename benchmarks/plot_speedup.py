"""Summary and plots (time, speedup S(p), efficiency E(p)) from the benchmark raw CSV.

Usage: python benchmarks/plot_speedup.py benchmark_results.csv [output_prefix]
Needs only matplotlib. Central value: median; spread: min-max.
S(p) = T_openmp(1) / T_openmp(p): same method, one thread as the base (this isolates
the effect of parallelism). The speedup relative to the sequential reference is
reported separately (S_vs_sequential).
"""
import csv
import statistics
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

path = sys.argv[1] if len(sys.argv) > 1 else "benchmark_results.csv"
prefix = sys.argv[2] if len(sys.argv) > 2 else path.rsplit(".", 1)[0]
with open(path, newline="") as f:
    rows = list(csv.DictReader(f))
if any(r["equal_to_reference"] != "1" for r in rows):
    print("WARNING: some measurements differ from the reference; do not use them.")
queries = int(rows[0]["queries"])


def grouped(column):
    """{(backend, threads): [values]}"""
    g = defaultdict(list)
    for r in rows:
        g[(r["backend"], int(r["threads"]))].append(float(r[column]))
    return g


def summarize(column):
    g = grouped(column)
    seq = statistics.median(g[("sequential", 1)])
    omp = {p: v for (b, p), v in g.items() if b == "openmp"}
    threads = sorted(omp)
    med = {p: statistics.median(omp[p]) for p in threads}
    base = med[1]
    return {
        "threads": threads,
        "median": [med[p] for p in threads],
        "min": [min(omp[p]) for p in threads],
        "max": [max(omp[p]) for p in threads],
        "S": [base / med[p] for p in threads],
        "E": [base / med[p] / p for p in threads],
        "S_vs_sequential": [seq / med[p] for p in threads],
        "seq": seq,
    }


core, total = summarize("core_s"), summarize("total_s")
threads = core["threads"]
fields = ["threads", "core_median_s", "core_min_s", "core_max_s", "core_S", "core_E",
          "core_S_vs_sequential", "queries_per_s", "total_median_s", "total_S", "total_E"]
with open(prefix + "_summary.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(fields)
    for i, p in enumerate(threads):
        w.writerow([p] + [f"{x:.6g}" for x in (
            core["median"][i], core["min"][i], core["max"][i], core["S"][i], core["E"][i],
            core["S_vs_sequential"][i], queries / core["median"][i], total["median"][i],
            total["S"][i], total["E"][i])])
print(f"sequential reference: core median {core['seq']:.4f} s, total median "
      f"{total['seq']:.4f} s, {queries / core['seq']:.1f} queries/s")
print("  ".join(f"{h:>14}" for h in fields))
for i, p in enumerate(threads):
    print("  ".join(f"{x:>14.4f}" for x in (
        p, core["median"][i], core["min"][i], core["max"][i], core["S"][i], core["E"][i],
        core["S_vs_sequential"][i], queries / core["median"][i], total["median"][i],
        total["S"][i], total["E"][i])))

fig, ax = plt.subplots(1, 3, figsize=(15, 4.2))
for data, label in ((core, "core"), (total, "total")):
    err = [[m - lo for m, lo in zip(data["median"], data["min"])],
           [hi - m for m, hi in zip(data["median"], data["max"])]]
    line = ax[0].errorbar(threads, data["median"], yerr=err, marker="o", capsize=3,
                          label=f"OpenMP {label}")
    ax[0].axhline(data["seq"], linestyle=":", color=line[0].get_color(),
                  label=f"sequential {label}")
    ax[1].plot(threads, data["S"], marker="o", label=f"S(p) {label}")
    ax[2].plot(threads, data["E"], marker="o", label=f"E(p) {label}")
ax[0].set(xlabel="threads p", ylabel="time [s]", title="Time (median, min-max)", xticks=threads)
ax[1].plot(threads, threads, "--", color="gray", label="ideal")
ax[1].set(xlabel="threads p", ylabel="S(p) = T(1)/T(p)", title="Speedup", xticks=threads)
ax[2].axhline(1.0, linestyle="--", color="gray")
ax[2].set(xlabel="threads p", ylabel="E(p) = S(p)/p", title="Efficiency", xticks=threads,
          ylim=(0, 1.1))
for a in ax:
    a.legend(fontsize=8)
fig.tight_layout()
fig.savefig(prefix + "_plots.png", dpi=150)
print("Wrote", prefix + "_summary.csv", "and", prefix + "_plots.png")
