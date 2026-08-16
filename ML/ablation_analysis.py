"""
Feature-Ablation: haengt das Modell an Stoergroessen?

Die Merkmalswichtigkeit des Random Forest listet auf den Plaetzen 3 und 4
SessionDurationSeconds und KillDeathRatio. Beides sind keine Cheat-Indikatoren:

  SessionDurationSeconds  ist die Rundenlaenge. Ein Spieler, der frueher
                          verbindet oder laenger durchhaelt, cheatet deswegen
                          nicht. Das Merkmal kann aber mit der Erhebungs-
                          situation korrelieren — etwa wenn Cheat-Runden
                          systematisch kuerzer oder laenger waren.

  KillDeathRatio          misst Spielstaerke. Cheater haben ein hohes
                          Verhaeltnis, gute Spieler aber auch. Stuetzt sich
                          das Modell darauf, bestraft es Koennen statt Betrug
                          — im Einsatz gegen unbekannte Spieler ist das ein
                          direkter Weg zu Fehlalarmen.

Dieses Skript entfernt die Merkmale einzeln und gemeinsam und misst, was
davon abhaengt. Bewertet wird mit demselben personengruppierten Protokoll wie
im Haupttraining, gemittelt ueber mehrere Faltenaufteilungen.

Lesart des Ergebnisses:
  kaum Verlust   -> das Modell stuetzt sich nicht wesentlich darauf, die
                    Merkmale koennen entfernt werden und die Ergebnisse sind
                    robuster als befuerchtet
  klarer Verlust -> das Modell nutzt Stoergroessen; das ist eine ehrliche
                    Limitierung und gehoert in die Diskussion

Verwendung:
    python ablation_analysis.py
    python ablation_analysis.py --seeds 10
"""

import os
import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
import argparse
import numpy as np
import pandas as pd

from sklearn.model_selection import StratifiedGroupKFold, cross_val_predict
from sklearn.preprocessing import StandardScaler
from sklearn.ensemble import RandomForestClassifier
from sklearn.pipeline import Pipeline
from sklearn.metrics import roc_auc_score, average_precision_score

from features import add_derived_features, get_feature_cols

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DATA = os.path.join(SCRIPT_DIR, "training_data_real.csv")
DEFAULT_OUT  = os.path.join(SCRIPT_DIR, "results_real")

N_SPLITS   = 5
C_FP, C_FN = 1, 10

# Merkmal -> Begruendung, warum es als Stoergroesse verdaechtig ist
SUSPECTS = {
    "SessionDurationSeconds": "Rundenlaenge, kein Verhaltensmerkmal",
    "KillDeathRatio":         "Spielstaerke, nicht Betrug",
}


def make_model(seed):
    return Pipeline([
        ("scaler", StandardScaler()),
        ("clf", RandomForestClassifier(
            n_estimators=200, class_weight="balanced",
            min_samples_leaf=2, random_state=seed, n_jobs=-1))
    ])


def best_cost(y, prob):
    best = np.inf
    for t in np.arange(0.0, 1.01, 0.01):
        pred = (prob >= t).astype(int)
        fp = int(((y == 0) & (pred == 1)).sum())
        fn = int(((y == 1) & (pred == 0)).sum())
        best = min(best, C_FP * fp + C_FN * fn)
    return best


def evaluate(df, y, groups, cols, seeds):
    """Gemittelte Out-of-fold-Bewertung fuer einen Merkmalssatz."""
    X = df[cols].values
    aucs, aps, costs = [], [], []
    for seed in range(seeds):
        cv = StratifiedGroupKFold(n_splits=N_SPLITS, shuffle=True, random_state=seed)
        p = cross_val_predict(make_model(seed), X, y, cv=cv, groups=groups,
                              method="predict_proba")[:, 1]
        aucs.append(roc_auc_score(y, p))
        aps.append(average_precision_score(y, p))
        costs.append(best_cost(y, p))
    return dict(ROC_AUC=np.mean(aucs), ROC_AUC_std=np.std(aucs),
                AvgPrec=np.mean(aps), Kosten=np.mean(costs))


