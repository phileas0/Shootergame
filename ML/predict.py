"""
Cheat Detection – Prediction Script
Bachelorarbeit – ML-basierte Cheat-Erkennung in UE5

Verwendung:
    python predict.py                        # neueste Session aus Saved/Telemetry/
    python predict.py session_XYZ.csv        # bestimmte Datei (relativ zu Saved/Telemetry/)
    python predict.py --watch                # Ordner überwachen, automatisch predicten

Ausgabe:
    Saved/Telemetry/result_<session>.txt
"""

import os
import sys
import glob
import time
import argparse
import joblib
import pandas as pd

# ─────────────────────────────────────────────
# Konfiguration
# ─────────────────────────────────────────────
SCRIPT_DIR    = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT  = os.path.dirname(SCRIPT_DIR)                          # UE5_Projects/Shootergame
TELEMETRY_DIR = os.path.join(PROJECT_ROOT, "Saved", "Telemetry")
MODEL_PATH_RF = os.path.join(SCRIPT_DIR, "results", "model_rf.pkl")
MODEL_PATH_LR = os.path.join(SCRIPT_DIR, "results", "model_lr.pkl")

# Dieselben Ausschlüsse wie in train_model.py
ZERO_FEATURES = ["AimAngularErrorMean", "AimAngularErrorStdDev", "HeadshotRate"]
META_COLS     = ["PlayerID", "Label"]

# Optimaler Threshold (aus threshold_analysis_random_forest.csv, C_FP=1 / C_FN=10)
THRESHOLD = 0.3


# ─────────────────────────────────────────────
# Hilfsfunktionen
# ─────────────────────────────────────────────

def get_latest_csv():
    """Neueste session_*.csv aus dem Telemetry-Ordner."""
    files = glob.glob(os.path.join(TELEMETRY_DIR, "session_*.csv"))
    if not files:
        return None
    return max(files, key=os.path.getmtime)


def load_models():
    """Lädt RF- und LR-Modell. Gibt (rf, lr) zurück; lr kann None sein."""
    if not os.path.exists(MODEL_PATH_RF):
        print(f"[FEHLER] Modell nicht gefunden: {MODEL_PATH_RF}")
        print("         → Bitte zuerst ML/train_model.py ausführen.")
        sys.exit(1)
    rf = joblib.load(MODEL_PATH_RF)
    lr = joblib.load(MODEL_PATH_LR) if os.path.exists(MODEL_PATH_LR) else None
    return rf, lr


def prepare_features(df):
    """
    Feature-Spalten extrahieren – dieselbe Logik wie train_model.py.
    Gibt (df_active, feature_cols) zurück.
    df_active = nur Spieler mit tatsächlicher Aktivität (TotalShots > 0).
    """
    # Geister-Zeilen herausfiltern (Spieler ohne Schüsse – reine Verbindungs-Artefakte)
    if "TotalShots" in df.columns:
        df_active = df[df["TotalShots"] > 0].copy()
    else:
        df_active = df.copy()

    if df_active.empty:
        return df_active, []

    # Feature-Spalten: alle außer Meta + dauerhaft-Null-Features
    exclude = set(META_COLS) | set(ZERO_FEATURES)
    feature_cols = [c for c in df_active.columns if c not in exclude]

    return df_active, feature_cols


def predict_session(csv_path, rf_model, lr_model=None):
    """
    Liest eine Session-CSV und gibt DataFrame mit Predictions zurück.
    """
    df = pd.read_csv(csv_path)
    df_active, feature_cols = prepare_features(df)

    if df_active.empty:
        return None, df, feature_cols

    X = df_active[feature_cols].values

    # Random Forest
    rf_proba = rf_model.predict_proba(X)[:, 1]
    rf_label = (rf_proba >= THRESHOLD).astype(int)

    df_active = df_active.copy()
    df_active["RF_Probability"] = rf_proba
    df_active["RF_Label"]       = rf_label

    # Logistic Regression (optional, als Zweitmeinung)
    if lr_model is not None:
        lr_proba = lr_model.predict_proba(X)[:, 1]
        lr_label = (lr_proba >= THRESHOLD).astype(int)
        df_active["LR_Probability"] = lr_proba
        df_active["LR_Label"]       = lr_label

    return df_active, df, feature_cols


