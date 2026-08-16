"""
Schritt 3: ML-Modell Training
Bachelorarbeit – Kostenasymmetrisches ML-Klassifikationsmodell zur Cheat-Erkennung

Modelle:    Logistic Regression, Random Forest
Features:   siehe features.py (HeadshotRate ausgeschlossen, weil dauerhaft 0;
            AimAngularError* sind seit der Mehrpunkt-Messung aktiv und werden genutzt)
Klassen:    0 = legitimer Spieler, 1 = Cheater
Imbalance:  ~95% / 5% → class_weight='balanced' + SMOTE optional
Evaluation: Accuracy, Precision, Recall, F1, ROC-AUC, Cost-Matrix
Threshold:  τ-Variation 0.1–0.9 → optimaler Schwellwert für kostenasymmetrischen Einsatz

Kosten-Annahme (nach Bachelorarbeit Kap. 2.3):
    C_FP = 1   (legitimer Spieler fälschlicherweise gebannt)   → niedrige Kosten
    C_FN = 10  (Cheater nicht erkannt)                          → hohe Kosten
    → Gesamtkosten = C_FP * FP + C_FN * FN
    → Ziel: Threshold τ so wählen, dass Gesamtkosten minimal
"""

import os
# Windows-Konsole nutzt standardmaessig cp1252 und bricht bei Zeichen wie
# tau, lambda oder Pfeilen mit UnicodeEncodeError ab. UTF-8 erzwingen,
# damit die mathematische Notation in der Ausgabe erhalten bleibt.
import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
import json
import argparse
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')   # kein Display nötig (Headless)
import matplotlib.pyplot as plt

from sklearn.model_selection import (
    train_test_split, StratifiedKFold, StratifiedGroupKFold,
    cross_val_score, cross_val_predict
)
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import LogisticRegression
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import (
    classification_report, confusion_matrix,
    roc_auc_score, roc_curve, precision_recall_curve,
    average_precision_score, ConfusionMatrixDisplay
)
from sklearn.pipeline import Pipeline
import joblib

# ─────────────────────────────────────────────
# 0. Konfiguration
# ─────────────────────────────────────────────
SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))

# Datenquelle und Zielordner per Argument. Ohne Angabe bleibt es beim
# synthetischen training_data.csv, damit der bisherige Lauf als
# Vergleichspunkt reproduzierbar bleibt:
#     python train_model.py
#     python train_model.py training_data_real.csv --outdir results_real
_parser = argparse.ArgumentParser(description="Cheat-Erkennung – Modelltraining")
_parser.add_argument("data", nargs="?", default="training_data.csv",
                     help="Trainingsdaten (Standard: training_data.csv)")
_parser.add_argument("--outdir", default="results",
                     help="Zielordner fuer Modelle und Auswertung "
                          "(Standard: results). predict.py liest aus results/.")
_args = _parser.parse_args()

DATA_PATH = _args.data if os.path.isabs(_args.data) else os.path.join(SCRIPT_DIR, _args.data)
if not os.path.exists(DATA_PATH):
    print(f"[FEHLER] Datei nicht gefunden: {DATA_PATH}")
    sys.exit(1)

OUTPUT_DIR = _args.outdir if os.path.isabs(_args.outdir) else os.path.join(SCRIPT_DIR, _args.outdir)
os.makedirs(OUTPUT_DIR, exist_ok=True)

# Kostenmatrix: C_FP < C_FN → wir bevorzugen False Negatives zu vermeiden
C_FP = 1    # Kosten: legitimer Spieler wird gebannt
C_FN = 10   # Kosten: Cheater wird nicht erkannt

RANDOM_STATE = 42
TEST_SIZE    = 0.2   # 80/20 Split

# Anzahl gemittelter Faltenaufteilungen im gruppierten Modus.
#
# Grund: bei acht Personen gibt es nur wenige Moeglichkeiten, die Gruppen auf
# fuenf Falten zu verteilen, und das Ergebnis haengt spuerbar davon ab, welche
# gewaehlt wird. Gemessen ueber 15 Aufteilungen schwankt die ROC-AUC zwischen
# 0,696 und 0,844 — eine einzelne Zahl waere also Zufall der Seed-Wahl.
# Gemittelt wird deshalb ueber mehrere Aufteilungen; berichtet wird der
# Mittelwert samt Streuung.
N_SEEDS = 10

