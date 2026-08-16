"""
Rechenaufwand der beiden Erkennungsverfahren.

Beantwortet den Teil "Rechenaufwand" der Forschungsfrage mit eigenen
Messwerten statt mit Literaturangaben.

Gemessen wird auf den real erhobenen Sessions:
  * Regeldetektor : Einlesen + Regelauswertung je Session
  * ML-Modell     : Modellladen (einmalig) und Inferenz je Session
  * Training      : einmalige Kosten des Modells
  * Speicher      : Spitzenverbrauch der Inferenz, Modellgroesse auf Platte

Nicht messbar von hier aus: der Overhead des UTelemetryCollector im
laufenden Spiel (10 Hz je Pawn). Der faellt auf dem Spielserver an und
braucht eine Messung im laufenden Betrieb — Anleitung am Ende der Ausgabe.

Verwendung:
    python runtime_analysis.py
    python runtime_analysis.py D:\\telemetrysessions\\all --repeats 10
"""

import os
import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
import glob
import time
import json
import platform
import argparse
import tracemalloc
import numpy as np
import pandas as pd

SCRIPT_DIR   = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
RULES_DIR    = os.path.join(PROJECT_ROOT, "RuleBased")

DEFAULT_DIR = r"D:\telemetrysessions\all"
DEFAULT_OUT = os.path.join(SCRIPT_DIR, "results_real")

sys.path.insert(0, RULES_DIR)
from rule_based_detector import analyze_player

from features import add_derived_features, get_feature_cols


def timeit(fn, repeats):
    """Median statt Mittelwert: unempfindlich gegen einzelne Ausreisser
    durch Hintergrundlast des Betriebssystems."""
    samples = []
    for _ in range(repeats):
        t0 = time.perf_counter()
        fn()
        samples.append(time.perf_counter() - t0)
    return float(np.median(samples)), float(np.min(samples)), float(np.max(samples))


def fmt_ms(sec):
    return f"{sec * 1000:.2f} ms"