def main():
    parser = argparse.ArgumentParser(description="Feature-Ablation")
    parser.add_argument("--data", default=DEFAULT_DATA)
    parser.add_argument("--outdir", default=DEFAULT_OUT)
    parser.add_argument("--seeds", type=int, default=10)
    args = parser.parse_args()

    if not os.path.exists(args.data):
        print(f"[FEHLER] {args.data} fehlt.")
        sys.exit(1)

    df = add_derived_features(pd.read_csv(args.data))
    full = get_feature_cols(df)
    y = df["Label"].values.astype(int)
    groups = df["Person"].astype(str).values

    present = [c for c in SUSPECTS if c in full]

    print("=" * 70)
    print("  Feature-Ablation: Einfluss verdaechtiger Merkmale")
    print("=" * 70)
    print(f"  {len(df)} Zeilen, {int(y.sum())} Cheater, "
          f"{len(np.unique(groups))} Personen")
    print(f"  Voller Merkmalssatz: {len(full)} Merkmale")
    print(f"  {args.seeds} Faltenaufteilungen je Variante\n")
    for c in present:
        print(f"  Verdaechtig: {c:<24} {SUSPECTS[c]}")

    varianten = [("voller Satz", full)]
    for c in present:
        varianten.append((f"ohne {c}", [x for x in full if x != c]))
    if len(present) > 1:
        varianten.append(("ohne beide", [x for x in full if x not in present]))

    rows = []
    base = None
    for name, cols in varianten:
        res = evaluate(df, y, groups, cols, args.seeds)
        if base is None:
            base = res
        rows.append(dict(Variante=name, Merkmale=len(cols),
                         **{k: round(v, 4) for k, v in res.items()},
                         d_ROC_AUC=round(res["ROC_AUC"] - base["ROC_AUC"], 4),
                         d_Kosten=round(res["Kosten"] - base["Kosten"], 1)))
        print(f"  {name} berechnet", end="\r")

    print(" " * 40, end="\r")
    out = pd.DataFrame(rows)

    print("\n" + "-" * 70)
    print(f"  {'Variante':<28}{'Merkm.':>8}{'ROC-AUC':>18}{'Kosten':>10}{'dAUC':>9}")
    print("  " + "-" * 71)
    for r in out.itertuples():
        print(f"  {r.Variante:<28}{r.Merkmale:>8}"
              f"{r.ROC_AUC:>11.4f} ±{r.ROC_AUC_std:.3f}"
              f"{r.Kosten:>10.1f}{r.d_ROC_AUC:>+9.4f}")

    print("\n" + "=" * 70)
    print("  BEWERTUNG")
    print("=" * 70)

    # Groesste Abweichung in BEIDE Richtungen — ein Weglassen kann die
    # Leistung auch verbessern, wenn das Merkmal nur Rauschen beitraegt.
    deltas   = out.iloc[1:]["d_ROC_AUC"] if len(out) > 1 else pd.Series([0.0])
    groesste = deltas.abs().max()
    richtung = deltas.loc[deltas.abs().idxmax()]
    streuung = base["ROC_AUC_std"]

    print(f"\n  Groesste Aenderung durch Weglassen : {richtung:+.4f} ROC-AUC"
          f"  ({'besser' if richtung > 0 else 'schlechter'} ohne das Merkmal)")
    print(f"  Streuung ueber Faltenaufteilungen  : ±{streuung:.4f}")

    if groesste <= streuung:
        print(f"\n  Die Aenderung liegt INNERHALB der Streuung. Das Modell haengt")
        print(f"  nicht nachweisbar an diesen Merkmalen — die Ergebnisse sind")
        print(f"  robuster als die Merkmalswichtigkeit vermuten liess. Fuer die")
        print(f"  Arbeit ist das ein entlastender Befund.")
    else:
        print(f"\n  Die Aenderung liegt AUSSERHALB der Streuung. Das Modell stuetzt")
        print(f"  sich messbar auf Stoergroessen. Das gehoert als Limitierung in")
        print(f"  die Diskussion: die berichtete Leistung ist zum Teil auf")
        print(f"  Rundenlaenge bzw. Spielstaerke zurueckzufuehren, nicht auf")
        print(f"  Cheat-Verhalten.")

    os.makedirs(args.outdir, exist_ok=True)
    p = os.path.join(args.outdir, "feature_ablation.csv")
    out.to_csv(p, index=False)
    print(f"\n[Gespeichert] {p}")


if __name__ == "__main__":
    main()
