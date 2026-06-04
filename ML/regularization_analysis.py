"""
regularization_analysis.py
==========================
Schritt 4 der Bachelorarbeit: L2-Regularisierung und λ-Hyperparameter-Tuning

╔══════════════════════════════════════════════════════════════════════════╗
║  BEZUG ZUR BACHELORARBEIT                                                  ║
║  ────────────────────────                                                  ║
║  • Kap. 6.1 (Regularisierung):  J(w) = Loss(w) + λ·||w||²                   ║
║      L2 bestraft große Gewichte → das Modell wird "glatter", einzelne      ║
║      Features dominieren weniger → bessere Generalisierung.                ║
║  • Kap. 6.2 (Generalisierung):  Trainings- vs. Testfehler, Cross-Val.,     ║
║      Bias–Variance-Tradeoff.                                               ║
║  • Kap. 9   (Diskussion):       empirischer Beleg, wie λ Kosten/AUC und    ║
║      die FP/FN-Balance beeinflusst.                                        ║
╚══════════════════════════════════════════════════════════════════════════╝

Bachelorarbeit Kapitel 6 behandelt Regularisierung theoretisch:
    J(w) = Loss(w) + λ·||w||²
    → λ klein: wenig Regularisierung, Overfitting-Risiko
    → λ groß: starke Regularisierung, Underfitting-Risiko

Dieses Skript liefert den empirischen Bezug dazu (Kap. 9 Diskussion):
    1. Regularisierungspfad: λ-Variation für Logistic Regression (C = 1/λ)
       → ROC-AUC, Gesamtkosten, Anzahl aktiver Features vs. λ
    2. GridSearchCV: optimales λ per 5-Fold Cross-Validation
    3. Vergleich: nicht-regularisiert vs. optimal regularisiert (Kosten, AUC)
    4. Gewichtspfad: wie schrumpfen Feature-Koeffizienten mit steigendem λ

Hinweis zu scikit-learn Notation (WICHTIG fürs Verständnis!):
    LR in sklearn nutzt C = 1/λ (inverse Regularisierungsstärke)
    → C groß = schwache Regularisierung (λ klein)
    → C klein = starke Regularisierung (λ groß)
    Merksatz: In der Arbeit steht λ; im Code steht C = 1/λ. Wir rechnen
    deshalb unten ständig zwischen beiden um.
"""

import os
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

from sklearn.model_selection import train_test_split, StratifiedKFold, GridSearchCV
from sklearn.preprocessing import StandardScaler
from sklearn.linear_model import LogisticRegression
from sklearn.pipeline import Pipeline
from sklearn.metrics import (
    roc_auc_score, confusion_matrix, average_precision_score,
    classification_report
)
import joblib

# ─────────────────────────────────────────────
# 0. Konfiguration
# ─────────────────────────────────────────────
SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
DATA_PATH    = os.path.join(SCRIPT_DIR, "training_data.csv")
OUTPUT_DIR   = os.path.join(SCRIPT_DIR, "results")
os.makedirs(OUTPUT_DIR, exist_ok=True)

# Gleiche Kostenmatrix wie in train_model.py → Ergebnisse sind vergleichbar (Kap. 3.2).
C_FP         = 1
C_FN         = 10
RANDOM_STATE = 42
TEST_SIZE    = 0.2

ZERO_FEATURES = ["AimAngularErrorMean", "AimAngularErrorStdDev", "HeadshotRate"]
META_COLS     = ["PlayerID", "Label"]

# λ-Werte die getestet werden (als C = 1/λ in sklearn).
# Bereich: sehr schwache (C=1000, λ≈0) bis sehr starke (C=0.001, λ=1000)
# Regularisierung. Logarithmisch gestaffelt, weil λ über mehrere
# Größenordnungen variiert.
C_VALUES = [1000, 100, 10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.01, 0.001]
# Als λ = 1/C zum Anzeigen (das, was in der Arbeit als λ auftaucht).
LAMBDA_VALUES = [round(1/c, 4) for c in C_VALUES]

