"""
Schritt 3: ML-Modell Training
Bachelorarbeit – Kostenasymmetrisches ML-Klassifikationsmodell zur Cheat-Erkennung

╔══════════════════════════════════════════════════════════════════════════╗
║  BEZUG ZUR BACHELORARBEIT (dieses Skript deckt mehrere Kapitel ab)         ║
║  ─────────────────────────────────────────────────────────────────        ║
║  • Kap. 3   Verlustfunktionen & Kostenasymmetrie  → Kostenmatrix C_FP,C_FN ║
║  • Kap. 4.1 Lineare Modelle                        → Logistic Regression   ║
║  • Kap. 4.2 Nichtlineare Modelle                   → Random Forest         ║
║  • Kap. 5   Klassenungleichgewicht & Metriken      → Precision/Recall/AUC  ║
║  • Kap. 5.3 Schwellenwert-Optimierung              → τ-Variation           ║
║  • Kap. 6.2 Generalisierung                        → 5-Fold Cross-Val.     ║
║  • Kap. 8   Experimentelle Evaluation              → Trainingssetup        ║
║  • Kap. 9   Diskussion                             → τ*-vs-Kostenverhältnis║
╚══════════════════════════════════════════════════════════════════════════╝

Modelle:    Logistic Regression, Random Forest
Features:   21 von 25 Spalten (AimAngularErrorMean, AimAngularErrorStdDev, HeadshotRate = 0 → ignoriert)
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
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')   # kein Display nötig (Headless) – rendert Plots in Dateien statt Fenster
import matplotlib.pyplot as plt

# scikit-learn Bausteine:
from sklearn.model_selection import train_test_split, StratifiedKFold, cross_val_score
from sklearn.preprocessing import StandardScaler          # Feature-Standardisierung (z-Score)
from sklearn.linear_model import LogisticRegression       # lineares Modell (Kap. 4.1)
from sklearn.ensemble import RandomForestClassifier       # nichtlineares Modell (Kap. 4.2)
from sklearn.metrics import (
    classification_report, confusion_matrix,
    roc_auc_score, roc_curve, precision_recall_curve,
    average_precision_score, ConfusionMatrixDisplay
)
from sklearn.pipeline import Pipeline                     # Scaler+Modell als eine Einheit
import joblib                                             # Modell-Serialisierung (.pkl)

# ─────────────────────────────────────────────
# 0. Konfiguration
# ─────────────────────────────────────────────
SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
DATA_PATH    = os.path.join(SCRIPT_DIR, "training_data.csv")
OUTPUT_DIR   = os.path.join(SCRIPT_DIR, "results")
os.makedirs(OUTPUT_DIR, exist_ok=True)

# ── KERN DER ARBEIT (Kap. 3.2): Kostenasymmetrie C_FN > C_FP ──
# Ein übersehener Cheater (FN) ist 10× teurer als ein fälschlich gebannter
# Spieler (FP). Dieses 1:10-Verhältnis ist die zentrale Annahme, aus der sich
# später die Verschiebung des optimalen Schwellenwerts τ* < 0.5 ergibt.
C_FP = 1    # Kosten: legitimer Spieler wird gebannt   (False Positive)
C_FN = 10   # Kosten: Cheater wird nicht erkannt        (False Negative)

RANDOM_STATE = 42      # Reproduzierbarkeit (gleiche Splits/Modelle bei jedem Lauf)
TEST_SIZE    = 0.2     # 80/20 Split → Trainings-/Testfehler trennen (Kap. 6.2)

# Features die dauerhaft 0 sind → aus Training ausschließen.
# Im echten UE5-Logger werden diese 3 Größen (noch) nicht erfasst; ein
# konstantes Feature trägt keine Information und würde nur Rauschen liefern.
ZERO_FEATURES = ["AimAngularErrorMean", "AimAngularErrorStdDev", "HeadshotRate"]
# Nicht-Feature-Spalten: PlayerID ist nur ID, Label ist die Zielgröße y.
META_COLS = ["PlayerID", "Label"]


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

# Feature-Spalten = alle Spalten außer Meta- und Null-Features.
feature_cols = [c for c in df.columns
                if c not in META_COLS and c not in ZERO_FEATURES]
print(f"\n[Features] {len(feature_cols)} Features genutzt:")
print("  " + ", ".join(feature_cols))
print(f"\n[Ignoriert] {ZERO_FEATURES}  (dauerhaft 0 in echten UE5-Daten)")

# X = Feature-Matrix (eine Zeile pro Session = ein Vektor x ∈ R^n, Kap. 2.1)
# y = Label-Vektor   (y ∈ {0,1}, Kap. 2.2)
X = df[feature_cols].values
y = df["Label"].values

# Train/Test Split (stratifiziert → Klassenverteilung bleibt gleich).
# stratify=y ist bei Imbalance PFLICHT: ohne stratify könnten im Testset
# zufällig kaum/keine Cheater landen und die Metriken wären unbrauchbar (Kap. 5.1).
X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=TEST_SIZE, random_state=RANDOM_STATE, stratify=y
)
print(f"\n[Split] Train: {len(X_train)} | Test: {len(X_test)}")


# ─────────────────────────────────────────────
# 2. Hilfsfunktionen
# ─────────────────────────────────────────────

def compute_total_cost(y_true, y_pred, c_fp=C_FP, c_fn=C_FN):
    """
    Gesamtkosten nach Kostenmatrix:  Kosten = C_FP · FP + C_FN · FN   (Kap. 3.2)

    Diese Größe – nicht die Accuracy – ist die eigentliche Zielfunktion:
    Wir wollen sie minimieren. Weil C_FN >> C_FP, "bestraft" sie übersehene
    Cheater viel härter als Fehlalarme.
    """
    cm = confusion_matrix(y_true, y_pred)
    # Confusion-Matrix-Layout (sklearn, Labels [0,1]):
    #            pred 0   pred 1
    #   true 0 [   TN       FP  ]   cm[0,1] = FP (legit → fälschlich Cheater)
    #   true 1 [   FN       TP  ]   cm[1,0] = FN (Cheater → fälschlich legit)
    fp = cm[0, 1] if cm.shape == (2, 2) else 0
    fn = cm[1, 0] if cm.shape == (2, 2) else 0
    return c_fp * fp + c_fn * fn


def evaluate_thresholds(model_name, y_true, y_prob, thresholds=None):
    """
    Schwellenwert-Analyse (Kap. 5.3).

    Ein Klassifikator liefert zunächst eine Wahrscheinlichkeit s(x)=P(y=1|x).
    Die Entscheidung lautet:  f_τ(x) = 1  falls s(x) ≥ τ, sonst 0.
    Der Standard τ=0.5 ist NICHT optimal, wenn Fehler ungleich teuer sind.

    Diese Funktion probiert τ ∈ {0.1,...,0.9} durch, berechnet je τ
    Precision/Recall/F1 und die Gesamtkosten, und liefert das τ mit
    MINIMALEN Kosten zurück. Das ist die empirische Version der Aussage
    "τ* hängt vom Kostenverhältnis ab" (Kap. 3.2 / Kap. 9).
    """
    if thresholds is None:
        thresholds = np.arange(0.1, 1.0, 0.1)

    results = []
    for tau in thresholds:
        # Schwellenwert anwenden: Wahrscheinlichkeit → harte 0/1-Entscheidung.
        y_pred_tau = (y_prob >= tau).astype(int)
        cm = confusion_matrix(y_true, y_pred_tau, labels=[0, 1])
        # ravel() entpackt die 2x2-Matrix in der Reihenfolge tn, fp, fn, tp.
        tn, fp, fn, tp = cm.ravel() if cm.size == 4 else (0, 0, 0, 0)

        # Precision = von allen als Cheater gemeldeten, wie viele sind es wirklich?
        #            → hoch = wenige Fehlalarme (wichtig, weil FP echte Spieler trifft)
        precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
        # Recall   = von allen echten Cheatern, wie viele wurden gefunden?
        #            → hoch = wenige übersehene Cheater (drückt FN, also C_FN, runter)
        recall    = tp / (tp + fn) if (tp + fn) > 0 else 0.0
        # F1 = harmonisches Mittel von Precision & Recall (ein Kompromisswert).
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
    # idxmin() auf der Kostenspalte → Zeile mit den geringsten Gesamtkosten = τ*.
    best_row = df_res.loc[df_res["Cost"].idxmin()]
    print(f"\n[{model_name}] Threshold-Analyse (C_FP={C_FP}, C_FN={C_FN}):")
    print(df_res[["tau", "TP", "FP", "FN", "TN", "Precision", "Recall", "F1", "Cost"]].to_string(index=False))
    print(f"\n  → Optimaler Threshold: τ = {best_row['tau']:.1f}  (Gesamtkosten = {best_row['Cost']:.0f})")

    # CSV speichern (Rohdaten für Tabellen/Abbildungen in Kap. 8/9).
    out_path = os.path.join(OUTPUT_DIR, f"threshold_analysis_{model_name.lower().replace(' ','_')}.csv")
    df_res.to_csv(out_path, index=False)
    print(f"  → Gespeichert: {out_path}")

    return df_res, float(best_row["tau"])


def print_evaluation(model_name, y_true, y_pred, y_prob):
    """
    Vollständige Modell-Evaluation inkl. Kosten (Kap. 5.2).

    Wichtig: Bei 95/5-Imbalance ist Accuracy irreführend — ein Modell, das
    IMMER "legitim" sagt, hätte 95 % Accuracy, fände aber 0 Cheater (Kap. 5.1).
    Deshalb stützen wir uns auf Precision/Recall, ROC-AUC und vor allem Kosten.
    """
    print(f"\n{'─'*50}")
    print(f"  Modell: {model_name}")
    print(f"{'─'*50}")
    # classification_report gibt Precision/Recall/F1 je Klasse aus.
    print(classification_report(y_true, y_pred,
                                 target_names=["Legitim (0)", "Cheater (1)"],
                                 digits=4))
    cm = confusion_matrix(y_true, y_pred)
    print(f"Confusion Matrix:\n{cm}")
    # ROC-AUC: schwellenunabhängiges Maß der Trennfähigkeit (1.0 = perfekt, 0.5 = Zufall).
    roc = roc_auc_score(y_true, y_prob)
    # Average Precision = Fläche unter Precision-Recall-Kurve, bei Imbalance
    # aussagekräftiger als ROC-AUC (Kap. 5.2).
    ap  = average_precision_score(y_true, y_prob)
    cost = compute_total_cost(y_true, y_pred)
    print(f"ROC-AUC:            {roc:.4f}")
    print(f"Avg. Precision:     {ap:.4f}")
    print(f"Gesamtkosten (τ=0.5): {cost}  (C_FP={C_FP}·FP={cm[0,1]} + C_FN={C_FN}·FN={cm[1,0]})")
    return roc, ap, cost


# ─────────────────────────────────────────────
# 3. Logistic Regression  (Kap. 4.1 – lineares Modell)
#    Modell:  P(y=1|x) = σ(wᵀx + b),  Entscheidungsgrenze wᵀx + b = 0.
#    Interpretierbar (Koeffizienten = Feature-Gewichte), schnell, robust.
# ─────────────────────────────────────────────
print("\n" + "=" * 60)
print("  Modell 1: Logistic Regression")
print("=" * 60)

# Pipeline = Scaler + Klassifikator als EINE Einheit. Vorteil: der Scaler wird
# bei Cross-Validation NUR auf den Trainingsfolds gefittet → kein "Data Leakage".
lr_pipeline = Pipeline([
    # StandardScaler: jedes Feature auf Mittelwert 0, Streuung 1. Für LogReg
    # nötig, weil Features sehr unterschiedliche Skalen haben (z. B. Speed ~1300
    # vs. ReactionTimeStdDev ~0.005) — sonst dominieren großskalige Features.
    ("scaler", StandardScaler()),
    ("clf",    LogisticRegression(
        class_weight="balanced",  # kompensiert 95/5 Imbalance: Cheater-Fehler
                                   # werden im Training stärker gewichtet (Kap. 5.1)
        max_iter=1000,
        random_state=RANDOM_STATE
    ))
])

# Cross-Validation (5-fold, stratifiziert) – Kap. 6.2.
# Schätzt die Generalisierung: Trainingsdaten werden in 5 Teile geteilt, jeweils
# 4 zum Trainieren, 1 zum Testen, 5× rotiert. Mittelwert ± Std zeigt Stabilität.
cv = StratifiedKFold(n_splits=5, shuffle=True, random_state=RANDOM_STATE)
cv_scores = cross_val_score(lr_pipeline, X_train, y_train, cv=cv, scoring="roc_auc")
print(f"\n[LR] 5-Fold CV ROC-AUC: {cv_scores.mean():.4f} ± {cv_scores.std():.4f}")
print(f"     Scores: {np.round(cv_scores, 4)}")

# Auf gesamten Trainingsdaten fitten, dann auf dem (ungesehenen) Testset bewerten.
lr_pipeline.fit(X_train, y_train)
lr_prob  = lr_pipeline.predict_proba(X_test)[:, 1]   # P(y=1|x) je Testbeispiel
lr_pred  = lr_pipeline.predict(X_test)               # Default-Entscheidung (τ=0.5)

lr_roc, lr_ap, lr_cost = print_evaluation("Logistic Regression", y_test, lr_pred, lr_prob)
lr_threshold_df, lr_best_tau = evaluate_thresholds("Logistic Regression", y_test, lr_prob)

# Optimalen Threshold anwenden und Kosten erneut messen → zeigt die Ersparnis
# gegenüber dem naiven τ=0.5 (Kern der Forschungsfrage).
lr_pred_opt = (lr_prob >= lr_best_tau).astype(int)
lr_cost_opt = compute_total_cost(y_test, lr_pred_opt)
print(f"\n[LR] Mit τ={lr_best_tau:.1f}: Gesamtkosten = {lr_cost_opt}")


# ─────────────────────────────────────────────
# 4. Random Forest  (Kap. 4.2 – nichtlineares Modell)
#    Ensemble vieler Entscheidungsbäume. Kann nichtlineare Muster und
#    Feature-Interaktionen erfassen (z. B. "niedrige StdDev UND hohe HitRate"),
#    die ein lineares Modell nicht trennen kann.
# ─────────────────────────────────────────────
print("\n" + "=" * 60)
print("  Modell 2: Random Forest")
print("=" * 60)

rf_pipeline = Pipeline([
    # Scaler bei Bäumen eigentlich unnötig (Bäume sind skalierungsinvariant),
    # aber für eine einheitliche Pipeline-Struktur beibehalten.
    ("scaler", StandardScaler()),
    ("clf",    RandomForestClassifier(
        n_estimators=200,          # Anzahl Bäume (mehr = stabiler, langsamer)
        class_weight="balanced",   # wieder gegen Imbalance (Kap. 5.1)
        max_depth=None,            # Bäume voll wachsen lassen
        min_samples_leaf=2,        # leichte Regularisierung gegen Overfitting (Kap. 6)
        random_state=RANDOM_STATE,
        n_jobs=-1                  # alle CPU-Kerne nutzen
    ))
])

cv_scores_rf = cross_val_score(rf_pipeline, X_train, y_train, cv=cv, scoring="roc_auc")
print(f"\n[RF] 5-Fold CV ROC-AUC: {cv_scores_rf.mean():.4f} ± {cv_scores_rf.std():.4f}")
print(f"     Scores: {np.round(cv_scores_rf, 4)}")

rf_pipeline.fit(X_train, y_train)
rf_prob  = rf_pipeline.predict_proba(X_test)[:, 1]
rf_pred  = rf_pipeline.predict(X_test)

rf_roc, rf_ap, rf_cost = print_evaluation("Random Forest", y_test, rf_pred, rf_prob)
rf_threshold_df, rf_best_tau = evaluate_thresholds("Random Forest", y_test, rf_prob)

rf_pred_opt = (rf_prob >= rf_best_tau).astype(int)
rf_cost_opt = compute_total_cost(y_test, rf_pred_opt)
print(f"\n[RF] Mit τ={rf_best_tau:.1f}: Gesamtkosten = {rf_cost_opt}")


# ─────────────────────────────────────────────
# 5. Feature Importance (Random Forest)
#    Welche Features tragen am meisten zur Trennung bei? Erwartung gemäß
#    Datengenerierung: ReactionTimeStdDev / ShotIntervalStdDev (Bot-Merkmale)
#    und SpeedViolationRatio (Speedhack) sollten oben stehen.
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
# 6. Plots  (Abbildungen für Kap. 8/9)
#    2×3-Raster: ROC, Precision-Recall, Kosten-vs-τ, 2× Confusion-Matrix,
#    Feature-Importances.
# ─────────────────────────────────────────────
print("\n[Plots] Erstelle Grafiken...")

fig, axes = plt.subplots(2, 3, figsize=(18, 11))
fig.suptitle("Schritt 3 – Cheat-Erkennung ML-Evaluation\n(C_FP=1, C_FN=10)", fontsize=14)

# ── Plot 1: ROC-Kurven ── (Kap. 5.2)
# Zeigt True-Positive-Rate gegen False-Positive-Rate über alle Schwellen.
ax = axes[0, 0]
for name, prob in [("Logistic Regression", lr_prob), ("Random Forest", rf_prob)]:
    fpr, tpr, _ = roc_curve(y_test, prob)
    auc = roc_auc_score(y_test, prob)
    ax.plot(fpr, tpr, label=f"{name} (AUC={auc:.3f})")
ax.plot([0,1],[0,1],"--", color="gray", label="Zufall")   # Diagonale = Ratemodell
ax.set_xlabel("False Positive Rate"); ax.set_ylabel("True Positive Rate")
ax.set_title("ROC-Kurven"); ax.legend(); ax.grid(True, alpha=0.3)

# ── Plot 2: Precision-Recall ── (bei Imbalance aussagekräftiger als ROC, Kap. 5.2)
ax = axes[0, 1]
for name, prob in [("Logistic Regression", lr_prob), ("Random Forest", rf_prob)]:
    prec, rec, _ = precision_recall_curve(y_test, prob)
    ap = average_precision_score(y_test, prob)
    ax.plot(rec, prec, label=f"{name} (AP={ap:.3f})")
ax.set_xlabel("Recall"); ax.set_ylabel("Precision")
ax.set_title("Precision-Recall-Kurven"); ax.legend(); ax.grid(True, alpha=0.3)

# ── Plot 3: Kosten vs. Threshold ── (Kap. 5.3 / Kap. 9: das τ* mit min. Kosten)
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
# 7. Modelle speichern  (für späteren Einsatz / Schritt 5 Future Work)
# ─────────────────────────────────────────────
lr_model_path = os.path.join(OUTPUT_DIR, "model_lr.pkl")
rf_model_path = os.path.join(OUTPUT_DIR, "model_rf.pkl")
# joblib.dump serialisiert die GESAMTE Pipeline (inkl. gefittetem Scaler),
# sodass neue Daten exakt gleich vorverarbeitet werden.
joblib.dump(lr_pipeline, lr_model_path)
joblib.dump(rf_pipeline, rf_model_path)
print(f"\n[Modelle] Gespeichert:")
print(f"  {lr_model_path}")
print(f"  {rf_model_path}")


# ─────────────────────────────────────────────
# 8. Zusammenfassung  (Tabelle für Kap. 8.3 Ergebnisse)
# ─────────────────────────────────────────────
print("\n" + "=" * 60)
print("  ZUSAMMENFASSUNG")
print("=" * 60)
summary = {
    "Modell":            ["Logistic Regression", "Random Forest"],
    "CV ROC-AUC":        [f"{cv_scores.mean():.4f} ±{cv_scores.std():.4f}",
                          f"{cv_scores_rf.mean():.4f} ±{cv_scores_rf.std():.4f}"],
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
#
#    Idee: Statt nur das fixe 1:10 zu betrachten, variieren wir das Verhältnis
#    C_FN/C_FP systematisch und suchen jeweils das kostenminimale τ*. So wird
#    die theoretische Aussage aus Kap. 3.2 ("τ* sinkt, wenn FN teurer wird")
#    empirisch bestätigt: bei großem C_FN bannt man lieber im Zweifel (kleiner τ).
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

    # Für beide Modelle: feines Raster aller τ durchsuchen und das mit den
    # geringsten Kosten merken (argmin über τ).
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

# Plot: τ* vs C_FN/C_FP für beide Modelle (die zentrale Abbildung zur Forschungsfrage).
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
ax2.set_xscale("log")   # log-Skala, weil die Verhältnisse 1..50 stark spreizen
ax2.set_xticks(cost_ratios)
ax2.set_xticklabels([f"1:{r}" for r in cost_ratios])
ax2.set_ylim(0, 1.05)
ax2.axhline(0.5, color="gray", linestyle="--", alpha=0.4, label="Standard τ=0.5")  # Referenz
ax2.legend()
ax2.grid(True, alpha=0.3)

tau_curve_path = os.path.join(OUTPUT_DIR, "tau_vs_cost_ratio.png")
fig2.tight_layout()
fig2.savefig(tau_curve_path, dpi=150, bbox_inches="tight")
plt.close(fig2)
print(f"→ Plot gespeichert: {tau_curve_path}")

print("\n✓ Schritt 3 abgeschlossen.")
