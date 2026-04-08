"""
cost_threshold_analysis.py
==========================
Bachelorarbeit – Kap. 9: τ*-Verschiebung bei Kostenasymmetrie

Dieses Skript simuliert realistische Score-Verteilungen beider Klassen
und zeigt, wie sich der optimale Schwellenwert τ* verschiebt,
wenn das Kostenverhältnis C_FN/C_FP steigt.

Hintergrund:
  Bei realen Spielerdaten überlappen die Score-Verteilungen P(s|y=0) und
  P(s|y=1) in einer Grauzone. Der optimale Schwellenwert τ* minimiert:
      E[C] = C_FP · FP + C_FN · FN
  und verschiebt sich bei steigendem C_FN/C_FP nach links (niedrigerer τ),
  um False Negatives zu vermeiden — auch wenn dadurch mehr False Positives
  entstehen.

Verwendung:
    python cost_threshold_analysis.py
"""

import os
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from sklearn.metrics import confusion_matrix

SEED       = 42
rng        = np.random.default_rng(SEED)
OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")
os.makedirs(OUTPUT_DIR, exist_ok=True)

# ─────────────────────────────────────────────
# 1. Score-Verteilungen simulieren
#    Legitim:  Beta(2, 8)  → Scores häufen sich bei 0.1–0.3
#    Cheater:  Beta(5, 2)  → Scores häufen sich bei 0.6–0.9
#    → bewusste Überlappung im Bereich 0.3–0.6 (Grauzone)
# ─────────────────────────────────────────────
N_LEGIT   = 9500
N_CHEATER =  500

# Beta-Parameter erzeugen natürliche Score-Überlappung
scores_legit   = rng.beta(2, 8, N_LEGIT)      # Modalwert ~0.14, aber Ausläufer bis 0.7
scores_cheater = rng.beta(5, 2, N_CHEATER)    # Modalwert ~0.80, aber Ausläufer bis 0.3

scores = np.concatenate([scores_legit, scores_cheater])
labels = np.concatenate([np.zeros(N_LEGIT), np.ones(N_CHEATER)])

print("=" * 60)
print("  τ*-Analyse: Optimaler Threshold vs. C_FP/C_FN")
print("=" * 60)
print(f"\n  Simulierte Score-Verteilung:")
print(f"  Legitim  (n={N_LEGIT}):  Mean={scores_legit.mean():.3f}, Std={scores_legit.std():.3f}")
print(f"  Cheater  (n={N_CHEATER}): Mean={scores_cheater.mean():.3f}, Std={scores_cheater.std():.3f}")

# Überlappungszone zeigen
grey = (scores > 0.3) & (scores < 0.6)
n_grey_legit   = ((grey) & (labels == 0)).sum()
n_grey_cheater = ((grey) & (labels == 1)).sum()
print(f"\n  Grauzone (0.3–0.6): {n_grey_legit} Legitime, {n_grey_cheater} Cheater")
print(f"  → Diese können nicht ohne Tradeoff getrennt werden.\n")

# ─────────────────────────────────────────────
# 2. τ*-Analyse über Kostenverhältnisse
# ─────────────────────────────────────────────
cost_ratios = [1, 2, 5, 10, 20, 50]
thresholds  = np.round(np.arange(0.01, 1.0, 0.01), 2)

results = []
for ratio in cost_ratios:
    C_FP = 1
    C_FN = ratio
    best_tau  = None
    best_cost = np.inf
    best_fp   = 0
    best_fn   = 0

    for tau in thresholds:
        y_pred = (scores >= tau).astype(int)
        cm = confusion_matrix(labels, y_pred, labels=[0, 1])
        tn, fp, fn, tp = cm.ravel()
        cost = C_FP * fp + C_FN * fn
        if cost < best_cost:
            best_cost = cost
            best_tau  = tau
            best_fp   = fp
            best_fn   = fn

    results.append({
        "C_FN/C_FP": ratio,
        "τ*":        round(best_tau, 2),
        "FP":        best_fp,
        "FN":        best_fn,
        "Min. Kosten": best_cost
    })

df_res = pd.DataFrame(results)
print("  τ* je Kostenverhältnis:\n")
print(df_res.to_string(index=False))

