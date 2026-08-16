"""
Regelbasierter Detektor gegen ML-Modell auf identischer Datengrundlage.

Kernvergleich der Arbeit. Beide Verfahren werden auf denselben Zeilen
bewertet — denen aus training_data_real.csv — und mit derselben Kostenmatrix
beurteilt (C_FP = 1, C_FN = 10).

Fairness-Bedingungen, ohne die der Vergleich nichts aussagt:

  * Das ML-Modell wird ueber seine Out-of-Fold-Vorhersagen bewertet
    (results_real/oof_predictions.csv). Jede Zeile stammt dort von einem
    Modell, das die zugehoerige Person nie gesehen hat.

  * Der Regeldetektor bekommt dieselben Zeilen. Er hat gar keine Trainings-
    phase, seine Grenzwerte wurden aber an frueheren Sitzungen von Hand
    eingestellt — er hat also einen kleinen, nicht quantifizierbaren
    Vorsprung. Das gehoert in die Diskussion, nicht wegdiskutiert.

  * Beide Verfahren werden zusaetzlich bei ihrem kostenoptimalen Schwellwert
    verglichen, nicht nur bei ihrer Voreinstellung. Sonst vergleicht man
    Schwellwertwahl statt Erkennungsleistung.

Verwendung:
    python compare_detectors.py
    python compare_detectors.py --data training_data_real.csv --pred results_real/oof_predictions.csv
"""

import os
import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
import argparse
import numpy as np
import pandas as pd

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
RULES_DIR    = os.path.join(PROJECT_ROOT, "RuleBased")

DEFAULT_DATA = os.path.join(SCRIPT_DIR, "training_data_real.csv")
DEFAULT_PRED = os.path.join(SCRIPT_DIR, "results_real", "oof_predictions.csv")
DEFAULT_CAL  = os.path.join(SCRIPT_DIR, "results_real", "regeln_kalibriert_oof.csv")
DEFAULT_OUT  = os.path.join(SCRIPT_DIR, "results_real")

C_FP = 1
C_FN = 10

CHEAT_SEP = "+"

# Den Regeldetektor als Modul einbinden, statt seine Logik zu kopieren.
# Eine Kopie wuerde beim naechsten Nachjustieren der Grenzwerte auseinander
# laufen und der Vergleich waere still veraltet.
sys.path.insert(0, RULES_DIR)
try:
    from rule_based_detector import analyze_player
except ImportError as e:
    print(f"[FEHLER] rule_based_detector.py nicht importierbar: {e}")
    print(f"         Erwartet in: {RULES_DIR}")
    sys.exit(1)


def confusion(y_true, y_pred):
    """(TP, FP, FN, TN) ohne sklearn-Abhaengigkeit."""
    y_true = np.asarray(y_true).astype(int)
    y_pred = np.asarray(y_pred).astype(int)
    tp = int(((y_true == 1) & (y_pred == 1)).sum())
    fp = int(((y_true == 0) & (y_pred == 1)).sum())
    fn = int(((y_true == 1) & (y_pred == 0)).sum())
    tn = int(((y_true == 0) & (y_pred == 0)).sum())
    return tp, fp, fn, tn


def metrics(y_true, y_pred):
    tp, fp, fn, tn = confusion(y_true, y_pred)
    precision = tp / (tp + fp) if (tp + fp) else 0.0
    recall    = tp / (tp + fn) if (tp + fn) else 0.0
    f1        = (2 * precision * recall / (precision + recall)
                 if (precision + recall) else 0.0)
    return {
        "TP": tp, "FP": fp, "FN": fn, "TN": tn,
        "Precision": precision, "Recall": recall, "F1": f1,
        "Cost": C_FP * fp + C_FN * fn,
    }


def best_threshold(y_true, scores, candidates):
    """Schwellwert mit minimalen Gesamtkosten."""
    best, best_cost = candidates[0], np.inf
    for t in candidates:
        cost = metrics(y_true, (scores >= t).astype(int))["Cost"]
        if cost < best_cost:
            best, best_cost = t, cost
    return best


def rule_scores(df):
    """Regeldetektor je Zeile laufen lassen."""
    scores, all_flags = [], []
    for _, row in df.iterrows():
        score, flags = analyze_player(row)
        scores.append(score)
        all_flags.append("; ".join(flags))
    return np.array(scores, dtype=float), all_flags


