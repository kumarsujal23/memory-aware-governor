#!/usr/bin/env python3
"""
Visualization script for Memory-Pressure-Aware Cache Governor evaluation results.

Reads eval/results/summary.csv and generates comparison bar charts:
  1) OOM kills per condition
  2) Cumulative PSI stall time per condition
  3) Cache hit rate per condition

Usage:
    python3 eval/plot_results.py [--results-dir eval/results]
"""
import csv
import os
import sys
import argparse

def read_results(results_dir):
    """Read the summary CSV file."""
    csv_path = os.path.join(results_dir, "summary.csv")
    if not os.path.exists(csv_path):
        print(f"ERROR: {csv_path} not found. Run the evaluation first.")
        sys.exit(1)

    conditions = []
    oom_kills = []
    psi_stall = []
    hit_rates = []

    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if not row.get("Condition"):
                continue
            conditions.append(row["Condition"].replace("_", "\n"))
            oom_kills.append(int(row.get("OOM_Kills") or 0))
            
            try:
                psi_val = row.get("PSI_Stall_Total_us")
                psi_stall.append(int(psi_val) if psi_val else 0)
            except ValueError:
                psi_stall.append(0)
                
            try:
                hit_val = row.get("Hit_Rate_pct")
                hit_rates.append(float(hit_val) if hit_val else 0.0)
            except ValueError:
                hit_rates.append(0.0)

    return conditions, oom_kills, psi_stall, hit_rates


def plot_results(conditions, oom_kills, psi_stall, hit_rates, results_dir):
    """Generate bar chart PNGs using matplotlib."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed. Install with: pip install matplotlib")
        print("Printing text summary instead.\n")
        print_text_summary(conditions, oom_kills, psi_stall, hit_rates)
        return

    colors = ["#2196F3", "#FF9800", "#4CAF50"]

    # 1) OOM kills
    fig, ax = plt.subplots(figsize=(7, 5))
    bars = ax.bar(conditions, oom_kills, color=colors[:len(conditions)], edgecolor="black")
    ax.set_title("OOM Kills per Condition", fontsize=14, fontweight="bold")
    ax.set_ylabel("Count")
    for bar, val in zip(bars, oom_kills):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.1,
                str(val), ha="center", va="bottom", fontweight="bold")
    plt.tight_layout()
    plt.savefig(os.path.join(results_dir, "oom_kills.png"), dpi=150)
    plt.close()

    # 2) PSI stall time
    fig, ax = plt.subplots(figsize=(7, 5))
    stall_ms = [s / 1000.0 for s in psi_stall]  # µs → ms
    bars = ax.bar(conditions, stall_ms, color=colors[:len(conditions)], edgecolor="black")
    ax.set_title("Cumulative PSI Stall Time", fontsize=14, fontweight="bold")
    ax.set_ylabel("Stall Time (ms)")
    for bar, val in zip(bars, stall_ms):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.5,
                f"{val:.1f}", ha="center", va="bottom", fontweight="bold")
    plt.tight_layout()
    plt.savefig(os.path.join(results_dir, "psi_stall_time.png"), dpi=150)
    plt.close()

    # 3) Cache hit rate
    fig, ax = plt.subplots(figsize=(7, 5))
    bars = ax.bar(conditions, hit_rates, color=colors[:len(conditions)], edgecolor="black")
    ax.set_title("Cache Hit Rate", fontsize=14, fontweight="bold")
    ax.set_ylabel("Hit Rate (%)")
    ax.set_ylim(0, 100)
    for bar, val in zip(bars, hit_rates):
        ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.5,
                f"{val:.1f}%", ha="center", va="bottom", fontweight="bold")
    plt.tight_layout()
    plt.savefig(os.path.join(results_dir, "cache_hit_rate.png"), dpi=150)
    plt.close()

    print(f"Plots saved to {results_dir}/")


def print_text_summary(conditions, oom_kills, psi_stall, hit_rates):
    """Print a text table summary when matplotlib is unavailable."""
    header = f"{'Condition':<20} {'OOM Kills':>10} {'PSI Stall (ms)':>15} {'Hit Rate':>10}"
    print(header)
    print("─" * len(header))
    for cond, oom, psi, hr in zip(conditions, oom_kills, psi_stall, hit_rates):
        label = cond.replace("\n", " ")
        print(f"{label:<20} {oom:>10} {psi/1000.0:>15.1f} {hr:>9.1f}%")


def main():
    parser = argparse.ArgumentParser(description="Plot evaluation results")
    parser.add_argument("--results-dir", default="eval/results",
                        help="Directory containing summary.csv")
    args = parser.parse_args()

    conditions, oom_kills, psi_stall, hit_rates = read_results(args.results_dir)
    print_text_summary(conditions, oom_kills, psi_stall, hit_rates)
    plot_results(conditions, oom_kills, psi_stall, hit_rates, args.results_dir)


if __name__ == "__main__":
    main()
