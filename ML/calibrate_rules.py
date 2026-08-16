"""
Datenbasierte Kalibrierung der Grenzwerte des Regeldetektors.

Warum das noetig ist
--------------------
Der Regeldetektor benutzt Grenzwerte, die im August 2026 von Hand an einer
EINZELNEN Sitzung eingestellt wurden. Auf den 296 real erhobenen Zeilen sind
sie nachweislich falsch: die Regel ON_TARGET_RATIO loest ab 45 % aus, der
Normalwert sauberer Spieler liegt aber bei 73 %. Entsprechend markiert der
Detektor auf der Verdachtsstufe 264 von 268 sauberen Spielern.

Solange das so bleibt, ist der Vergleich mit dem ML-Modell nicht aussage-
kraeftig: das Modell wurde auf diesen Daten trainiert, der Regeldetektor auf
anderen. Ein Leistungsunterschied waere dann kein Unterschied der Verfahren,
sondern einer der Kalibrierung.

Verfahren
---------
Dasselbe personengruppierte Protokoll wie beim ML-Modell:

  1. StratifiedGroupKFold ueber Person, gemittelt ueber mehrere
     Faltenaufteilungen.
  2. Auf der TRAININGSFALTE werden die Grenzwerte so gesetzt, dass jede
     einzelne Regel hoechstens einen Anteil alpha der SAUBEREN Spieler
     ausloest. alpha ist damit die zulaessige Fehlalarmquote je Regel.
  3. alpha und die finale Risk-Score-Schwelle werden gemeinsam per
     Gittersuche kostenminimal bestimmt (C_FP = 1, C_FN = 10) — ebenfalls
     ausschliesslich auf der Trainingsfalte.
  4. Bewertet wird ausschliesslich auf der TESTFALTE.

Der Ansatz veraendert die Regel-Logik und die Punktgewichte nicht. Kalibriert
werden nur die Schwellen. Statt zwoelf freier Parameter gibt es genau zwei
(alpha und die Risk-Schwelle), was die Gefahr einer Ueberanpassung an die
kleine Stichprobe deutlich begrenzt.

Verwendung:
    python calibrate_rules.py
    python calibrate_rules.py --seeds 10
"""

import os
import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
import argparse
import numpy as np
import pandas as pd
from sklearn.model_selection import StratifiedGroupKFold

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
RULES_DIR    = os.path.join(PROJECT_ROOT, "RuleBased")

DEFAULT_DATA = os.path.join(SCRIPT_DIR, "training_data_real.csv")
DEFAULT_OUT  = os.path.join(SCRIPT_DIR, "results_real")

C_FP, C_FN = 1, 10
N_SPLITS   = 5

# Zulaessige Fehlalarmquote je Einzelregel, aus der die Schwellen abgeleitet
# werden. Wird auf der Trainingsfalte gewaehlt.
ALPHA_GRID = [0.005, 0.01, 0.02, 0.05, 0.10, 0.20]

# Moegliche Schwellen fuer den aggregierten Risk-Score.
RISK_GRID = list(range(5, 101, 5))

sys.path.insert(0, RULES_DIR)
from rule_based_detector import analyze_player, DEFAULT_LIMITS

# Fuer jede kalibrierbare Regel: wie der zugehoerige Messwert berechnet wird,
# in welche Richtung die Regel ausloest, und ob es eine strengere Zweitstufe
# gibt. "greater" heisst: Wert > Grenzwert loest aus.
#
# Die Zweitstufen (…_HIGH) bekommen ein Viertel der Fehlalarmquote, damit die
# Abstufung "auffaellig" / "extrem auffaellig" erhalten bleibt.
RULE_SPECS = {
    "RAPID_FIRE":      dict(direction="greater", tier=1.0),
    "RAPID_FIRE_HIGH": dict(direction="greater", tier=0.25),
    "REACTION_MEAN":   dict(direction="less",    tier=1.0),
    "REACTION_STD":    dict(direction="less",    tier=1.0),
    "INTERVAL_STD":    dict(direction="less",    tier=1.0),
    "AIM_FLIP_RATIO":  dict(direction="greater", tier=1.0),
    "AIM_ERROR":       dict(direction="less",    tier=1.0),
    "AIM_ERROR_HIGH":  dict(direction="less",    tier=0.25),
    "ON_TARGET_RATIO": dict(direction="greater", tier=1.0),
    "SPEED_MAX":       dict(direction="greater", tier=1.0),
    "SPEED_VIOLATION": dict(direction="greater", tier=1.0),
    "GOD_KILLS":       dict(direction="greater", tier=1.0),
}