print("=" * 60)
print("  Schritt 4 – L2-Regularisierung & λ-Tuning")
print("=" * 60)

# ─────────────────────────────────────────────
# 1. Daten laden
# ─────────────────────────────────────────────
df = pd.read_csv(DATA_PATH)
feature_cols = [c for c in df.columns if c not in META_COLS and c not in ZERO_FEATURES]
X = df[feature_cols].values
y = df["Label"].values

X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=TEST_SIZE, random_state=RANDOM_STATE, stratify=y
)

# Scaler einmal fitten — für den Gewichtspfad brauchen wir konsistente Skalierung.
# WICHTIG: fit NUR auf Trainingsdaten, transform auf Test (kein Data Leakage).
# Standardisierung ist Voraussetzung dafür, dass ||w||² fair ist: nur bei
# gleich skalierten Features sind die Koeffizienten w_i untereinander vergleichbar
# und L2 bestraft alle gleichmäßig.
scaler = StandardScaler()
X_train_scaled = scaler.fit_transform(X_train)
X_test_scaled  = scaler.transform(X_test)

print(f"\n[Daten] {len(df)} Sessions, {len(feature_cols)} Features")
print(f"[Split] Train: {len(X_train)} | Test: {len(X_test)}")

# ─────────────────────────────────────────────
# 2. Regularisierungspfad: λ vs. AUC / Kosten
#    Wir trainieren für jedes λ ein eigenes LogReg-Modell und protokollieren,
#    wie sich AUC, Kosten, FP/FN und die Gewichte verändern. Das ist die
#    empirische Visualisierung des Bias–Variance-Tradeoffs (Kap. 6.2).
# ─────────────────────────────────────────────
print("\n[Regularisierungspfad] Berechne für alle λ-Werte...")

path_results = []
weight_matrix = []   # für Gewichtspfad-Plot: speichert je λ den Koeffizientenvektor w

for C_val, lam in zip(C_VALUES, LAMBDA_VALUES):
    model = LogisticRegression(
        C=C_val,                   # = 1/λ : kleines C ⇒ starke Regularisierung
        penalty="l2",              # L2-Strafterm λ·||w||²  (Kap. 6.1)
        class_weight="balanced",   # gegen 95/5-Imbalance (Kap. 5.1)
        max_iter=2000,
        random_state=RANDOM_STATE,
        solver="lbfgs"
    )
    model.fit(X_train_scaled, y_train)

    prob  = model.predict_proba(X_test_scaled)[:, 1]
    pred  = model.predict(X_test_scaled)
    auc   = roc_auc_score(y_test, prob)
    ap    = average_precision_score(y_test, prob)
    cm    = confusion_matrix(y_test, pred)
    fp    = cm[0, 1] if cm.size == 4 else 0
    fn    = cm[1, 0] if cm.size == 4 else 0
    cost  = C_FP * fp + C_FN * fn

    # Anzahl "aktiver" Features (|w| > 0.01 nach L2 — nie exakt 0, aber klein).
    # Hinweis: L2 schrumpft Gewichte gegen 0, setzt sie aber NICHT exakt auf 0
    # (das täte L1). Daher zählen wir "fast 0" über die Schwelle 0.01.
    n_active = (np.abs(model.coef_[0]) > 0.01).sum()

    path_results.append({
        "lambda":    lam,
        "C":         C_val,
        "ROC-AUC":   round(auc, 4),
        "Avg.Prec":  round(ap,  4),
        "FP":        fp,
        "FN":        fn,
        "Kosten":    cost,
        "Aktive Features": n_active
    })
    weight_matrix.append(model.coef_[0])

df_path = pd.DataFrame(path_results)
print("\n  λ       C       AUC    AvgPrec  FP   FN  Kosten  AktiveF")
for _, row in df_path.iterrows():
    print(f"  {row['lambda']:<7} {row['C']:<7} {row['ROC-AUC']:.4f} "
          f"{row['Avg.Prec']:.4f}  {int(row['FP']):3d}  {int(row['FN']):3d}  "
          f"{int(row['Kosten']):5d}   {int(row['Aktive Features'])}")