def main():
    parser = argparse.ArgumentParser(description="Rechenaufwand der Erkennungsverfahren")
    parser.add_argument("session_dir", nargs="?", default=DEFAULT_DIR)
    parser.add_argument("--repeats", type=int, default=7,
                        help="Wiederholungen je Messung (Median wird berichtet)")
    parser.add_argument("--models", default=os.path.join(SCRIPT_DIR, "results_real"))
    parser.add_argument("--outdir", default=DEFAULT_OUT)
    args = parser.parse_args()

    files = sorted(glob.glob(os.path.join(args.session_dir, "session_*.csv")))
    if not files:
        print(f"[FEHLER] Keine session_*.csv in {args.session_dir}")
        sys.exit(1)

    print("=" * 68)
    print("  Rechenaufwand der Erkennungsverfahren")
    print("=" * 68)
    print(f"  System   : {platform.processor() or platform.machine()}")
    print(f"  Python   : {platform.python_version()}")
    print(f"  Sessions : {len(files)}")
    print(f"  Messungen: Median aus {args.repeats} Wiederholungen")

    total_rows = sum(len(pd.read_csv(f)) for f in files)
    print(f"  Zeilen   : {total_rows}")

    results = {}

    # ── Regeldetektor ────────────────────────────────────────────────
    print("\n" + "-" * 68)
    print("  REGELBASIERTER DETEKTOR")
    print("-" * 68)

    def rules_all():
        for f in files:
            df = pd.read_csv(f)
            for _, row in df.iterrows():
                analyze_player(row)

    med, lo, hi = timeit(rules_all, args.repeats)
    print(f"\n  Alle {len(files)} Sessions   : {med:.3f} s   "
          f"(min {lo:.3f} / max {hi:.3f})")
    print(f"  Je Session          : {fmt_ms(med / len(files))}")
    print(f"  Je Spielerzeile     : {fmt_ms(med / total_rows)}")
    results["regeln_gesamt_s"]     = round(med, 4)
    results["regeln_je_session_ms"] = round(med / len(files) * 1000, 3)
    results["regeln_je_zeile_ms"]   = round(med / total_rows * 1000, 4)

    # Reine Regelauswertung ohne CSV-Einlesen — trennt Ein-/Ausgabe von
    # Rechenaufwand, weil das Einlesen bei einer Live-Anbindung entfaellt.
    all_rows = pd.concat([pd.read_csv(f) for f in files], ignore_index=True)

    def rules_only():
        for _, row in all_rows.iterrows():
            analyze_player(row)

    med_r, _, _ = timeit(rules_only, args.repeats)
    print(f"\n  davon Regellogik    : {med_r:.3f} s  "
          f"({fmt_ms(med_r / total_rows)} je Zeile)")
    print(f"  davon CSV-Einlesen  : {med - med_r:.3f} s")
    results["regeln_logik_je_zeile_ms"] = round(med_r / total_rows * 1000, 4)

    # ── ML-Modell ────────────────────────────────────────────────────
    print("\n" + "-" * 68)
    print("  ML-MODELL (Random Forest)")
    print("-" * 68)

    import joblib
    model_path = os.path.join(args.models, "model_rf.pkl")
    if not os.path.exists(model_path):
        print(f"\n  [uebersprungen] {model_path} fehlt.")
        print(f"  Zuerst: python train_model.py training_data_real.csv --outdir results_real")
    else:
        med_load, _, _ = timeit(lambda: joblib.load(model_path), args.repeats)
        print(f"\n  Modell laden (einmalig) : {fmt_ms(med_load)}")

        model = joblib.load(model_path)
        df_all = add_derived_features(all_rows)
        feat = get_feature_cols(df_all)
        with open(os.path.join(args.models, "feature_columns.json"), encoding="utf-8") as f:
            feat = json.load(f)
        X = df_all[feat].values

        med_pred, lo_p, hi_p = timeit(lambda: model.predict_proba(X), args.repeats)
        print(f"  Inferenz {total_rows} Zeilen (Stapel) : {fmt_ms(med_pred)}  "
              f"(min {fmt_ms(lo_p)} / max {fmt_ms(hi_p)})")
        print(f"  Je Spielerzeile im Stapel      : {fmt_ms(med_pred / total_rows)}")

        # Realistischer Betriebsfall: eine Runde endet, eine Datei wird
        # ausgewertet. Der Aufruf-Overhead von predict_proba faellt dann je
        # Session einzeln an und nicht einmal fuer 316 Zeilen zusammen.
        session_blocks = []
        pos = 0
        for f in files:
            n = len(pd.read_csv(f))
            session_blocks.append(X[pos:pos + n])
            pos += n

        def predict_per_session():
            for blk in session_blocks:
                if len(blk):
                    model.predict_proba(blk)

        med_sess, _, _ = timeit(predict_per_session, args.repeats)
        print(f"  Je Session einzeln (Ø {total_rows/len(files):.1f} Zeilen) : "
              f"{fmt_ms(med_sess / len(files))}")

        # ── Parallelisierung ──────────────────────────────────────────
        # Das Modell wird mit n_jobs=-1 trainiert und gespeichert, nutzt bei
        # der Vorhersage also alle Kerne. Fuer den Betriebsfall ist das die
        # falsche Einstellung: eine Runde liefert nur eine Handvoll Zeilen,
        # und das Verteilen der Arbeit auf viele Threads kostet dann mehr als
        # die Rechnung selbst. Beide Varianten werden gemessen, damit die
        # Empfehlung belegt ist.
        clf = model.named_steps["clf"]
        n_jobs_orig = clf.n_jobs

        clf.n_jobs = 1
        med_single, _, _ = timeit(predict_per_session, args.repeats)
        print(f"\n  Je Session mit n_jobs=1 : {fmt_ms(med_single / len(files))}"
              f"   (Faktor {med_sess/med_single:.1f} schneller)")

        # Gegenprobe: bei grossen Stapeln lohnt Parallelisierung sehr wohl.
        big = np.repeat(X, max(1, 60000 // max(1, total_rows)), axis=0)
        times = {}
        for nj in (1, -1):
            clf.n_jobs = nj
            times[nj], _, _ = timeit(lambda: model.predict_proba(big), 3)
        print(f"  Stapel mit {len(big)} Zeilen : "
              f"n_jobs=1 {fmt_ms(times[1])}, n_jobs=-1 {fmt_ms(times[-1])}")
        print(f"  -> Parallelisierung lohnt erst ab grossen Stapeln, nicht im "
              f"rundenweisen Betrieb.")

        clf.n_jobs = n_jobs_orig
        results["ml_inferenz_1thread_je_session_ms"] = round(med_single / len(files) * 1000, 3)
        results["ml_stapel_zeilen"]      = int(len(big))
        results["ml_stapel_1thread_ms"]  = round(times[1] * 1000, 1)
        results["ml_stapel_parallel_ms"] = round(times[-1] * 1000, 1)

        results["ml_laden_ms"]        = round(med_load * 1000, 2)
        results["ml_inferenz_je_zeile_ms"] = round(med_pred / total_rows * 1000, 4)
        results["ml_inferenz_je_session_ms"] = round(med_sess / len(files) * 1000, 3)

        # Speicher
        tracemalloc.start()
        model.predict_proba(X)
        _, peak = tracemalloc.get_traced_memory()
        tracemalloc.stop()
        print(f"  Speicher (Spitze)       : {peak / 1024 / 1024:.2f} MB")
        results["ml_speicher_mb"] = round(peak / 1024 / 1024, 2)

        size = os.path.getsize(model_path) / 1024
        print(f"  Modellgroesse           : {size:.1f} KB")
        results["ml_modellgroesse_kb"] = round(size, 1)

        # Training als einmalige Kosten
        train_csv = os.path.join(SCRIPT_DIR, "training_data_real.csv")
        if os.path.exists(train_csv):
            from sklearn.ensemble import RandomForestClassifier
            from sklearn.preprocessing import StandardScaler
            from sklearn.pipeline import Pipeline
            dft = add_derived_features(pd.read_csv(train_csv))
            Xt, yt = dft[feat].values, dft["Label"].values

            def train_once():
                Pipeline([("scaler", StandardScaler()),
                          ("clf", RandomForestClassifier(
                              n_estimators=200, class_weight="balanced",
                              min_samples_leaf=2, random_state=42, n_jobs=-1))]).fit(Xt, yt)

            med_t, _, _ = timeit(train_once, max(3, args.repeats // 2))
            print(f"\n  Training (einmalig, {len(dft)} Zeilen) : {med_t:.2f} s")
            print(f"  GPU erforderlich        : nein")
            results["ml_training_s"] = round(med_t, 2)

    # ── Einordnung ───────────────────────────────────────────────────
    print("\n" + "=" * 68)
    print("  EINORDNUNG")
    print("=" * 68)

    if "ml_inferenz_je_session_ms" in results:
        n_avg = total_rows / len(files)
        # Fairer Vergleich: beide Verfahren OHNE das Einlesen der CSV. Das
        # Einlesen faellt in beiden Faellen identisch an und wuerde den
        # Vergleich sonst verzerren — bei den Regeln dominiert es sogar.
        r_logic = results["regeln_logik_je_zeile_ms"] * n_avg
        m_sess  = results["ml_inferenz_je_session_ms"]
        m_1t    = results.get("ml_inferenz_1thread_je_session_ms", m_sess)
        io_sess = results["regeln_je_session_ms"] - r_logic

        print(f"\n  Auswertung einer Runde (Ø {n_avg:.1f} Spieler), "
              f"jeweils ohne CSV-Einlesen:")
        print(f"    Regellogik           : {r_logic:8.3f} ms")
        print(f"    ML-Inferenz (n_jobs=1): {m_1t:8.3f} ms   "
              f"(Faktor {m_1t/r_logic:.1f} gegenueber den Regeln)")
        print(f"    ML-Inferenz (alle Kerne): {m_sess:6.3f} ms   "
              f"(Fehlkonfiguration, siehe oben)")
        print(f"    CSV-Einlesen         : {io_sess:8.3f} ms   "
              f"(faellt bei beiden an)")
        m_sess = m_1t   # fuer die Prozentangabe die sinnvolle Konfiguration

        rundendauer = 300.0   # Rundenlaenge laut Spielkonfiguration
        print(f"\n  Bezogen auf die Rundenlaenge von {rundendauer:.0f} s "
              f"beansprucht die Auswertung")
        print(f"  einen Anteil von {r_logic/1000/rundendauer*100:.6f} % "
              f"(Regeln) bzw. {m_sess/1000/rundendauer*100:.6f} % (ML).")
        print(f"\n  Beide Verfahren sind damit fuer den Betrieb auf einem "
              f"Standard-Server\n  ohne GPU unkritisch. Der relative Unterschied "
              f"(Faktor {m_sess/r_logic:.1f}) faellt\n  praktisch nicht ins "
              f"Gewicht, weil beide Werte um Groessenordnungen\n  unter der "
              f"Rundendauer liegen. Der Kostenvorteil regelbasierter Ansaetze\n"
              f"  liegt nicht in der Inferenz, sondern im entfallenden Training "
              f"und in\n  der fehlenden Datenanforderung.")

    print("\n" + "-" * 68)
    print("  NICHT VON HIER MESSBAR: Overhead im laufenden Spiel")
    print("-" * 68)
    print("""
  Der UTelemetryCollector tastet mit 10 Hz je Pawn ab. Dieser Aufwand faellt
  auf dem Spielserver an und laesst sich nur dort messen. Vorgehen:

    1. Server starten, Runde mit der ueblichen Spielerzahl laufen lassen.
    2. In der Server-Konsole:   stat unit
       -> zeigt Frame-, Game- und Draw-Zeit in ms je Frame.
    3. Dieselbe Messung mit deaktiviertem Collector wiederholen
       (PrimaryComponentTick.bCanEverTick = false oder Komponente entfernen).
    4. Differenz der 'Game'-Zeit ist der Overhead der Telemetrie.

  Alternativ ueber laengere Zeit:   stat startfile / stat stopfile
  erzeugt ein Profil in Saved/Profiling, auswertbar mit UnrealInsights.

  Fuer die Arbeit genuegt die Differenz der durchschnittlichen Game-Zeit
  in ms mit Angabe der Spielerzahl.
""")

    os.makedirs(args.outdir, exist_ok=True)
    out_path = os.path.join(args.outdir, "rechenaufwand.json")
    results["_system"]   = platform.processor() or platform.machine()
    results["_sessions"] = len(files)
    results["_zeilen"]   = total_rows
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    print(f"[Gespeichert] {out_path}")


if __name__ == "__main__":
    main()