def print_block(title, m, extra=""):
    print(f"\n  {title}{extra}")
    print(f"    TP {m['TP']:>3}   FP {m['FP']:>3}   FN {m['FN']:>3}   TN {m['TN']:>3}")
    print(f"    Precision {m['Precision']*100:5.1f} %   "
          f"Recall {m['Recall']*100:5.1f} %   "
          f"F1 {m['F1']*100:5.1f} %   "
          f"Kosten {m['Cost']}")


def per_cheat_table(df, y_true, preds_by_name):
    """Recall je Cheat-Art fuer alle Verfahren nebeneinander."""
    cheat_series = df["CheatType"].fillna("").astype(str)
    all_cheats = sorted({t.strip()
                         for e in cheat_series if e.strip()
                         for t in e.split(CHEAT_SEP) if t.strip()})

    rows = []
    for cheat in all_cheats:
        involved = cheat_series.apply(
            lambda e: cheat in [t.strip() for t in e.split(CHEAT_SEP)]
        ).values
        alone = (cheat_series.str.strip() == cheat).values

        entry = {"Cheat": cheat,
                 "n_beteiligt": int(involved.sum()),
                 "n_sortenrein": int(alone.sum())}

        for name, pred in preds_by_name.items():
            for suffix, mask in (("beteiligt", involved), ("sortenrein", alone)):
                n = int(mask.sum())
                entry[f"{name}_{suffix}"] = (
                    float(np.asarray(pred)[mask].sum()) / n if n else np.nan
                )
        rows.append(entry)

    return pd.DataFrame(rows)


