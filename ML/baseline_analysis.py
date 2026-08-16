"""
Nutzwertanalyse: schlaegt der Klassifikator die Trivialstrategien?

Kernfrage der Arbeit ist nicht "wie gut erkennt das Modell", sondern "wie
waehlt man die Entscheidungsschwelle, wenn ein uebersehener Cheater deutlich
teurer ist als ein Fehlalarm". Diese Frage hat eine Antwort, die von der
Guete des Modells unabhaengig ist — aber sie hat auch eine Grenze:

Es gibt zwei Strategien, die ganz ohne Modell auskommen:

    "niemanden sperren"  -> Kosten = C_FN * (Anzahl Cheater)
    "alle sperren"       -> Kosten = C_FP * (Anzahl Sauberer)

Ein Klassifikator ist nur dann etwas wert, wenn er bei gegebenem
Kostenverhaeltnis billiger ist als beide. Bei stark steigendem C_FN naehert
sich die optimale Schwelle null an — das Modell empfiehlt dann selbst, alle zu
sperren, und ist ueberfluessig. Dieses Skript bestimmt, wo dieser Kipppunkt
liegt.

Verwendung:
    python baseline_analysis.py
    python baseline_analysis.py --pred results_real/oof_predictions.csv
"""

import os
import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
import argparse
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
RULES_DIR    = os.path.join(PROJECT_ROOT, "RuleBased")

DEFAULT_DATA = os.path.join(SCRIPT_DIR, "training_data_real.csv")
DEFAULT_PRED = os.path.join(SCRIPT_DIR, "results_real", "oof_predictions.csv")
DEFAULT_OUT  = os.path.join(SCRIPT_DIR, "results_real")

# Kostenverhaeltnisse C_FN/C_FP, die untersucht werden. Fein genug, um den
# Kipppunkt auf wenige Einheiten genau zu bestimmen.
RATIOS = list(range(1, 61))

sys.path.insert(0, RULES_DIR)
try:
    from rule_based_detector import analyze_player
except ImportError:
    analyze_player = None


def cost_of(y_true, y_pred, c_fp, c_fn):
    y_true = np.asarray(y_true).astype(int)
    y_pred = np.asarray(y_pred).astype(int)
    fp = int(((y_true == 0) & (y_pred == 1)).sum())
    fn = int(((y_true == 1) & (y_pred == 0)).sum())
    return c_fp * fp + c_fn * fn


def best_over_thresholds(y, scores, thresholds, c_fp, c_fn):
    """Beste erreichbare Kosten und der zugehoerige Schwellwert."""
    best_cost, best_tau = np.inf, thresholds[0]
    for t in thresholds:
        c = cost_of(y, scores >= t, c_fp, c_fn)
        if c < best_cost:
            best_cost, best_tau = c, t
    return best_cost, best_tau