# Feature-Definition zentral aus features.py — siehe dort die Begründung,
# warum Training und Inferenz dieselbe Quelle nutzen müssen.
from features import (
    ZERO_FEATURES, META_COLS, add_derived_features, get_feature_cols
)


# ─────────────────────────────────────────────
# 1. Daten laden und aufbereiten
# ─────────────────────────────────────────────
print("=" * 60)
print("Schritt 3 – ML-Modell Training")
print("=" * 60)

df = pd.read_csv(DATA_PATH)
print(f"\n[Daten] {len(df)} Sessions geladen aus: {DATA_PATH}")
print(f"[Daten] Klassenverteilung:\n{df['Label'].value_counts().to_string()}")
print(f"         → {(df['Label']==0).sum()} legitim (Label=0)")
print(f"         → {(df['Label']==1).sum()} Cheater (Label=1)")

# Abgeleitete Features ergänzen (OnTargetRatio) und Feature-Spalten bestimmen
df = add_derived_features(df)
feature_cols = get_feature_cols(df)
print(f"\n[Features] {len(feature_cols)} Features genutzt:")
print("  " + ", ".join(feature_cols))
print(f"\n[Ignoriert] {ZERO_FEATURES}  (dauerhaft 0 in echten UE5-Daten)")

X = df[feature_cols].values
y = df["Label"].values

# ── Bewertungsverfahren wählen ────────────────────────────────────────
#
# Echte Daten (aus load_labeled_data.py) bringen eine Spalte Person mit.
# Ist sie vorhanden, wird gruppiert kreuzvalidiert statt einmal zufällig
# geteilt. Zwei Gründe:
#
#   1. Personenbezug. Dieselben acht Personen spielen in fast allen Runden
#      mit. Ein zufälliger Split legt Runden derselben Person gleichzeitig
#      in Trainings- UND Testmenge. Das Modell kann dann Spielstil
#      wiedererkennen, statt Cheat-Verhalten zu lernen — die Kennzahlen
#      fallen zu gut aus und halten im Einsatz gegen fremde Spieler nicht.
#
#   2. Größe der Testmenge. Bei 28 Cheater-Zeilen enthielte eine 20-%-Test-
#      menge etwa sechs davon. Eine einzige Fehlklassifikation verschöbe den
#      Recall um 17 Prozentpunkte — eine Zahl, die man nicht berichten kann.
#
# StratifiedGroupKFold hält alle Zeilen einer Person zusammen in derselben
# Falte und die Klassenverteilung je Falte möglichst stabil. Jede Zeile
# bekommt dadurch genau eine Vorhersage von einem Modell, das diese Person
# nie gesehen hat (Out-of-Fold). Ausgewertet wird anschließend über alle
# Zeilen statt über ein Fünftel.
GROUPS   = None
GROUP_BY = None

if "Person" in df.columns and df["Person"].astype(str).str.strip().ne("").all():
    GROUPS   = df["Person"].astype(str).values
    GROUP_BY = "Person"

if GROUPS is None:
    X_train, X_test, y_train, y_test = train_test_split(
        X, y, test_size=TEST_SIZE, random_state=RANDOM_STATE, stratify=y
    )
    print(f"\n[Bewertung] Einzelner Train/Test-Split "
          f"(Train: {len(X_train)} | Test: {len(X_test)})")
    print(f"            Keine Spalte 'Person' vorhanden — keine Gruppierung "
          f"moeglich.")
    N_SPLITS = 5
else:
    # Nicht mehr Falten als Gruppen, die überhaupt Cheater enthalten —
    # sonst entstehen Testfalten ohne eine einzige positive Zeile und der
    # Recall ist dort undefiniert.
    n_pos_groups = int(pd.Series(GROUPS)[y == 1].nunique())
    N_SPLITS = max(2, min(5, n_pos_groups))

    # Bewertet wird über alle Zeilen; ein separater Testsatz entfällt.
    X_train, y_train = X, y
    X_test,  y_test  = X, y

    print(f"\n[Bewertung] Gruppierte Kreuzvalidierung ueber '{GROUP_BY}'")
    print(f"            {len(np.unique(GROUPS))} Gruppen, davon "
          f"{n_pos_groups} mit mindestens einer Cheater-Zeile")
    print(f"            {N_SPLITS} Falten, Bewertung out-of-fold ueber alle "
          f"{len(y)} Zeilen")
    print(f"            Zeilen je Gruppe: " +
          ", ".join(f"{g}={n}" for g, n in
                    pd.Series(GROUPS).value_counts().items()))