def rule_values(df):
    """
    Die Messwerte, auf denen die einzelnen Regeln arbeiten — inklusive der
    Gueltigkeitsfilter aus analyze_player(). Nur wo eine Regel ueberhaupt
    greifen kann, darf ihr Grenzwert aus der Verteilung abgeleitet werden.
    """
    v = {}
    sim = df["ShotIntervalMean"].astype(float)
    active_sps = np.where(sim > 0, 1.0 / sim.replace(0, np.nan), 0.0)
    active_sps = pd.Series(active_sps, index=df.index).fillna(0.0)

    v["RAPID_FIRE"]      = active_sps
    v["RAPID_FIRE_HIGH"] = active_sps

    # Nur Zeilen mit tatsaechlich gemessener Reaktionszeit
    rt = df["ReactionTimeMean"].astype(float)
    v["REACTION_MEAN"] = rt.where(rt > 0)
    v["REACTION_STD"]  = df["ReactionTimeStdDev"].astype(float).where(rt > 0)

    # Die Autofire-Regel greift nur bei hoher Kadenz
    v["INTERVAL_STD"] = df["ShotIntervalStdDev"].astype(float).where(active_sps > 5.0)

    v["AIM_FLIP_RATIO"] = df["AimFlipRatio"].astype(float)

    # Zielfehler nur bei ausreichender Stichprobe
    enough = df["AimErrorSampleCount"].astype(float) >= DEFAULT_LIMITS["MIN_AIM_ERROR_SAMPLES"]
    v["AIM_ERROR"]      = df["AimAngularErrorMean"].astype(float).where(enough)
    v["AIM_ERROR_HIGH"] = v["AIM_ERROR"]

    shots = df["TotalShots"].astype(float)
    ratio = (df["AimErrorSampleCount"].astype(float) / shots.replace(0, np.nan))
    v["ON_TARGET_RATIO"] = ratio.where(shots >= DEFAULT_LIMITS["MIN_SHOTS_FOR_RATIO"])

    v["SPEED_MAX"]       = df["MovementSpeedMax"].astype(float)
    v["SPEED_VIOLATION"] = df["SpeedViolationRatio"].astype(float)

    # God-Mode greift nur bei null Toden
    v["GOD_KILLS"] = df["TotalKills"].astype(float).where(df["TotalDeaths"].astype(float) == 0)

    return v


def fit_limits(df_train, alpha):
    """
    Grenzwerte aus der Verteilung der SAUBEREN Trainingsspieler ableiten.

    Jede Regel wird so gesetzt, dass sie hoechstens den Anteil alpha der
    sauberen Spieler ausloest — die Fehlalarmquote je Einzelregel ist damit
    beschraenkt und nicht mehr das Ergebnis einer Schaetzung von Hand.
    """
    clean = df_train[df_train["Label"] == 0]
    vals  = rule_values(clean)
    limits = {}

    for name, spec in RULE_SPECS.items():
        series = vals[name].dropna()
        if len(series) < 10:
            # Zu wenige saubere Beobachtungen: Standardwert behalten, statt
            # aus drei Zahlen eine Schwelle zu erfinden.
            limits[name] = DEFAULT_LIMITS[name]
            continue

        a = alpha * spec["tier"]
        if spec["direction"] == "greater":
            limits[name] = float(np.quantile(series, 1.0 - a))
        else:
            limits[name] = float(np.quantile(series, a))

    # Die Zweitstufen muessen strenger bleiben als die Grundstufe, sonst
    # vergibt der Detektor die hoehere Punktzahl bei schwaecherem Befund.
    limits["RAPID_FIRE_HIGH"] = max(limits["RAPID_FIRE_HIGH"], limits["RAPID_FIRE"])
    limits["AIM_ERROR_HIGH"]  = min(limits["AIM_ERROR_HIGH"],  limits["AIM_ERROR"])

    return limits


def scores_for(df, limits):
    return np.array([analyze_player(r, limits)[0] for _, r in df.iterrows()], dtype=float)


def cost_of(y, pred):
    fp = int(((y == 0) & (pred == 1)).sum())
    fn = int(((y == 1) & (pred == 0)).sum())
    return C_FP * fp + C_FN * fn