path_csv = os.path.join(OUTPUT_DIR, "regularization_path.csv")
df_path.to_csv(path_csv, index=False)
print(f"\n→ Gespeichert: {path_csv}")

# ─────────────────────────────────────────────
# 3. GridSearchCV: optimales C (= 1/λ)
#    Statt das beste λ "per Auge" am Testset abzulesen (wäre Data Leakage!),
#    wählt GridSearchCV es sauber per 5-Fold Cross-Validation NUR auf den
#    Trainingsdaten. Das Testset bleibt für die finale, unabhängige Bewertung
#    reserviert (Kap. 6.2 – korrekte Evaluationsmethodik).
# ─────────────────────────────────────────────
print("\n[GridSearchCV] Suche optimales λ via 5-Fold CV (Scoring: ROC-AUC)...")

cv = StratifiedKFold(n_splits=5, shuffle=True, random_state=RANDOM_STATE)

param_grid = {"clf__C": C_VALUES}   # alle Kandidaten-Cs durchprobieren
grid_pipeline = Pipeline([
    ("scaler", StandardScaler()),   # in der Pipeline → pro Fold sauber neu gefittet
    ("clf", LogisticRegression(
        penalty="l2",
        class_weight="balanced",
        max_iter=2000,
        random_state=RANDOM_STATE,
        solver="lbfgs"
    ))
])

grid_search = GridSearchCV(
    grid_pipeline,
    param_grid,
    cv=cv,
    scoring="roc_auc",
    n_jobs=-1,
    return_train_score=True   # auch Train-Scores → Over/Underfitting sichtbar machen
)
grid_search.fit(X_train, y_train)

best_C      = grid_search.best_params_["clf__C"]
best_lambda = round(1 / best_C, 4)
best_score  = round(grid_search.best_score_, 4)

print(f"\n  Bestes C    = {best_C}  (λ = {best_lambda})")
print(f"  CV ROC-AUC  = {best_score}")

# CV-Ergebnisse speichern (Train- vs. CV-AUC pro λ → Beleg für Kap. 6.2).
cv_results = pd.DataFrame(grid_search.cv_results_)
cv_results["lambda"] = [round(1/c, 4) for c in cv_results["param_clf__C"]]
cv_out = cv_results[["lambda", "param_clf__C", "mean_test_score",
                      "std_test_score", "mean_train_score"]].copy()
cv_out.columns = ["lambda", "C", "CV_AUC_mean", "CV_AUC_std", "Train_AUC_mean"]
cv_out = cv_out.sort_values("lambda")
cv_csv = os.path.join(OUTPUT_DIR, "gridsearch_cv_results.csv")
cv_out.to_csv(cv_csv, index=False)
print(f"→ CV-Ergebnisse gespeichert: {cv_csv}")

# ─────────────────────────────────────────────
# 4. Finales Modell mit optimalem λ — Evaluation auf dem Testset
# ─────────────────────────────────────────────
print(f"\n[Finales Modell] Trainiere mit C={best_C} (λ={best_lambda})...")

# best_estimator_ ist bereits auf den GESAMTEN Trainingsdaten mit dem besten C
# nachtrainiert → jetzt einmalig auf dem unabhängigen Testset bewerten.
best_model = grid_search.best_estimator_
best_prob  = best_model.predict_proba(X_test)[:, 1]
best_pred  = best_model.predict(X_test)
best_auc   = roc_auc_score(y_test, best_prob)
best_ap    = average_precision_score(y_test, best_prob)
best_cm    = confusion_matrix(y_test, best_pred)
best_fp    = best_cm[0, 1]
best_fn    = best_cm[1, 0]
best_cost  = C_FP * best_fp + C_FN * best_fn