# ─────────────────────────────────────────────
# 2. Hilfsfunktionen
# ─────────────────────────────────────────────

def compute_total_cost(y_true, y_pred, c_fp=C_FP, c_fn=C_FN):
    """Gesamtkosten nach Kostenmatrix C_FP * FP + C_FN * FN"""
    cm = confusion_matrix(y_true, y_pred)
    # cm[0,1] = FP (legit → Cheater), cm[1,0] = FN (Cheater → legit)
    fp = cm[0, 1] if cm.shape == (2, 2) else 0
    fn = cm[1, 0] if cm.shape == (2, 2) else 0
    return c_fp * fp + c_fn * fn


def evaluate_thresholds(model_name, y_true, y_prob, thresholds=None):
    """
    Berechnet für τ ∈ [0.1, 0.9] Precision, Recall, F1, Kosten.
    Gibt optimalen Threshold (min. Gesamtkosten) zurück.
    """
    if thresholds is None:
        thresholds = np.arange(0.1, 1.0, 0.1)

    results = []
    for tau in thresholds:
        y_pred_tau = (y_prob >= tau).astype(int)
        cm = confusion_matrix(y_true, y_pred_tau, labels=[0, 1])
        tn, fp, fn, tp = cm.ravel() if cm.size == 4 else (0, 0, 0, 0)

        precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
        recall    = tp / (tp + fn) if (tp + fn) > 0 else 0.0
        f1        = (2 * precision * recall / (precision + recall)
                     if (precision + recall) > 0 else 0.0)
        cost      = C_FP * fp + C_FN * fn

        results.append({
            "tau":       round(tau, 1),
            "TP": tp, "FP": fp, "FN": fn, "TN": tn,
            "Precision": round(precision, 4),
            "Recall":    round(recall,    4),
            "F1":        round(f1,        4),
            "Cost":      cost
        })

    df_res = pd.DataFrame(results)
    best_row = df_res.loc[df_res["Cost"].idxmin()]
    print(f"\n[{model_name}] Threshold-Analyse (C_FP={C_FP}, C_FN={C_FN}):")
    print(df_res[["tau", "TP", "FP", "FN", "TN", "Precision", "Recall", "F1", "Cost"]].to_string(index=False))
    print(f"\n  → Optimaler Threshold: τ = {best_row['tau']:.1f}  (Gesamtkosten = {best_row['Cost']:.0f})")

    # CSV speichern
    out_path = os.path.join(OUTPUT_DIR, f"threshold_analysis_{model_name.lower().replace(' ','_')}.csv")
    df_res.to_csv(out_path, index=False)
    print(f"  → Gespeichert: {out_path}")

    return df_res, float(best_row["tau"])


def make_cv(seed=RANDOM_STATE):
    """Passenden Kreuzvalidierer für den gewählten Bewertungsmodus."""
    if GROUPS is None:
        return StratifiedKFold(n_splits=N_SPLITS, shuffle=True,
                               random_state=seed)
    return StratifiedGroupKFold(n_splits=N_SPLITS, shuffle=True,
                                random_state=seed)