csv_path = os.path.join(OUTPUT_DIR, "tau_vs_cost_ratio.csv")
df_res.to_csv(csv_path, index=False)
print(f"\n→ CSV gespeichert: {csv_path}")

# ─────────────────────────────────────────────
# 3. Vollständige Kosten-Kurve für ausgewählte Verhältnisse
# ─────────────────────────────────────────────
cost_curves = {}
for ratio in cost_ratios:
    costs = []
    for tau in thresholds:
        y_pred = (scores >= tau).astype(int)
        cm = confusion_matrix(labels, y_pred, labels=[0, 1])
        tn, fp, fn, tp = cm.ravel()
        costs.append(1 * fp + ratio * fn)
    cost_curves[ratio] = costs

# ─────────────────────────────────────────────
# 4. Plots
# ─────────────────────────────────────────────
fig, axes = plt.subplots(1, 3, figsize=(18, 5))
fig.suptitle("τ*-Verschiebung bei steigender Kostenasymmetrie (C_FN > C_FP)\n"
             "Bachelorarbeit – Kap. 9: Empirische Evaluation",
             fontsize=12)

# ── Plot 1: Score-Verteilungen ──
ax = axes[0]
ax.hist(scores_legit,   bins=50, alpha=0.6, color="steelblue", label="Legitim (Label=0)",  density=True)
ax.hist(scores_cheater, bins=50, alpha=0.6, color="tomato",    label="Cheater (Label=1)", density=True)
ax.axvspan(0.3, 0.6, alpha=0.12, color="orange", label="Grauzone (Überlappung)")
ax.set_xlabel("Klassifikator-Score s(x)")
ax.set_ylabel("Dichte")
ax.set_title("Score-Verteilungen beider Klassen")
ax.legend(fontsize=8)
ax.grid(True, alpha=0.3)

# ── Plot 2: τ* vs. Kostenverhältnis ──
ax = axes[1]
tau_values = df_res["τ*"].values
ratio_vals = df_res["C_FN/C_FP"].values
ax.plot(ratio_vals, tau_values, "o-", color="darkblue", linewidth=2.5, markersize=9)
ax.axhline(0.5, color="gray", linestyle="--", alpha=0.5, label="Standard τ=0.5")
for r, t in zip(ratio_vals, tau_values):
    ax.annotate(f"τ*={t:.2f}", (r, t), textcoords="offset points",
                xytext=(6, 4), fontsize=8)
ax.set_xlabel("Kostenverhältnis C_FN / C_FP")
ax.set_ylabel("Optimaler Schwellenwert τ*")
ax.set_title("Verschiebung von τ* bei steigender\nKostenasymmetrie")
ax.set_xscale("log")
ax.set_xticks(cost_ratios)
ax.set_xticklabels([f"1:{r}" for r in cost_ratios])
ax.set_ylim(0, 0.8)
ax.legend(fontsize=8)
ax.grid(True, alpha=0.3)

# ── Plot 3: Kosten-Kurven für 3 Verhältnisse ──
ax = axes[2]
colors = {1: "steelblue", 5: "darkorange", 20: "tomato"}
for ratio in [1, 5, 20]:
    costs = cost_curves[ratio]
    opt_idx = np.argmin(costs)
    ax.plot(thresholds, costs, label=f"C_FN/C_FP = 1:{ratio}", color=colors[ratio])
    ax.axvline(thresholds[opt_idx], color=colors[ratio], linestyle="--", alpha=0.6)
ax.set_xlabel("Threshold τ")
ax.set_ylabel("Gesamtkosten  C_FP·FP + C_FN·FN")
ax.set_title("Kosten-Kurven für 3 Kostenverhältnisse\n(gestrichelt: jeweiliges τ*)")
ax.legend(fontsize=8)
ax.set_xlim(0, 0.9)
ax.grid(True, alpha=0.3)

plt.tight_layout()
plot_path = os.path.join(OUTPUT_DIR, "tau_vs_cost_ratio.png")
fig.savefig(plot_path, dpi=150, bbox_inches="tight")
plt.close(fig)
print(f"→ Plot gespeichert: {plot_path}")
print("\n✓ τ*-Analyse abgeschlossen.")