print(f"\n  ROC-AUC:         {best_auc:.4f}")
print(f"  Avg. Precision:  {best_ap:.4f}")
print(f"  Confusion Matrix:\n{best_cm}")
print(f"  Gesamtkosten:    {best_cost}  (C_FP·{best_fp} + C_FN·{best_fn})")
print(f"\n{classification_report(y_test, best_pred, target_names=['Legitim','Cheater'], digits=4)}")

# Vergleich: praktisch keine Regularisierung (C=1000, λ≈0) vs. optimales λ.
# Zeigt direkt den Nutzen der Regularisierung in Kosten und AUC.
no_reg_row  = df_path[df_path["C"] == 1000].iloc[0]
opt_row     = df_path[df_path["C"] == best_C].iloc[0]
print("\n[Vergleich] Keine Regularisierung vs. Optimal:")
print(f"  C=1000  (λ≈0):   AUC={no_reg_row['ROC-AUC']}, Kosten={int(no_reg_row['Kosten'])}")
print(f"  C={best_C:<5} (λ={best_lambda}): AUC={opt_row['ROC-AUC']}, Kosten={int(opt_row['Kosten'])}")

# Modell speichern
model_path = os.path.join(OUTPUT_DIR, "model_lr_regularized.pkl")
joblib.dump(best_model, model_path)
print(f"\n→ Modell gespeichert: {model_path}")

# ─────────────────────────────────────────────
# 5. Plots  (4 Abbildungen für Kap. 6 & 9)
# ─────────────────────────────────────────────
print("\n[Plots] Erstelle Grafiken...")

weight_matrix = np.array(weight_matrix)   # shape: (n_lambdas, n_features)
lambda_arr    = np.array(LAMBDA_VALUES)

fig, axes = plt.subplots(2, 2, figsize=(14, 10))
fig.suptitle("Schritt 4 – L2-Regularisierung & λ-Tuning\n"
             "(Bachelorarbeit Kap. 6 & 9)", fontsize=13)

# ── Plot 1: λ vs. ROC-AUC (Test + CV) ──
# Schere zwischen Train-AUC (hoch) und CV-AUC (niedriger) bei kleinem λ =
# Overfitting. Bei zu großem λ fallen beide = Underfitting. Optimum dazwischen.
ax = axes[0, 0]
ax.semilogx(lambda_arr, df_path["ROC-AUC"], "o-", color="steelblue",
            linewidth=2, label="Test AUC")
cv_mean = cv_out.sort_values("lambda")["CV_AUC_mean"].values
cv_std  = cv_out.sort_values("lambda")["CV_AUC_std"].values
ax.semilogx(lambda_arr, cv_mean, "s--", color="darkorange",
            linewidth=2, label="CV AUC (mean)")
ax.fill_between(lambda_arr, cv_mean - cv_std, cv_mean + cv_std,
                alpha=0.2, color="darkorange", label="CV ±1 std")
ax.axvline(best_lambda, color="red", linestyle=":", linewidth=1.5,
           label=f"Opt. λ={best_lambda}")
ax.set_xlabel("Regularisierungsstärke λ  (log-Skala)")
ax.set_ylabel("ROC-AUC")
ax.set_title("λ vs. ROC-AUC\n(Train / Cross-Validation)")
ax.legend(fontsize=8); ax.grid(True, alpha=0.3)

# ── Plot 2: λ vs. Gesamtkosten ── (verbindet Regularisierung mit Kap. 3 Kosten)
ax = axes[0, 1]
ax.semilogx(lambda_arr, df_path["Kosten"], "o-", color="tomato",
            linewidth=2, markersize=7)
ax.axvline(best_lambda, color="red", linestyle=":", linewidth=1.5,
           label=f"Opt. λ={best_lambda}")
for lam, cost in zip(lambda_arr, df_path["Kosten"]):
    ax.annotate(f"{int(cost)}", (lam, cost),
                textcoords="offset points", xytext=(4, 4), fontsize=7)
ax.set_xlabel("Regularisierungsstärke λ  (log-Skala)")
ax.set_ylabel(f"Gesamtkosten  (C_FP={C_FP}·FP + C_FN={C_FN}·FN)")
ax.set_title("λ vs. Gesamtkosten")
ax.legend(fontsize=8); ax.grid(True, alpha=0.3)