def fit_and_predict(pipeline, short_name):
    """
    Trainiert und liefert (cv_scores, y_prob).

    Ohne Gruppierung: Kreuzvalidierung auf der Trainingsmenge zur Kontrolle,
    danach ein Fit und Vorhersage auf der separaten Testmenge — das bisherige
    Verfahren.

    Mit Gruppierung: die Wahrscheinlichkeiten stammen aus der Kreuzvalidierung
    selbst (out-of-fold). Jede Zeile wird von einem Modell bewertet, das die
    zugehoerige Person nie gesehen hat. Anschliessend wird das Modell noch
    einmal auf allen Daten gefittet — dieses Modell wird gespeichert und
    spaeter von predict.py benutzt, bewertet wird es aber nie auf den eigenen
    Trainingsdaten.
    """
    cv = make_cv()
    fit_kwargs = {} if GROUPS is None else {"groups": GROUPS}

    cv_scores = cross_val_score(pipeline, X_train, y_train, cv=cv,
                                scoring="roc_auc", **fit_kwargs)


    # Enthält eine Falte nur eine Klasse, ist ROC-AUC dort undefiniert und
    # sklearn liefert NaN. Bei acht Gruppen, von denen eine gar keine
    # Cheater-Zeile hat, ist das nicht zu vermeiden. Der Mittelwert wird
    # deshalb über die gültigen Falten gebildet und der Ausfall benannt —
    # die berichtete Kennzahl stammt ohnehin aus den Out-of-Fold-Vorhersagen
    # über alle Zeilen, nicht aus diesem Mittelwert.
    n_nan = int(np.isnan(cv_scores).sum())
    print(f"\n[{short_name}] {N_SPLITS}-Fold CV ROC-AUC: "
          f"{np.nanmean(cv_scores):.4f} ± {np.nanstd(cv_scores):.4f}")
    print(f"     Scores: {np.round(cv_scores, 4)}")
    if n_nan:
        print(f"     {n_nan} Falte(n) ohne Cheater-Zeile — dort ist ROC-AUC "
              f"undefiniert und geht nicht in den Mittelwert ein.")

    if GROUPS is None:
        pipeline.fit(X_train, y_train)
        y_prob = pipeline.predict_proba(X_test)[:, 1]
    else:
        # Ueber mehrere Faltenaufteilungen mitteln — siehe N_SEEDS oben.
        # Die Einzelwerte werden mitberichtet, damit die Streuung sichtbar
        # bleibt und nicht als Scheingenauigkeit verschwindet.
        probs, aucs = [], []
        for seed in range(N_SEEDS):
            p = cross_val_predict(pipeline, X, y, cv=make_cv(seed),
                                  groups=GROUPS, method="predict_proba")[:, 1]
            probs.append(p)
            aucs.append(roc_auc_score(y, p))

        y_prob = np.mean(probs, axis=0)
        print(f"     Out-of-fold ueber {N_SEEDS} Faltenaufteilungen: "
              f"ROC-AUC {np.mean(aucs):.4f} ± {np.std(aucs):.4f} "
              f"(Spanne {min(aucs):.4f}–{max(aucs):.4f})")
        print(f"     Berichtet wird die gemittelte Vorhersage.")

        pipeline.fit(X, y)

    return cv_scores, y_prob


def print_evaluation(model_name, y_true, y_pred, y_prob):
    """Vollständige Modell-Evaluation inkl. Kosten"""
    print(f"\n{'─'*50}")
    print(f"  Modell: {model_name}")
    print(f"{'─'*50}")
    print(classification_report(y_true, y_pred,
                                 target_names=["Legitim (0)", "Cheater (1)"],
                                 digits=4))
    cm = confusion_matrix(y_true, y_pred)
    print(f"Confusion Matrix:\n{cm}")
    roc = roc_auc_score(y_true, y_prob)
    ap  = average_precision_score(y_true, y_prob)
    cost = compute_total_cost(y_true, y_pred)
    print(f"ROC-AUC:            {roc:.4f}")
    print(f"Avg. Precision:     {ap:.4f}")
    print(f"Gesamtkosten (τ=0.5): {cost}  (C_FP={C_FP}·FP={cm[0,1]} + C_FN={C_FN}·FN={cm[1,0]})")
    return roc, ap, cost


# ─────────────────────────────────────────────
# 3. Logistic Regression
# ─────────────────────────────────────────────
print("\n" + "=" * 60)
print("  Modell 1: Logistic Regression")
print("=" * 60)

lr_pipeline = Pipeline([
    ("scaler", StandardScaler()),
    ("clf",    LogisticRegression(
        class_weight="balanced",  # kompensiert 95/5 Imbalance
        max_iter=1000,
        random_state=RANDOM_STATE
    ))
])

cv_scores, lr_prob = fit_and_predict(lr_pipeline, "LR")
lr_pred = (lr_prob >= 0.5).astype(int)

lr_roc, lr_ap, lr_cost = print_evaluation("Logistic Regression", y_test, lr_pred, lr_prob)
lr_threshold_df, lr_best_tau = evaluate_thresholds("Logistic Regression", y_test, lr_prob)

