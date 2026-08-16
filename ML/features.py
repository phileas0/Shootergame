"""
Gemeinsame Feature-Definition für Training und Inferenz.

Zweck: Training (train_model.py) und Vorhersage (predict.py) müssen exakt
dieselben Spalten in exakt derselben Reihenfolge verwenden. Lagen diese
Definitionen wie zuvor als Kopien in jedem Skript, konnten sie auseinander
laufen und es entstand ein Train/Serve-Skew, der nicht auffällt, sondern
still falsche Vorhersagen liefert.
"""

# Spalten, die keine Features sind: Identität und Zielvariable.
# Label darf niemals ins Feature-Set — es IST die Antwort. Ein Modell, das
# Label als Eingabe sieht, erreicht ~100 % Trainingsgenauigkeit und ist
# im Betrieb wertlos, weil die Spalte dort nicht existiert.
#
# CheatType, SessionFile und Person kommen aus load_labeled_data.py und
# gelten aus demselben Grund als Metadaten:
#   CheatType   ist direktes Label-Leakage (nur gesetzt, wenn Label=1) und
#               existiert zur Inferenzzeit gar nicht. Er dient allein der
#               Auswertung: Recall je Cheat-Art nach dem Training.
#   Person      dient als Gruppierungsschlüssel für den Train/Test-Split,
#               nicht als Merkmal — sonst lernt das Modell, wer cheatet,
#               statt woran man Cheaten erkennt.
#   SessionFile ist die Herkunft der Zeile, reine Nachvollziehbarkeit.
META_COLS = ["PlayerID", "Label", "SessionFile", "CheatType", "Person"]

# Features, die in echten UE5-Daten konstant 0 sind und daher keine
# Information tragen.
#
# Historie: AimAngularErrorMean und AimAngularErrorStdDev standen hier,
# solange die Messung im TelemetryCollector nicht implementiert war.
# Seit der Mehrpunkt-Messung (11.08.2026) liefern sie Werte und trennen
# Aimbot (3,59 Grad) deutlich von sauberem Spiel (17,16 Grad) — sie sind
# deshalb entfernt worden und werden jetzt als Features genutzt.
#
# HeadshotRate bleibt ausgeschlossen: RecordHit(false) ist im Collector
# fest verdrahtet, die Kopftreffer-Erkennung fehlt noch.
ZERO_FEATURES = ["HeadshotRate"]


def add_derived_features(df):
    """
    Ergänzt abgeleitete Features. Wird von Training UND Inferenz aufgerufen,
    damit beide Seiten identisch rechnen.

    OnTargetRatio: Anteil der Schüsse, die überhaupt ein sichtbares Ziel im
    Kegel hatten. Der Rohwert AimErrorSampleCount waechst mit der Rundenlaenge
    und ist zwischen unterschiedlich langen Sitzungen nicht vergleichbar;
    die Quote ist es. Messung: Aimbot 0,66 gegen sauberes Spiel 0,20.
    """
    df = df.copy()

    if "AimErrorSampleCount" in df.columns and "TotalShots" in df.columns:
        shots = df["TotalShots"].astype(float)
        # Ohne Schuss gibt es keine Quote — 0 statt Division durch null.
        df["OnTargetRatio"] = (df["AimErrorSampleCount"] / shots).where(shots > 0, 0.0)

    return df


def get_feature_cols(df):
    """
    Liefert die Feature-Spalten in stabiler, alphabetischer Reihenfolge.

    Die Sortierung ist wichtig: sklearn-Modelle arbeiten positionsbasiert.
    Käme die Spaltenreihenfolge aus der CSV, würde eine Änderung am
    CSV-Header im TelemetryLogger die Features stillschweigend vertauschen.
    """
    exclude = set(META_COLS) | set(ZERO_FEATURES)
    return sorted(c for c in df.columns if c not in exclude)


def check_feature_match(expected, actual):
    """
    Vergleicht die beim Training gespeicherten Features mit denen der
    aktuellen Daten und liefert eine erklärende Fehlermeldung oder None.

    Ohne diese Prüfung äußert sich ein Schema-Unterschied (z. B. eine neue
    Spalte im CSV-Export) als unverständlicher sklearn-Dimensionsfehler
    oder — schlimmer — als stille Fehlzuordnung der Werte.
    """
    if list(expected) == list(actual):
        return None

    missing = [c for c in expected if c not in actual]
    extra   = [c for c in actual if c not in expected]

    lines = ["Feature-Schema passt nicht zum trainierten Modell."]
    if missing:
        lines.append(f"  Fehlend in den Daten : {missing}")
    if extra:
        lines.append(f"  Zusaetzlich in Daten : {extra}")
    if not missing and not extra:
        lines.append("  Gleiche Spalten, andere Reihenfolge.")
    lines.append("  -> Modell mit train_model.py auf dem aktuellen Schema neu trainieren.")
    return "\n".join(lines)