def format_result(df_active, csv_path, lr_available=True):
    """Formatiert das Ergebnis als lesbaren String."""
    session_name = os.path.basename(csv_path)
    lines = []
    lines.append("=" * 55)
    lines.append("  CHEAT DETECTION – ERGEBNIS")
    lines.append("=" * 55)
    lines.append(f"  Session:   {session_name}")
    lines.append(f"  Threshold: τ = {THRESHOLD}  (C_FP=1 / C_FN=10)")
    lines.append(f"  Spieler analysiert: {len(df_active)}")
    lines.append("─" * 55)

    cheater_found = False

    for _, row in df_active.iterrows():
        player  = row.get("PlayerID", "Unbekannt")
        rf_prob = row["RF_Probability"]
        rf_flag = row["RF_Label"]

        verdict = "⚠  CHEATER" if rf_flag else "✓  CLEAN  "
        if rf_flag:
            cheater_found = True

        line = f"  {player:<28}  {rf_prob*100:5.1f}%   {verdict}"

        # LR als Zweitmeinung in Klammern
        if lr_available and "LR_Probability" in row:
            lr_prob = row["LR_Probability"]
            lr_flag = row["LR_Label"]
            lr_mark = "⚠" if lr_flag else "✓"
            line += f"   [LR: {lr_prob*100:.0f}% {lr_mark}]"

        lines.append(line)

    lines.append("─" * 55)

    if cheater_found:
        cheater_list = df_active[df_active["RF_Label"] == 1]["PlayerID"].tolist()
        lines.append(f"  → CHEATER ERKANNT: {', '.join(cheater_list)}")
    else:
        lines.append("  → Keine Cheater erkannt.")

    lines.append("=" * 55)
    return "\n".join(lines)


def run_prediction(csv_path, rf_model, lr_model):
    """Vollständiger Predict-Lauf für eine CSV-Datei."""
    print(f"\n[Predict] {os.path.basename(csv_path)}")

    df_active, df_full, feature_cols = predict_session(csv_path, rf_model, lr_model)

    if df_active is None or df_active.empty:
        msg = f"[INFO] Keine aktiven Spieler in {os.path.basename(csv_path)} (TotalShots = 0 für alle)."
        print(msg)
        return

    result_str = format_result(df_active, csv_path, lr_available=(lr_model is not None))
    print(result_str)

    # Ergebnis in result_<session>.txt speichern
    base = os.path.splitext(os.path.basename(csv_path))[0]   # z.B. session_2026-05-15_14-04-59
    result_filename = f"result_{base}.txt"
    result_path     = os.path.join(TELEMETRY_DIR, result_filename)

    with open(result_path, "w", encoding="utf-8") as f:
        f.write(result_str + "\n")

    print(f"\n[Gespeichert] {result_path}")
    return result_path


# ─────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Cheat Detection – Prediction Script (Random Forest + Logistic Regression)"
    )
    parser.add_argument(
        "csv_file",
        nargs="?",
        help="Session-CSV (Dateiname oder Pfad). Ohne Angabe: neueste Session."
    )
    parser.add_argument(
        "--watch",
        action="store_true",
        help="Ordner überwachen – automatisch predicten wenn neue CSV erscheint."
    )
    parser.add_argument(
        "--threshold",
        type=float,
        default=THRESHOLD,
        help=f"Entscheidungsschwelle τ (Standard: {THRESHOLD})"
    )
    args = parser.parse_args()

    # Threshold überschreiben falls angegeben
    if args.threshold != THRESHOLD:
        globals()["THRESHOLD"] = args.threshold

    # Modelle laden
    rf_model, lr_model = load_models()
    print(f"[Modell] Random Forest geladen: {MODEL_PATH_RF}")
    if lr_model:
        print(f"[Modell] Logistic Regression geladen: {MODEL_PATH_LR}")

    # ── Watch-Modus ──
    if args.watch:
        print(f"\n[Watch] Überwache Ordner: {TELEMETRY_DIR}")
        print("[Watch] Drücke Ctrl+C zum Beenden.\n")
        known = set(glob.glob(os.path.join(TELEMETRY_DIR, "session_*.csv")))
        try:
            while True:
                time.sleep(2)
                current = set(glob.glob(os.path.join(TELEMETRY_DIR, "session_*.csv")))
                new = current - known
                for f in sorted(new):
                    time.sleep(0.5)   # kurz warten bis Datei vollständig geschrieben
                    run_prediction(f, rf_model, lr_model)
                known = current
        except KeyboardInterrupt:
            print("\n[Watch] Beendet.")
        return

    # ── Einzel-Modus ──
    if args.csv_file:
        # Absoluter Pfad oder relativ zu Telemetry-Dir
        csv_path = args.csv_file
        if not os.path.isabs(csv_path):
            csv_path = os.path.join(TELEMETRY_DIR, csv_path)
        if not os.path.exists(csv_path):
            print(f"[FEHLER] Datei nicht gefunden: {csv_path}")
            sys.exit(1)
    else:
        csv_path = get_latest_csv()
        if csv_path is None:
            print(f"[FEHLER] Keine session_*.csv in {TELEMETRY_DIR}")
            sys.exit(1)
        print(f"[Auto] Neueste Session: {os.path.basename(csv_path)}")

    run_prediction(csv_path, rf_model, lr_model)


if __name__ == "__main__":
    main()