# Optimaler Threshold anwenden
lr_pred_opt = (lr_prob >= lr_best_tau).astype(int)
lr_cost_opt = compute_total_cost(y_test, lr_pred_opt)
print(f"\n[LR] Mit τ={lr_best_tau:.1f}: Gesamtkosten = {lr_cost_opt}")


# ─────────────────────────────────────────────
# 4. Random Forest
# ─────────────────────────────────────────────
print("\n" + "=" * 60)
print("  Modell 2: Random Forest")
print("=" * 60)

rf_pipeline = Pipeline([
    ("scaler", StandardScaler()),
    ("clf",    RandomForestClassifier(
        n_estimators=200,
        class_weight="balanced",
        max_depth=None,
        min_samples_leaf=2,
        random_state=RANDOM_STATE,
        n_jobs=-1
    ))
])

cv_scores_rf, rf_prob = fit_and_predict(rf_pipeline, "RF")
rf_pred = (rf_prob >= 0.5).astype(int)

rf_roc, rf_ap, rf_cost = print_evaluation("Random Forest", y_test, rf_pred, rf_prob)
rf_threshold_df, rf_best_tau = evaluate_thresholds("Random Forest", y_test, rf_prob)

rf_pred_opt = (rf_prob >= rf_best_tau).astype(int)
rf_cost_opt = compute_total_cost(y_test, rf_pred_opt)
print(f"\n[RF] Mit τ={rf_best_tau:.1f}: Gesamtkosten = {rf_cost_opt}")


# ─────────────────────────────────────────────
# 5. Feature Importance (Random Forest)
# ─────────────────────────────────────────────
rf_clf       = rf_pipeline.named_steps["clf"]
importances  = pd.Series(rf_clf.feature_importances_, index=feature_cols)
importances  = importances.sort_values(ascending=False)

print("\n[RF] Feature Importances (Top 10):")
print(importances.head(10).round(4).to_string())

fi_path = os.path.join(OUTPUT_DIR, "feature_importance.csv")
importances.reset_index().rename(
    columns={"index": "Feature", 0: "Importance"}
).to_csv(fi_path, index=False)
print(f"→ Gespeichert: {fi_path}")


# ─────────────────────────────────────────────
# 5b. Recall je Cheat-Art
#     Nur mit echten Daten möglich (Spalte CheatType aus load_labeled_data.py)
# ─────────────────────────────────────────────
# GROUPS-Bedingung ist nötig, nicht kosmetisch: nur im gruppierten Modus
# liegt für JEDE Zeile von df eine Out-of-Fold-Vorhersage vor. Beim
# einzelnen Split gilt rf_prob nur für die Testmenge und liesse sich nicht
# zeilenweise auf df beziehen.
if "CheatType" in df.columns and GROUPS is not None:
    print("\n" + "=" * 60)
    print("  Recall je Cheat-Art (Random Forest, τ = "
          f"{rf_best_tau:.2f})")
    print("=" * 60)

    cheat_series = df["CheatType"].fillna("").astype(str)
    rf_pred_opt_all = (rf_prob >= rf_best_tau).astype(int)

    all_cheats = sorted({t.strip()
                         for entry in cheat_series if entry.strip()
                         for t in entry.split("+") if t.strip()})

    rows = []
    for cheat in all_cheats:
        # "beteiligt": Cheat war aktiv, evtl. zusammen mit anderen
        involved = cheat_series.apply(
            lambda e: cheat in [t.strip() for t in e.split("+")]
        ).values
        # "sortenrein": genau dieser eine Cheat und sonst nichts
        alone = (cheat_series.str.strip() == cheat).values

        def recall_of(mask):
            n = int(mask.sum())
            if n == 0:
                return n, None
            return n, float(rf_pred_opt_all[mask].sum()) / n

        n_inv, rec_inv = recall_of(involved)
        n_alo, rec_alo = recall_of(alone)

        rows.append({
            "Cheat":            cheat,
            "n_beteiligt":      n_inv,
            "Recall_beteiligt": round(rec_inv, 4) if rec_inv is not None else None,
            "n_sortenrein":     n_alo,
            "Recall_sortenrein": round(rec_alo, 4) if rec_alo is not None else None,
        })

    df_cheat = pd.DataFrame(rows)

    print(f"\n  {'Cheat':<14}{'n':>5}{'Recall':>9}   |{'n rein':>8}{'Recall':>9}")
    print("  " + "-" * 52)
    for r in df_cheat.itertuples():
        rec_i = f"{r.Recall_beteiligt*100:6.1f} %" if r.Recall_beteiligt is not None else "     – "
        rec_a = f"{r.Recall_sortenrein*100:6.1f} %" if r.Recall_sortenrein is not None else "     – "
        print(f"  {r.Cheat:<14}{r.n_beteiligt:>5}{rec_i:>9}   |"
              f"{r.n_sortenrein:>8}{rec_a:>9}")

    print("\n  'beteiligt'  = Cheat war aktiv, evtl. gemeinsam mit anderen.")
    print("  'sortenrein' = ausschliesslich dieser Cheat war aktiv.")
    print("\n  Nur die rechte Spalte erlaubt eine Aussage darueber, ob DIESER")
    print("  Cheat erkannt wird. Links kann die Erkennung von einem gleich-")
    print("  zeitig aktiven anderen Cheat stammen — der Wert ist dann zu hoch.")

    cheat_path = os.path.join(OUTPUT_DIR, "recall_per_cheat.csv")
    df_cheat.to_csv(cheat_path, index=False)
    print(f"\n→ Gespeichert: {cheat_path}")