def metrics(y, pred):
    tp = int(((y == 1) & (pred == 1)).sum())
    fp = int(((y == 0) & (pred == 1)).sum())
    fn = int(((y == 1) & (pred == 0)).sum())
    tn = int(((y == 0) & (pred == 0)).sum())
    prec = tp / (tp + fp) if (tp + fp) else 0.0
    rec  = tp / (tp + fn) if (tp + fn) else 0.0
    f1   = 2 * prec * rec / (prec + rec) if (prec + rec) else 0.0
    return dict(TP=tp, FP=fp, FN=fn, TN=tn, Precision=prec, Recall=rec,
                F1=f1, Cost=C_FP * fp + C_FN * fn)


def main():
    parser = argparse.ArgumentParser(description="Grenzwerte des Regeldetektors kalibrieren")
    parser.add_argument("--data", default=DEFAULT_DATA)
    parser.add_argument("--outdir", default=DEFAULT_OUT)
    parser.add_argument("--seeds", type=int, default=10)
    args = parser.parse_args()

    if not os.path.exists(args.data):
        print(f"[FEHLER] {args.data} fehlt. Zuerst load_labeled_data.py ausfuehren.")
        sys.exit(1)

    df = pd.read_csv(args.data)
    y  = df["Label"].values.astype(int)
    groups = df["Person"].astype(str).values

    print("=" * 70)
    print("  Datenbasierte Kalibrierung der Regel-Grenzwerte")
    print("=" * 70)
    print(f"  {len(df)} Zeilen, {int(y.sum())} Cheater, "
          f"{len(np.unique(groups))} Personen")
    print(f"  {args.seeds} Faltenaufteilungen x {N_SPLITS} Falten")
    print(f"  Kostenmatrix C_FP = {C_FP}, C_FN = {C_FN}\n")

    # ── Ausgangslage: Handkalibrierung ───────────────────────────────
    base_scores = scores_for(df, None)
    base_50 = metrics(y, (base_scores >= 50).astype(int))
    best_base_tau = min(RISK_GRID, key=lambda t: cost_of(y, (base_scores >= t).astype(int)))
    base_opt = metrics(y, (base_scores >= best_base_tau).astype(int))

    # ── Kalibrierung ─────────────────────────────────────────────────
    oof = np.zeros(len(df))
    oof_n = np.zeros(len(df))
    chosen = []

    for seed in range(args.seeds):
        cv = StratifiedGroupKFold(n_splits=N_SPLITS, shuffle=True, random_state=seed)
        for tr, te in cv.split(df, y, groups):
            df_tr, df_te = df.iloc[tr], df.iloc[te]
            y_tr = y[tr]

            best = (np.inf, None, None)
            for alpha in ALPHA_GRID:
                lim = fit_limits(df_tr, alpha)
                s_tr = scores_for(df_tr, lim)
                for tau in RISK_GRID:
                    c = cost_of(y_tr, (s_tr >= tau).astype(int))
                    if c < best[0]:
                        best = (c, alpha, tau, lim)

            _, alpha, tau, lim = best
            chosen.append(dict(seed=seed, alpha=alpha, risk_tau=tau, **lim))

            # Vorhersage auf der Testfalte, als 0/1 gemittelt ueber alle
            # Aufteilungen — analog zur Mittelung beim ML-Modell.
            s_te = scores_for(df_te, lim)
            oof[te]   += (s_te >= tau).astype(int)
            oof_n[te] += 1

        print(f"  Faltenaufteilung {seed + 1}/{args.seeds} fertig", end="\r")

    print(" " * 40, end="\r")
    oof_frac = oof / np.maximum(oof_n, 1)
    cal_pred = (oof_frac >= 0.5).astype(int)   # Mehrheitsentscheid
    cal = metrics(y, cal_pred)

    df_chosen = pd.DataFrame(chosen)

    # ── Ausgabe ──────────────────────────────────────────────────────
    def show(title, m, extra=""):
        print(f"\n  {title}{extra}")
        print(f"    TP {m['TP']:>3}  FP {m['FP']:>3}  FN {m['FN']:>3}  TN {m['TN']:>3}")
        print(f"    Precision {m['Precision']*100:5.1f} %   Recall {m['Recall']*100:5.1f} %"
              f"   F1 {m['F1']*100:5.1f} %   Kosten {m['Cost']}")

    print("-" * 70)
    print("  A  HANDKALIBRIERUNG (Grenzwerte aus einer einzelnen Sitzung)")
    print("-" * 70)
    show("Voreinstellung Risk >= 50", base_50)
    show("Beste erreichbare Schwelle", base_opt, f"  (Risk >= {best_base_tau})")
    print("\n    Hinweis: die beste Schwelle wurde hier auf denselben Daten")
    print("    gesucht, auf denen bewertet wird — der Wert ist also zugunsten")
    print("    der Handkalibrierung geschoent.")

    print("\n" + "-" * 70)
    print("  B  DATENBASIERTE KALIBRIERUNG (gruppiert, out-of-fold)")
    print("-" * 70)
    show("Mehrheitsentscheid ueber alle Faltenaufteilungen", cal)

    print(f"\n    Gewaehlte Parameter ueber {len(df_chosen)} Falten:")
    print(f"      alpha (Fehlalarmquote je Regel): "
          f"Median {df_chosen['alpha'].median():.3f}, "
          f"Spanne {df_chosen['alpha'].min():.3f}-{df_chosen['alpha'].max():.3f}")
    print(f"      Risk-Schwelle: Median {df_chosen['risk_tau'].median():.0f}, "
          f"Spanne {df_chosen['risk_tau'].min():.0f}-{df_chosen['risk_tau'].max():.0f}")

    print("\n" + "-" * 70)
    print("  GRENZWERTE IM VERGLEICH")
    print("-" * 70)
    print(f"\n  {'Regel':<18}{'von Hand':>12}{'kalibriert':>14}{'Streuung':>12}")
    print("  " + "-" * 54)
    for name in RULE_SPECS:
        med = df_chosen[name].median()
        std = df_chosen[name].std()
        print(f"  {name:<18}{DEFAULT_LIMITS[name]:>12.4g}{med:>14.4g}{std:>12.4g}")

    print("\n" + "=" * 70)
    print("  ERGEBNIS")
    print("=" * 70)
    d_cost = base_50["Cost"] - cal["Cost"]
    print(f"\n  Kosten Handkalibrierung (Risk >= 50) : {base_50['Cost']}")
    print(f"  Kosten datenbasiert kalibriert       : {cal['Cost']}")
    print(f"  Differenz                            : {d_cost:+d}")
    if cal["Cost"] < base_50["Cost"]:
        print("\n  Die Kalibrierung verbessert den Regeldetektor. Erst dieser")
        print("  Wert darf dem ML-Modell gegenuebergestellt werden — der")
        print("  Vergleich mit der Handkalibrierung waere ein Vergleich der")
        print("  Kalibrierung, nicht der Verfahren.")
    else:
        print("\n  Die Kalibrierung verbessert den Regeldetektor NICHT. Auch das")
        print("  ist ein verwertbares Ergebnis: die Grenze liegt dann nicht an")
        print("  den Schwellen, sondern an den Merkmalen selbst.")

    os.makedirs(args.outdir, exist_ok=True)
    summary = pd.DataFrame([
        {"Variante": "Handkalibrierung, Risk >= 50", **base_50},
        {"Variante": f"Handkalibrierung, beste Schwelle ({best_base_tau})", **base_opt},
        {"Variante": "Datenbasiert kalibriert (out-of-fold)", **cal},
    ])
    p1 = os.path.join(args.outdir, "regeln_kalibriert.csv")
    p2 = os.path.join(args.outdir, "regeln_kalibriert_grenzwerte.csv")
    summary.to_csv(p1, index=False)
    df_chosen.to_csv(p2, index=False)

    # Vorhersagen sichern, damit compare_detectors.py sie verwenden kann
    out_pred = df[[c for c in ["SessionFile", "PlayerID", "Person", "Label", "CheatType"]
                   if c in df.columns]].copy()
    out_pred["RuleCalibrated_Frac"]  = oof_frac
    out_pred["RuleCalibrated_Label"] = cal_pred
    p3 = os.path.join(args.outdir, "regeln_kalibriert_oof.csv")
    out_pred.to_csv(p3, index=False)

    print(f"\n[Gespeichert] {p1}")
    print(f"[Gespeichert] {p2}")
    print(f"[Gespeichert] {p3}")


if __name__ == "__main__":
    main()