# ── Plot 3: Gewichtspfad (Top-8 Features nach max |w|) ──
# Die direkte Visualisierung von Kap. 6.1: mit steigendem λ wandern alle
# Koeffizienten w_i in Richtung 0 ("Shrinkage"). Features mit großem Einfluss
# bei kleinem λ schrumpfen am deutlichsten.
ax = axes[1, 0]
max_weights = np.max(np.abs(weight_matrix), axis=0)
top8_idx    = np.argsort(max_weights)[-8:]
for i in top8_idx:
    ax.semilogx(lambda_arr, weight_matrix[:, i],
                label=feature_cols[i], linewidth=1.5)
ax.axvline(best_lambda, color="red", linestyle=":", linewidth=1.5,
           label=f"Opt. λ={best_lambda}")
ax.axhline(0, color="black", linewidth=0.8, alpha=0.5)
ax.set_xlabel("Regularisierungsstärke λ  (log-Skala)")
ax.set_ylabel("Koeffizient w_i")
ax.set_title("Gewichtspfad – Top 8 Features\n(L2 schrumpft Koeffizienten gegen 0)")
ax.legend(fontsize=7, ncol=2); ax.grid(True, alpha=0.3)

# ── Plot 4: FP / FN vs. λ ── (Brücke zur Kostenasymmetrie, Kap. 3.2/9)
# Zu wenig Regularisierung → mehr FN (Cheater übersehen, teuer wegen C_FN).
# Zu viel Regularisierung → Modell verliert Trennschärfe → mehr FP.
ax = axes[1, 1]
ax.semilogx(lambda_arr, df_path["FP"], "o-", color="steelblue",
            linewidth=2, label="False Positives (legitim gebannt)")
ax.semilogx(lambda_arr, df_path["FN"], "s-", color="tomato",
            linewidth=2, label="False Negatives (Cheater übersehen)")
ax.axvline(best_lambda, color="red", linestyle=":", linewidth=1.5,
           label=f"Opt. λ={best_lambda}")
ax.set_xlabel("Regularisierungsstärke λ  (log-Skala)")
ax.set_ylabel("Anzahl Fehler")
ax.set_title("FP / FN vs. λ\n(Tradeoff bei Kostenasymmetrie C_FN > C_FP)")
ax.legend(fontsize=8); ax.grid(True, alpha=0.3)

plt.tight_layout()
plot_path = os.path.join(OUTPUT_DIR, "regularization_plots.png")
fig.savefig(plot_path, dpi=150, bbox_inches="tight")
plt.close(fig)
print(f"→ Gespeichert: {plot_path}")

# ─────────────────────────────────────────────
# 6. Zusammenfassung  (Interpretationstext für Kap. 6/9)
# ─────────────────────────────────────────────
print("\n" + "=" * 60)
print("  ZUSAMMENFASSUNG – Schritt 4")
print("=" * 60)
print(f"  Getestete λ-Werte:       {LAMBDA_VALUES}")
print(f"  Optimales λ (GridSearch): {best_lambda}  (C={best_C})")
print(f"  CV ROC-AUC (opt. λ):     {best_score:.4f}")
print(f"  Test ROC-AUC (opt. λ):   {best_auc:.4f}")
print(f"  Gesamtkosten (opt. λ):   {best_cost}")
print(f"\n  Interpretation:")
print(f"  → Zu wenig Regularisierung (λ→0): Modell passt sich zu stark")
print(f"    an Trainingsdaten an, generalisiert schlechter auf neue Spieler.")
print(f"  → Zu viel Regularisierung (λ→∞): Koeffizienten schrumpfen gegen 0,")
print(f"    Modell verliert Diskriminierungsfähigkeit.")
print(f"  → Optimales λ={best_lambda} balanciert Bias-Variance-Tradeoff")
print(f"    und minimiert Gesamtkosten auf dem Test-Set.")
print(f"\n✓ Schritt 4 abgeschlossen.")