# ─────────────────────────────────────────────
# 5c. Vorhersagen je Zeile sichern
#     Grundlage für den Vergleich mit dem regelbasierten Detektor
# ─────────────────────────────────────────────
if GROUPS is not None:
    # Nur im gruppierten Modus liegt für jede Zeile eine Out-of-Fold-
    # Vorhersage vor. Sie ist die einzige, die man dem Regeldetektor
    # gegenüberstellen darf: beide Verfahren haben die Zeile dann nicht
    # zur Entscheidungsfindung benutzt.
    pred_cols = [c for c in ["SessionFile", "PlayerID", "Person", "Label", "CheatType"]
                 if c in df.columns]
    df_pred = df[pred_cols].copy()
    df_pred["RF_Probability"] = rf_prob
    df_pred["LR_Probability"] = lr_prob
    df_pred["RF_Label"] = (rf_prob >= rf_best_tau).astype(int)
    df_pred["LR_Label"] = (lr_prob >= lr_best_tau).astype(int)

    pred_path = os.path.join(OUTPUT_DIR, "oof_predictions.csv")
    df_pred.to_csv(pred_path, index=False)
    print(f"\n[Vorhersagen] Out-of-Fold je Zeile gespeichert: {pred_path}")


# ─────────────────────────────────────────────
# 6. Plots
# ─────────────────────────────────────────────
print("\n[Plots] Erstelle Grafiken...")

fig, axes = plt.subplots(2, 3, figsize=(18, 11))
fig.suptitle("Schritt 3 – Cheat-Erkennung ML-Evaluation\n(C_FP=1, C_FN=10)", fontsize=14)

# ── Plot 1: ROC-Kurven ──
ax = axes[0, 0]
for name, prob in [("Logistic Regression", lr_prob), ("Random Forest", rf_prob)]:
    fpr, tpr, _ = roc_curve(y_test, prob)
    auc = roc_auc_score(y_test, prob)
    ax.plot(fpr, tpr, label=f"{name} (AUC={auc:.3f})")
ax.plot([0,1],[0,1],"--", color="gray", label="Zufall")
ax.set_xlabel("False Positive Rate"); ax.set_ylabel("True Positive Rate")
ax.set_title("ROC-Kurven"); ax.legend(); ax.grid(True, alpha=0.3)

# ── Plot 2: Precision-Recall ──
ax = axes[0, 1]
for name, prob in [("Logistic Regression", lr_prob), ("Random Forest", rf_prob)]:
    prec, rec, _ = precision_recall_curve(y_test, prob)
    ap = average_precision_score(y_test, prob)
    ax.plot(rec, prec, label=f"{name} (AP={ap:.3f})")
ax.set_xlabel("Recall"); ax.set_ylabel("Precision")
ax.set_title("Precision-Recall-Kurven"); ax.legend(); ax.grid(True, alpha=0.3)

