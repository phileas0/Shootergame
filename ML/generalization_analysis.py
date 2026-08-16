"""
Generalisierbarkeit: wie stark haengt die berichtete Leistung davon ab,
wie bewertet wird?

Beantwortet den Teil "Generalisierbarkeit" der Forschungsfrage mit eigenen
Messwerten. Drei Verfahren auf identischen Daten und identischem Modell:

  A) Zufaelliger Split (StratifiedKFold)
     Das uebliche Vorgehen. Zeilen derselben Person landen gleichzeitig in
     Trainings- und Testmenge. Da Spielstil ueber Runden hinweg konstant
     ist, kann das Modell die Person am Bewegungsmuster wiedererkennen —
     ohne je gelernt zu haben, was Cheaten ausmacht.

  B) Personengruppierter Split (StratifiedGroupKFold)
     Alle Zeilen einer Person bleiben in derselben Falte. Entspricht dem
     Einsatzfall: fremde Spieler auf dem Server.

  C) Leave-one-person-out
     Ein Modell je Person, geprueft ausschliesslich an dieser einen nie
     gesehenen Person. Zeigt, wie stark die Leistung zwischen Spielern
     schwankt.

Die Differenz zwischen A und B ist die Ueberschaetzung, die entsteht, wenn
der Personenbezug ignoriert wird.

Verwendung:
    python generalization_analysis.py
    python generalization_analysis.py --seeds 10
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

from sklearn.model_selection import (
    StratifiedKFold, StratifiedGroupKFold, LeaveOneGroupOut, cross_val_predict
)
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import LogisticRegression
from sklearn.ensemble import RandomForestClassifier
from sklearn.pipeline import Pipeline
from sklearn.metrics import roc_auc_score, average_precision_score

from features import add_derived_features, get_feature_cols

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DATA = os.path.join(SCRIPT_DIR, "training_data_real.csv")
DEFAULT_OUT  = os.path.join(SCRIPT_DIR, "results_real")

N_SPLITS = 5
C_FP, C_FN = 1, 10


def make_model(seed):
    return Pipeline([
        ("scaler", StandardScaler()),
        ("clf", RandomForestClassifier(
            n_estimators=200, class_weight="balanced",
            min_samples_leaf=2, random_state=seed, n_jobs=-1))
    ])


def best_cost(y, prob):
    """Minimale Gesamtkosten ueber alle Schwellwerte."""
    best = np.inf
    for t in np.arange(0.0, 1.01, 0.01):
        pred = (prob >= t).astype(int)
        fp = int(((y == 0) & (pred == 1)).sum())
        fn = int(((y == 1) & (pred == 0)).sum())
        best = min(best, C_FP * fp + C_FN * fn)
    return best


def evaluate(y, prob):
    return {
        "ROC_AUC": roc_auc_score(y, prob),
        "AvgPrec": average_precision_score(y, prob),
        "Kosten":  best_cost(y, prob),
    }


def main():
    parser = argparse.ArgumentParser(description="Generalisierbarkeit der Bewertung")
    parser.add_argument("--data", default=DEFAULT_DATA)
    parser.add_argument("--outdir", default=DEFAULT_OUT)
    parser.add_argument("--seeds", type=int, default=10,
                        help="Wiederholungen mit verschiedenen Zufallszahlen")
    args = parser.parse_args()

    if not os.path.exists(args.data):
        print(f"[FEHLER] {args.data} fehlt. Zuerst load_labeled_data.py ausfuehren.")
        sys.exit(1)

    df = add_derived_features(pd.read_csv(args.data))
    feat = get_feature_cols(df)
    X = df[feat].values
    y = df["Label"].values.astype(int)
    groups = df["Person"].astype(str).values

    print("=" * 68)
    print("  Generalisierbarkeit: Einfluss des Bewertungsverfahrens")
    print("=" * 68)
    print(f"  {len(y)} Zeilen, {int(y.sum())} Cheater, "
          f"{len(np.unique(groups))} Personen, {len(feat)} Merkmale")
    print(f"  {args.seeds} Wiederholungen je Verfahren "
          f"(verschiedene Zufallszahlen)\n")

    rows = []

    # ── A und B ──────────────────────────────────────────────────────
    for seed in range(args.seeds):
        model = make_model(seed)

        cv_rand = StratifiedKFold(n_splits=N_SPLITS, shuffle=True, random_state=seed)
        p_rand = cross_val_predict(model, X, y, cv=cv_rand,
                                   method="predict_proba")[:, 1]
        rows.append({"Verfahren": "A zufaelliger Split", "seed": seed,
                     **evaluate(y, p_rand)})

        cv_grp = StratifiedGroupKFold(n_splits=N_SPLITS, shuffle=True,
                                      random_state=seed)
        p_grp = cross_val_predict(model, X, y, cv=cv_grp, groups=groups,
                                  method="predict_proba")[:, 1]
        rows.append({"Verfahren": "B gruppierter Split", "seed": seed,
                     **evaluate(y, p_grp)})

    res = pd.DataFrame(rows)
    agg = res.groupby("Verfahren").agg(["mean", "std"]).round(4)

    print("-" * 68)
    print("  A gegen B")
    print("-" * 68)
    print(f"\n  {'Verfahren':<22}{'ROC-AUC':>18}{'Avg. Precision':>18}{'Kosten':>10}")
    for name in ["A zufaelliger Split", "B gruppierter Split"]:
        sub = res[res["Verfahren"] == name]
        print(f"  {name:<22}"
              f"{sub['ROC_AUC'].mean():>11.4f} ±{sub['ROC_AUC'].std():.4f}"
              f"{sub['AvgPrec'].mean():>11.4f} ±{sub['AvgPrec'].std():.4f}"
              f"{sub['Kosten'].mean():>10.1f}")

    a = res[res["Verfahren"] == "A zufaelliger Split"]
    b = res[res["Verfahren"] == "B gruppierter Split"]
    d_auc = a["ROC_AUC"].mean() - b["ROC_AUC"].mean()
    d_cost = b["Kosten"].mean() - a["Kosten"].mean()

    print(f"\n  Ueberschaetzung durch zufaelligen Split:")
    print(f"    ROC-AUC : +{d_auc:.4f}  "
          f"({d_auc / b['ROC_AUC'].mean() * 100:+.1f} % gegenueber B)")
    print(f"    Kosten  : {-d_cost:.1f} (scheinbar guenstiger)")
    print(f"\n  Lesart: Wer nicht nach Personen trennt, berichtet eine um "
          f"{d_auc:.3f}\n  ROC-AUC-Punkte zu gute Zahl. Bei acht Spielern, die in "
          f"fast allen\n  Runden vorkommen, erkennt das Modell den Spielstil wieder, "
          f"nicht\n  den Cheat.")

    # ── C: Leave-one-person-out ──────────────────────────────────────
    print("\n" + "-" * 68)
    print("  C Leave-one-person-out")
    print("-" * 68)

    logo = LeaveOneGroupOut()
    per_person = []
    for train_idx, test_idx in logo.split(X, y, groups):
        person = groups[test_idx][0]
        n_pos = int(y[test_idx].sum())
        if n_pos == 0 or n_pos == len(test_idx):
            per_person.append({"Person": person, "n": len(test_idx),
                               "Cheater": n_pos, "ROC_AUC": np.nan})
            continue
        model = make_model(42).fit(X[train_idx], y[train_idx])
        prob = model.predict_proba(X[test_idx])[:, 1]
        per_person.append({"Person": person, "n": len(test_idx),
                           "Cheater": n_pos,
                           "ROC_AUC": roc_auc_score(y[test_idx], prob)})

    dfp = pd.DataFrame(per_person).sort_values("n", ascending=False)
    print(f"\n  {'Person':<12}{'Zeilen':>8}{'Cheater':>9}{'ROC-AUC':>10}")
    for r in dfp.itertuples():
        auc = "     –" if np.isnan(r.ROC_AUC) else f"{r.ROC_AUC:6.4f}"
        print(f"  {r.Person:<12}{r.n:>8}{r.Cheater:>9}{auc:>10}")

    valid = dfp["ROC_AUC"].dropna()
    if len(valid) > 1:
        print(f"\n  Mittelwert {valid.mean():.4f}, Streuung {valid.std():.4f}, "
              f"Spanne {valid.min():.4f} bis {valid.max():.4f}")
        print(f"\n  Die Spanne ist der eigentliche Befund: die Leistung haengt "
              f"stark davon\n  ab, WER geprueft wird. Ein an sieben Personen "
              f"trainiertes Modell ist bei\n  der achten nicht verlaesslich — "
              f"genau die Situation im echten Betrieb.")
    print(f"\n  Zeilen ohne Wert: die Person hat keine oder ausschliesslich "
          f"Cheat-Runden,\n  dort ist ROC-AUC nicht definiert.")

    # ── Ausgabe ──────────────────────────────────────────────────────
    os.makedirs(args.outdir, exist_ok=True)
    res.to_csv(os.path.join(args.outdir, "generalisierbarkeit_splits.csv"), index=False)
    dfp.to_csv(os.path.join(args.outdir, "generalisierbarkeit_je_person.csv"), index=False)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.8))

    data = [a["ROC_AUC"].values, b["ROC_AUC"].values]
    ax1.boxplot(data, labels=["zufälliger\nSplit", "gruppierter\nSplit"])
    ax1.set_ylabel("ROC-AUC")
    ax1.set_title(f"Einfluss des Splits ({args.seeds} Wiederholungen)")
    ax1.grid(True, alpha=0.3, axis="y")

    # Die Fallzahl gehoert zwingend an den Balken: Personen mit nur einer
    # Cheat-Runde erreichen leicht eine ROC-AUC von 1,00, was ohne diese
    # Angabe wie ein perfektes Ergebnis aussieht. Balken mit weniger als
    # drei Positivfaellen werden zusaetzlich blass gezeichnet.
    v = dfp.dropna(subset=["ROC_AUC"]).iloc[::-1]
    farben = ["#9ecae1" if c < 3 else "#3182bd" for c in v["Cheater"]]
    ax2.barh(v["Person"], v["ROC_AUC"], color=farben)
    for i, (auc, n) in enumerate(zip(v["ROC_AUC"], v["Cheater"])):
        ax2.text(min(auc + 0.02, 0.98), i, f"n={n}", va="center", fontsize=8)
    ax2.axvline(0.5, color="crimson", linestyle="--", alpha=0.7,
                label="Zufallsniveau")
    ax2.set_xlim(0, 1.15)
    ax2.set_xlabel("ROC-AUC   (n = Anzahl Cheat-Runden dieser Person)")
    ax2.set_title("Leave-one-person-out")
    ax2.legend(fontsize=8, loc="lower right")
    ax2.grid(True, alpha=0.3, axis="x")
    ax2.text(0.02, -0.9, "blass = weniger als 3 Cheat-Runden, nicht belastbar",
             fontsize=7.5, style="italic", color="#555555")

    fig.tight_layout()
    plot_path = os.path.join(args.outdir, "generalisierbarkeit.png")
    fig.savefig(plot_path, dpi=150, bbox_inches="tight")
    plt.close(fig)

    print(f"\n[Gespeichert] {os.path.join(args.outdir, 'generalisierbarkeit_splits.csv')}")
    print(f"[Gespeichert] {os.path.join(args.outdir, 'generalisierbarkeit_je_person.csv')}")
    print(f"[Gespeichert] {plot_path}")


if __name__ == "__main__":
    main()