def main():
    parser = argparse.ArgumentParser(
        description="Vergleich gegen Trivialstrategien ueber Kostenverhaeltnisse"
    )
    parser.add_argument("--data", default=DEFAULT_DATA)
    parser.add_argument("--pred", default=DEFAULT_PRED)
    parser.add_argument("--outdir", default=DEFAULT_OUT)
    args = parser.parse_args()

    for p in (args.data, args.pred):
        if not os.path.exists(p):
            print(f"[FEHLER] Datei fehlt: {p}")
            sys.exit(1)

    df   = pd.read_csv(args.data)
    pred = pd.read_csv(args.pred)
    y    = df["Label"].values.astype(int)

    n_cheat = int(y.sum())
    n_clean = int((y == 0).sum())

    print("=" * 70)
    print("  Nutzwert des Klassifikators gegenueber Trivialstrategien")
    print("=" * 70)
    print(f"  {len(y)} Zeilen: {n_cheat} Cheater, {n_clean} sauber")
    print(f"\n  Trivialstrategien (C_FP = 1):")
    print(f"    'niemanden sperren' : Kosten = C_FN x {n_cheat}")
    print(f"    'alle sperren'      : Kosten = {n_clean}  (unabhaengig von C_FN)")

    rf_prob = pred["RF_Probability"].values
    ml_taus = np.round(np.arange(0.00, 1.01, 0.01), 2)

    have_rules = analyze_player is not None
    if have_rules:
        rule_score = np.array([analyze_player(r)[0] for _, r in df.iterrows()],
                              dtype=float)
        rule_taus = list(range(0, 101, 5))
        # Kombination: auffaellig, wenn eines der beiden Verfahren anschlaegt.
        # Beide Schwellen werden gemeinsam optimiert, sonst waere die
        # Kombination gegenueber den Einzelverfahren benachteiligt.

    rows = []
    for ratio in RATIOS:
        c_fp, c_fn = 1, ratio

        cost_none = c_fn * n_cheat
        cost_all  = c_fp * n_clean
        cost_triv = min(cost_none, cost_all)

        cost_ml, tau_ml = best_over_thresholds(y, rf_prob, ml_taus, c_fp, c_fn)

        entry = {
            "C_FN/C_FP":        ratio,
            "Kosten_niemand":   cost_none,
            "Kosten_alle":      cost_all,
            "Kosten_trivial":   cost_triv,
            "Kosten_ML":        cost_ml,
            "tau_ML":           tau_ml,
            "Ersparnis_ML_%":   round((cost_triv - cost_ml) / cost_triv * 100, 1)
                                if cost_triv else 0.0,
        }

        if have_rules:
            cost_rule, tau_rule = best_over_thresholds(y, rule_score, rule_taus,
                                                       c_fp, c_fn)
            entry["Kosten_Regeln"] = cost_rule
            entry["tau_Regeln"]    = tau_rule

            best_comb, best_pair = np.inf, (None, None)
            for tr in rule_taus:
                r_hit = rule_score >= tr
                for tm in ml_taus[::5]:
                    c = cost_of(y, r_hit | (rf_prob >= tm), c_fp, c_fn)
                    if c < best_comb:
                        best_comb, best_pair = c, (tr, tm)
            entry["Kosten_Kombi"]  = best_comb
            entry["tau_Kombi"]     = f"Regel>={best_pair[0]}, tau>={best_pair[1]:.2f}"
            entry["Ersparnis_Kombi_%"] = round(
                (cost_triv - best_comb) / cost_triv * 100, 1) if cost_triv else 0.0

        rows.append(entry)

    res = pd.DataFrame(rows)

    # ── Kipppunkt bestimmen ──────────────────────────────────────────
    def tipping_point(col):
        """Kleinstes Verhaeltnis, ab dem das Verfahren nicht mehr besser ist."""
        worse = res[res[col] >= res["Kosten_trivial"]]
        return int(worse["C_FN/C_FP"].min()) if not worse.empty else None

    print("\n" + "-" * 70)
    print("  Kosten im Vergleich (Auswahl)")
    print("-" * 70)
    show_cols = ["C_FN/C_FP", "Kosten_niemand", "Kosten_alle", "Kosten_ML", "tau_ML"]
    if have_rules:
        show_cols += ["Kosten_Regeln", "Kosten_Kombi"]
    print(res[res["C_FN/C_FP"].isin([1, 2, 5, 10, 15, 20, 30, 50])][show_cols]
          .to_string(index=False))

    print("\n" + "=" * 70)
    print("  KIPPPUNKT — ab wann lohnt sich das Verfahren nicht mehr?")
    print("=" * 70)

    for label, col in ([("ML-Modell", "Kosten_ML")] +
                       ([("Regeldetektor", "Kosten_Regeln"),
                         ("Kombination", "Kosten_Kombi")] if have_rules else [])):
        tp = tipping_point(col)
        if tp is None:
            print(f"  {label:<16}: bleibt bis C_FN/C_FP = {RATIOS[-1]} besser "
                  f"als jede Trivialstrategie")
        else:
            print(f"  {label:<16}: ab C_FN/C_FP = {tp} nicht mehr besser als "
                  f"'alle sperren'")

    print(f"\n  Lesart: unterhalb des Kipppunkts liefert das Verfahren einen")
    print(f"  messbaren Nutzen. Oberhalb ist die Trivialstrategie 'alle sperren'")
    print(f"  guenstiger — der Klassifikator traegt dann nichts mehr bei.")
    print(f"\n  Das ist keine Schwaeche der Methode, sondern eine Eigenschaft")
    print(f"  jeder Klassifikation unter extremer Kostenasymmetrie: wird ein")
    print(f"  Fehler beliebig teuer, gewinnt immer die Strategie, die ihn")
    print(f"  vollstaendig ausschliesst.")

    # ── Plot ─────────────────────────────────────────────────────────
    os.makedirs(args.outdir, exist_ok=True)
    fig, ax = plt.subplots(figsize=(9, 5.5))

    ax.plot(res["C_FN/C_FP"], res["Kosten_niemand"], "--", color="gray",
            label="trivial: niemanden sperren")
    ax.plot(res["C_FN/C_FP"], res["Kosten_alle"], ":", color="gray",
            label="trivial: alle sperren")
    ax.plot(res["C_FN/C_FP"], res["Kosten_ML"], "o-", markersize=3,
            label="ML-Modell (optimales τ)")
    if have_rules:
        ax.plot(res["C_FN/C_FP"], res["Kosten_Regeln"], "s-", markersize=3,
                label="Regeldetektor (optimale Schwelle)")
        ax.plot(res["C_FN/C_FP"], res["Kosten_Kombi"], "^-", markersize=3,
                label="Kombination (Regeln ODER ML)")

    tp = tipping_point("Kosten_ML")
    if tp:
        ax.axvline(tp, color="crimson", alpha=0.6, linewidth=1.2)
        ax.annotate(f"Kipppunkt ML\nC_FN/C_FP = {tp}",
                    xy=(tp, ax.get_ylim()[1] * 0.55),
                    xytext=(tp + 2, ax.get_ylim()[1] * 0.62),
                    color="crimson", fontsize=9)

    ax.set_xlabel("Kostenverhältnis  C_FN / C_FP\n"
                  "(wie viel teurer ist ein übersehener Cheater als ein Fehlalarm?)")
    ax.set_ylabel("Gesamtkosten bei optimaler Schwelle")
    ax.set_title("Nutzwert der Erkennungsverfahren gegenüber Trivialstrategien")
    ax.legend(fontsize=9)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()

    plot_path = os.path.join(args.outdir, "baseline_vergleich.png")
    fig.savefig(plot_path, dpi=150, bbox_inches="tight")
    plt.close(fig)

    csv_path = os.path.join(args.outdir, "baseline_vergleich.csv")
    res.to_csv(csv_path, index=False)

    print(f"\n[Gespeichert] {csv_path}")
    print(f"[Gespeichert] {plot_path}")


if __name__ == "__main__":
    main()