def main():
    parser = argparse.ArgumentParser(
        description="Regeldetektor gegen ML-Modell auf identischen Daten"
    )
    parser.add_argument("--data", default=DEFAULT_DATA)
    parser.add_argument("--pred", default=DEFAULT_PRED)
    parser.add_argument("--cal",  default=DEFAULT_CAL,
                        help="Out-of-fold-Vorhersagen der kalibrierten Regeln "
                             "(aus calibrate_rules.py). Optional.")
    parser.add_argument("--outdir", default=DEFAULT_OUT)
    args = parser.parse_args()

    for path, hint in ((args.data, "load_labeled_data.py"),
                       (args.pred, "train_model.py training_data_real.csv --outdir results_real")):
        if not os.path.exists(path):
            print(f"[FEHLER] Datei fehlt: {path}")
            print(f"         Zuerst ausfuehren: python {hint}")
            sys.exit(1)

    df   = pd.read_csv(args.data)
    pred = pd.read_csv(args.pred)

    if len(df) != len(pred):
        print(f"[FEHLER] {len(df)} Datenzeilen, aber {len(pred)} Vorhersagen.")
        print(f"         Beide muessen aus demselben Lauf stammen — "
              f"train_model.py auf {os.path.basename(args.data)} neu ausfuehren.")
        sys.exit(1)

    y = df["Label"].values.astype(int)

    print("=" * 66)
    print("  Regelbasierter Detektor  vs.  ML-Modell")
    print("=" * 66)
    print(f"  Datengrundlage : {os.path.basename(args.data)}")
    print(f"  Zeilen         : {len(df)}  ({int(y.sum())} Cheater, "
          f"{int((y == 0).sum())} sauber)")
    print(f"  Kostenmatrix   : C_FP = {C_FP}, C_FN = {C_FN}")

    # ── Regeldetektor ────────────────────────────────────────────────
    scores, flags = rule_scores(df)

    print("\n" + "-" * 66)
    print("  REGELBASIERTER DETEKTOR")
    print("-" * 66)

    m_rule_50 = metrics(y, (scores >= 50).astype(int))
    print_block("Voreinstellung (Risk >= 50 = CHEATER)", m_rule_50)

    m_rule_25 = metrics(y, (scores >= 25).astype(int))
    print_block("Inkl. Verdachtsfaellen (Risk >= 25)", m_rule_25)

    rule_tau = best_threshold(y, scores, list(range(0, 101, 5)))
    m_rule_opt = metrics(y, (scores >= rule_tau).astype(int))
    print_block("Kostenoptimal", m_rule_opt, f" (Risk >= {rule_tau})")

    # ── ML-Modell ────────────────────────────────────────────────────
    print("\n" + "-" * 66)
    print("  ML-MODELL (Random Forest, out-of-fold)")
    print("-" * 66)

    rf_prob = pred["RF_Probability"].values

    m_ml_50 = metrics(y, (rf_prob >= 0.5).astype(int))
    print_block("Standardschwelle (tau = 0.5)", m_ml_50)

    ml_tau = best_threshold(y, rf_prob, np.round(np.arange(0.01, 1.0, 0.01), 2))
    m_ml_opt = metrics(y, (rf_prob >= ml_tau).astype(int))
    print_block("Kostenoptimal", m_ml_opt, f" (tau = {ml_tau:.2f})")

    # ── Kalibrierter Regeldetektor ───────────────────────────────────
    #
    # Die Handkalibrierung stammt aus einer einzelnen frueheren Sitzung, das
    # ML-Modell wurde dagegen auf diesen Daten trainiert. Ein Vergleich der
    # beiden waere ein Vergleich der Kalibrierung, nicht der Verfahren.
    # calibrate_rules.py bestimmt die Grenzwerte mit demselben gruppierten
    # Protokoll wie das ML-Modell; erst diese Variante ist vergleichbar.
    cal_frac = None
    if os.path.exists(args.cal):
        cal_df = pd.read_csv(args.cal)
        if len(cal_df) == len(df):
            cal_frac = cal_df["RuleCalibrated_Frac"].values
            m_cal = metrics(y, (cal_frac >= 0.5).astype(int))
            print("\n" + "-" * 66)
            print("  REGELDETEKTOR, DATENBASIERT KALIBRIERT (out-of-fold)")
            print("-" * 66)
            print_block("Mehrheitsentscheid ueber die Faltenaufteilungen", m_cal)
        else:
            print(f"\n[WARNUNG] {os.path.basename(args.cal)} passt nicht zu den "
                  f"Daten ({len(cal_df)} statt {len(df)} Zeilen) — ignoriert.")
    else:
        print(f"\n[Hinweis] {os.path.basename(args.cal)} fehlt. Fuer den fairen "
              f"Vergleich\n          zuerst 'python calibrate_rules.py' ausfuehren.")

    # ── Kombiniertes Verfahren ───────────────────────────────────────
    #
    # "auffaellig, wenn eines der beiden Verfahren anschlaegt". Beide
    # Schwellen werden GEMEINSAM optimiert — wuerde man die einzeln
    # optimierten Schwellen uebernehmen, waere die Kombination gegenueber den
    # Einzelverfahren systematisch benachteiligt, weil sich die Fehlalarme
    # addieren.
    m_comb = None
    if cal_frac is not None:
        best = (np.inf, None, None)
        for tr in np.arange(0.0, 1.01, 0.1):
            r_hit = cal_frac >= tr
            for tm in np.round(np.arange(0.01, 1.0, 0.01), 2):
                c = metrics(y, (r_hit | (rf_prob >= tm)).astype(int))["Cost"]
                if c < best[0]:
                    best = (c, tr, tm)
        _, comb_tr, comb_tm = best
        comb_pred = ((cal_frac >= comb_tr) | (rf_prob >= comb_tm)).astype(int)
        m_comb = metrics(y, comb_pred)

        print("\n" + "-" * 66)
        print("  KOMBINATION: Regeln ODER ML")
        print("-" * 66)
        print_block("Kostenoptimal", m_comb,
                    f" (Regeln >= {comb_tr:.1f}, tau >= {comb_tm:.2f})")

    # ── Gegenueberstellung ───────────────────────────────────────────
    print("\n" + "=" * 66)
    print("  GEGENUEBERSTELLUNG bei jeweils kostenoptimaler Schwelle")
    print("=" * 66)
    cols = [("Regeln\n(Hand)", m_rule_opt), ("Regeln\n(kalibr.)", None),
            ("ML", m_ml_opt), ("Kombi", m_comb)]
    if cal_frac is not None:
        cols[1] = ("Regeln (kalibr.)", m_cal)
    cols = [(n, m) for n, m in cols if m is not None]

    header = "".join(f"{n.replace(chr(10), ' '):>18}" for n, _ in cols)
    print(f"  {'':<16}{header}")
    print("  " + "-" * (16 + 18 * len(cols)))
    for key, label, pct in (("Recall", "Recall", True),
                            ("Precision", "Precision", True),
                            ("F1", "F1", True),
                            ("FP", "Fehlalarme", False),
                            ("FN", "uebersehen", False),
                            ("Cost", "Gesamtkosten", False)):
        line = f"  {label:<16}"
        for _, m in cols:
            line += f"{m[key]*100:17.1f}%" if pct else f"{m[key]:>18.0f}"
        print(line)

    best_name, best_m = min(cols, key=lambda t: t[1]["Cost"])
    print(f"\n  -> Geringste Gesamtkosten: {best_name.replace(chr(10), ' ')} "
          f"({best_m['Cost']})")
    print("     (Kosten = 1 x Fehlalarm + 10 x uebersehener Cheater)")

    if cal_frac is not None:
        print(f"\n  Wichtig fuer die Einordnung: 'Regeln (Hand)' benutzt Grenzwerte,")
        print(f"  die an einer einzelnen frueheren Sitzung gesetzt wurden. Nur")
        print(f"  'Regeln (kalibr.)' wurde unter denselben Bedingungen bestimmt")
        print(f"  wie das ML-Modell und ist damit fair vergleichbar.")

    # ── Uebereinstimmung ─────────────────────────────────────────────
    rule_pred = (scores >= rule_tau).astype(int)
    ml_pred   = (rf_prob >= ml_tau).astype(int)

    # Fuer die Komplementaritaet zaehlt der FAIR kalibrierte Regeldetektor.
    # Mit der Handkalibrierung entstuende ein zu guenstiges Bild: die Regeln
    # verfehlen dort Faelle nur wegen falscher Schwellen, was faelschlich als
    # inhaltliche Ergaenzung durch das ML-Modell erschiene.
    if cal_frac is not None:
        rule_cmp = (cal_frac >= 0.5).astype(int)
        cmp_label = "kalibrierter Regeldetektor"
    else:
        rule_cmp = rule_pred
        cmp_label = "Regeldetektor (Handkalibrierung)"

    agree = int((rule_cmp == ml_pred).sum())

    print("\n" + "-" * 66)
    print(f"  UEBEREINSTIMMUNG: {cmp_label} gegen ML")
    print("-" * 66)
    print(f"  Gleiches Urteil : {agree} von {len(y)} Zeilen "
          f"({agree/len(y)*100:.1f} %)")
    both  = int(((rule_cmp == 1) & (ml_pred == 1) & (y == 1)).sum())
    only_r = int(((rule_cmp == 1) & (ml_pred == 0) & (y == 1)).sum())
    only_m = int(((rule_cmp == 0) & (ml_pred == 1) & (y == 1)).sum())
    none_  = int(((rule_cmp == 0) & (ml_pred == 0) & (y == 1)).sum())
    print(f"\n  Von {int(y.sum())} echten Cheatern erkannt:")
    print(f"    von beiden        : {both}")
    print(f"    nur von den Regeln: {only_r}")
    print(f"    nur vom ML-Modell : {only_m}")
    print(f"    von keinem        : {none_}")
    if m_comb is not None:
        gain = m_ml_opt["Cost"] - m_comb["Cost"]
        print(f"\n  Nutzen der Kombination gegenueber dem besseren Einzelverfahren:")
        print(f"    Kosten ML allein   : {m_ml_opt['Cost']}")
        print(f"    Kosten Kombination : {m_comb['Cost']}   ({gain:+d})")
        if gain > 0:
            print(f"\n  Die Kombination lohnt sich: die {only_r} nur von den Regeln")
            print(f"  erkannten Faelle ergaenzen das Modell.")
        else:
            print(f"\n  Die Kombination lohnt sich NICHT. Sobald der Regeldetektor")
            print(f"  fair kalibriert ist, erkennt er kaum noch etwas, das das")
            print(f"  ML-Modell uebersieht — beide Verfahren stuetzen sich auf")
            print(f"  dieselben Merkmale und scheitern an denselben Faellen.")
            print(f"  Das ist ein belastbares Ergebnis gegen die verbreitete")
            print(f"  Annahme, hybride Verfahren seien grundsaetzlich ueberlegen.")
    elif only_r or only_m:
        print(f"\n  Die {only_r + only_m} nur einseitig erkannten Faelle sind das "
              f"Argument fuer eine\n  Kombination beider Verfahren statt einer "
              f"Entscheidung fuer eines.")

    # ── Recall je Cheat-Art ──────────────────────────────────────────
    if "CheatType" in df.columns:
        print("\n" + "=" * 66)
        print("  RECALL JE CHEAT-ART")
        print("=" * 66)
        preds = {"Regeln": rule_pred, "ML": ml_pred}
        if cal_frac is not None:
            preds["RegelnKal"] = (cal_frac >= 0.5).astype(int)
            preds["Kombi"]     = comb_pred
        tab = per_cheat_table(df, y, preds)

        names = list(preds.keys())

        def pct(v):
            return "      –" if (v is None or (isinstance(v, float) and np.isnan(v))) \
                   else f"{v*100:6.1f}%"

        print("\n  Nur sortenreine Runden — nur dort ist die Erkennung eindeutig")
        print(f"  einem Cheat zuzuordnen.\n")
        print(f"  {'Cheat':<13}{'n':>4}" + "".join(f"{n:>11}" for n in names))
        print("  " + "-" * (17 + 11 * len(names)))
        for r in tab.itertuples():
            line = f"  {r.Cheat:<13}{r.n_sortenrein:>4}"
            for n in names:
                line += f"{pct(getattr(r, f'{n}_sortenrein')):>11}"
            print(line)

        print(f"\n  Zum Vergleich, alle Runden mit diesem Cheat (auch gemeinsam")
        print(f"  mit anderen) — Werte hier tendenziell zu hoch:\n")
        print(f"  {'Cheat':<13}{'n':>4}" + "".join(f"{n:>11}" for n in names))
        print("  " + "-" * (17 + 11 * len(names)))
        for r in tab.itertuples():
            line = f"  {r.Cheat:<13}{r.n_beteiligt:>4}"
            for n in names:
                line += f"{pct(getattr(r, f'{n}_beteiligt')):>11}"
            print(line)

        tab.to_csv(os.path.join(args.outdir, "recall_per_cheat_vergleich.csv"),
                   index=False)

    # ── Ergebnisdateien ──────────────────────────────────────────────
    os.makedirs(args.outdir, exist_ok=True)

    rows_sum = [
        {"Verfahren": "Regeln (Handkalibrierung)", "Schwelle": "Risk >= 50 (Voreinstellung)", **m_rule_50},
        {"Verfahren": "Regeln (Handkalibrierung)", "Schwelle": f"Risk >= {rule_tau} (kostenoptimal)", **m_rule_opt},
        {"Verfahren": "ML (RF, OOF)", "Schwelle": "tau = 0.50 (Standard)", **m_ml_50},
        {"Verfahren": "ML (RF, OOF)", "Schwelle": f"tau = {ml_tau:.2f} (kostenoptimal)", **m_ml_opt},
    ]
    if cal_frac is not None:
        rows_sum.append({"Verfahren": "Regeln (datenbasiert kalibriert)",
                         "Schwelle": "Mehrheitsentscheid, out-of-fold", **m_cal})
        rows_sum.append({"Verfahren": "Kombination Regeln ODER ML",
                         "Schwelle": f"Regeln >= {comb_tr:.1f}, tau >= {comb_tm:.2f}", **m_comb})
    summary = pd.DataFrame(rows_sum)
    summary_path = os.path.join(args.outdir, "vergleich_regeln_vs_ml.csv")
    summary.to_csv(summary_path, index=False)

    detail = df[[c for c in ["SessionFile", "PlayerID", "Person", "Label", "CheatType"]
                 if c in df.columns]].copy()
    detail["RuleScore"]     = scores
    detail["RuleFlags"]     = flags
    detail["RulePred"]      = rule_pred
    detail["RF_Probability"] = rf_prob
    detail["MLPred"]        = ml_pred
    detail_path = os.path.join(args.outdir, "vergleich_je_zeile.csv")
    detail.to_csv(detail_path, index=False)

    print(f"\n[Gespeichert] {summary_path}")
    print(f"[Gespeichert] {detail_path}")


if __name__ == "__main__":
    main()