# ── Plot 3: Kosten vs. Threshold ──
ax = axes[0, 2]
ax.plot(lr_threshold_df["tau"], lr_threshold_df["Cost"], "o-", label="Logistic Regression")
ax.plot(rf_threshold_df["tau"], rf_threshold_df["Cost"], "s-", label="Random Forest")
ax.axvline(lr_best_tau, color="blue",   linestyle="--", alpha=0.5, label=f"LR opt. τ={lr_best_tau:.1f}")
ax.axvline(rf_best_tau, color="orange", linestyle="--", alpha=0.5, label=f"RF opt. τ={rf_best_tau:.1f}")
ax.set_xlabel("Threshold τ"); ax.set_ylabel(f"Gesamtkosten (C_FP={C_FP}·FP + C_FN={C_FN}·FN)")
ax.set_title("Kosten vs. Threshold"); ax.legend(fontsize=8); ax.grid(True, alpha=0.3)

# ── Plot 4: Confusion Matrix LR (opt. τ) ──
ax = axes[1, 0]
ConfusionMatrixDisplay(
    confusion_matrix(y_test, lr_pred_opt),
    display_labels=["Legitim", "Cheater"]
).plot(ax=ax, colorbar=False)
ax.set_title(f"Confusion Matrix – LR (τ={lr_best_tau:.1f})")

# ── Plot 5: Confusion Matrix RF (opt. τ) ──
ax = axes[1, 1]
ConfusionMatrixDisplay(
    confusion_matrix(y_test, rf_pred_opt),
    display_labels=["Legitim", "Cheater"]
).plot(ax=ax, colorbar=False)
ax.set_title(f"Confusion Matrix – RF (τ={rf_best_tau:.1f})")

# ── Plot 6: Feature Importances ──
ax = axes[1, 2]
top10 = importances.head(10)
ax.barh(top10.index[::-1], top10.values[::-1])
ax.set_xlabel("Importance"); ax.set_title("Feature Importances (RF, Top 10)")
ax.grid(True, alpha=0.3, axis="x")

plt.tight_layout()
plot_path = os.path.join(OUTPUT_DIR, "evaluation_plots.png")
plt.savefig(plot_path, dpi=150, bbox_inches="tight")
plt.close()
print(f"→ Gespeichert: {plot_path}")


# ─────────────────────────────────────────────
# 7. Modelle speichern
# ─────────────────────────────────────────────
lr_model_path = os.path.join(OUTPUT_DIR, "model_lr.pkl")
rf_model_path = os.path.join(OUTPUT_DIR, "model_rf.pkl")
joblib.dump(lr_pipeline, lr_model_path)
joblib.dump(rf_pipeline, rf_model_path)

# Feature-Namen mitspeichern. sklearn-Pipelines arbeiten positionsbasiert und
# kennen die Spaltennamen nicht — ohne diese Datei kann predict.py nicht
# prüfen, ob die Daten zum Modell passen, und eine geänderte CSV-Struktur
# würde entweder als kryptischer Dimensionsfehler oder als stille
# Fehlzuordnung der Werte enden.
feature_path = os.path.join(OUTPUT_DIR, "feature_columns.json")
with open(feature_path, "w", encoding="utf-8") as f:
    json.dump(feature_cols, f, indent=2)

print(f"\n[Modelle] Gespeichert:")
print(f"  {lr_model_path}")
print(f"  {rf_model_path}")
print(f"  {feature_path}")


# ─────────────────────────────────────────────
# 8. Zusammenfassung
# ─────────────────────────────────────────────
print("\n" + "=" * 60)
print("  ZUSAMMENFASSUNG")
print("=" * 60)
summary = {
    "Modell":            ["Logistic Regression", "Random Forest"],
    "CV ROC-AUC":        [f"{np.nanmean(cv_scores):.4f} ±{np.nanstd(cv_scores):.4f}",
                          f"{np.nanmean(cv_scores_rf):.4f} ±{np.nanstd(cv_scores_rf):.4f}"],
    "Test ROC-AUC":      [f"{lr_roc:.4f}", f"{rf_roc:.4f}"],
    "Avg. Precision":    [f"{lr_ap:.4f}", f"{rf_ap:.4f}"],
    "Opt. Threshold τ":  [f"{lr_best_tau:.1f}", f"{rf_best_tau:.1f}"],
    "Kosten (τ=0.5)":    [lr_cost, rf_cost],
    "Kosten (opt. τ)":   [lr_cost_opt, rf_cost_opt],
}
df_summary = pd.DataFrame(summary)
print(df_summary.to_string(index=False))

summary_path = os.path.join(OUTPUT_DIR, "summary.csv")
df_summary.to_csv(summary_path, index=False)
print(f"\n→ Summary gespeichert: {summary_path}")


# ─────────────────────────────────────────────
# 9. τ*-Kurve über verschiedene C_FP/C_FN-Verhältnisse
#    → Zentraler Beitrag für Kapitel 9 (Diskussion)
#    Forschungsfrage: Wie verschiebt sich τ* mit dem Kostenverhältnis?
# ─────────────────────────────────────────────
print("\n" + "=" * 60)
print("  τ*-Analyse: Optimaler Threshold vs. C_FP/C_FN-Verhältnis")
print("=" * 60)

# Kostenverhältnisse die getestet werden
# Darstellung als C_FN/C_FP (wie oft teurer ist ein FN vs. FP)
cost_ratios = [1, 2, 5, 10, 20, 50]   # C_FN/C_FP
thresholds  = np.round(np.arange(0.01, 1.0, 0.01), 2)  # fein: 1%-Schritte

ratio_results = []

for ratio in cost_ratios:
    cfp_val = 1
    cfn_val = ratio   # C_FN = ratio * C_FP

    for model_name, y_prob in [("Logistic Regression", lr_prob),
                                ("Random Forest",       rf_prob)]:
        best_tau  = None
        best_cost = np.inf

        for tau in thresholds:
            y_pred_tau = (y_prob >= tau).astype(int)
            cm = confusion_matrix(y_test, y_pred_tau, labels=[0, 1])
            tn, fp, fn, tp = cm.ravel() if cm.size == 4 else (0, 0, 0, 0)
            cost = cfp_val * fp + cfn_val * fn
            if cost < best_cost:
                best_cost = cost
                best_tau  = tau

        ratio_results.append({
            "Modell":        model_name,
            "C_FN/C_FP":     ratio,
            "C_FP":          cfp_val,
            "C_FN":          cfn_val,
            "τ*":            round(best_tau, 2),
            "Min. Kosten":   best_cost
        })

df_ratios = pd.DataFrame(ratio_results)

print("\n  τ* (optimaler Threshold) je nach Kostenverhältnis:\n")
for model_name in ["Logistic Regression", "Random Forest"]:
    sub = df_ratios[df_ratios["Modell"] == model_name][["C_FN/C_FP", "τ*", "Min. Kosten"]]
    print(f"  {model_name}:")
    print(sub.to_string(index=False))
    print()

# CSV speichern
ratio_path = os.path.join(OUTPUT_DIR, "tau_vs_cost_ratio.csv")
df_ratios.to_csv(ratio_path, index=False)
print(f"→ Gespeichert: {ratio_path}")

# Plot: τ* vs C_FN/C_FP für beide Modelle
fig2, ax2 = plt.subplots(figsize=(8, 5))

for model_name, marker in [("Logistic Regression", "o"), ("Random Forest", "s")]:
    sub = df_ratios[df_ratios["Modell"] == model_name]
    ax2.plot(sub["C_FN/C_FP"], sub["τ*"],
             marker=marker, linewidth=2, markersize=8,
             label=model_name)

ax2.set_xlabel("Kostenverhältnis C_FN / C_FP  (wie viel teurer ist ein unbemerkter Cheater?)",
               fontsize=10)
ax2.set_ylabel("Optimaler Schwellenwert τ*", fontsize=10)
ax2.set_title("Verschiebung des optimalen Schwellenwerts τ*\nbei steigender Kostenasymmetrie C_FN > C_FP",
              fontsize=11)
ax2.set_xscale("log")
ax2.set_xticks(cost_ratios)
ax2.set_xticklabels([f"1:{r}" for r in cost_ratios])
ax2.set_ylim(0, 1.05)
ax2.axhline(0.5, color="gray", linestyle="--", alpha=0.4, label="Standard τ=0.5")
ax2.legend()
ax2.grid(True, alpha=0.3)

tau_curve_path = os.path.join(OUTPUT_DIR, "tau_vs_cost_ratio.png")
fig2.tight_layout()
fig2.savefig(tau_curve_path, dpi=150, bbox_inches="tight")
plt.close(fig2)
print(f"→ Plot gespeichert: {tau_curve_path}")

print("\n✓ Schritt 3 abgeschlossen.")
