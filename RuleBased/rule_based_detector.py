import os
import sys
import glob
import pandas as pd

# ─────────────────────────────────────────────
# Konfiguration
# ─────────────────────────────────────────────
DEFAULT_TELEMETRY_DIR = r"C:\Users\phile\OneDrive\Desktop\Telemetry"

# Echte Grenzwerte basierend auf der Spieldaten-Analyse
LIMIT_RAPID_FIRE_HIGH = 20.0       # > 20 Schüsse/Sek (extrem auffällig)
LIMIT_RAPID_FIRE      = 12.0       # > 12 Schüsse/Sek (menschliches Limit bei Semi-Auto)
LIMIT_REACTION_MEAN   = 0.08       # < 80ms Reaktion (menschliches Limit ca. 100ms)
LIMIT_REACTION_STD    = 0.015      # < 15ms Reaktions-Schwankung (Bot-Konsistenz)
LIMIT_INTERVAL_STD    = 0.015      # < 15ms Schussabstands-Schwankung bei hoher Kadenz
LIMIT_AIM_FLIP_RATIO  = 0.011      # > 1.1% Drehsprünge > 90° (Aimbot Snapping)

# --- Aimbot-Präzision (Winkelfehler zum Ziel im Schussmoment) ---
#
# ACHTUNG (16.08.2026): Diese Werte stammen aus je EINER Messreihe pro Klasse
# (sauberes Spiel 17.16° / Aimbot 3.59°) und haben sich auf 296 real erhobenen
# Zeilen als NICHT REPRÄSENTATIV erwiesen:
#
#     tatsächlich   sauber 9.47° / Cheat 8.19°      -> trennt nicht
#                   On-Target sauber 73.4 % / Cheat 73.3 %
#
# Die Schwelle ON_TARGET_RATIO von 45 % liegt damit weit unter dem Normalwert
# und markiert 264 von 268 sauberen Spielern. Die Werte bleiben hier als
# Standard stehen, damit ältere Auswertungen reproduzierbar bleiben —
# datenbasiert kalibrierte Grenzwerte liefert ML/calibrate_rules.py und
# übergibt sie über den Parameter `limits`.
LIMIT_AIM_ERROR_HIGH   = 2.5       # < 2.5° mittlerer Zielfehler (praktisch perfekte Rastung)
LIMIT_AIM_ERROR        = 6.0       # < 6.0° mittlerer Zielfehler (deutlich unter menschlichem Niveau)
LIMIT_ON_TARGET_RATIO  = 0.45      # > 45% der Schüsse haben ein Ziel im Kegel (Mensch: ~20%)
MIN_AIM_ERROR_SAMPLES  = 20        # Mindeststichproben, sonst ist der Mittelwert nicht belastbar
MIN_SHOTS_FOR_RATIO    = 30        # Mindestschüsse für die On-Target-Quote
LIMIT_SPEED_MAX       = 1500.0     # > 1500 cm/s (Physik-Limit im Spiel, inkl. Stürze)
LIMIT_SPEED_VIOLATION = 0.35       # > 35% der Zeit über 600 cm/s
LIMIT_GOD_KILLS       = 20         # >= 20 Kills (um Fehlalarme bei guten Spielern zu vermeiden)
LIMIT_GOD_DEATHS      = 0          # 0 Tode (KDR-Check)

# Alle Grenzwerte gebündelt, damit sie sich von außen überschreiben lassen.
# Gebraucht wird das für die datenbasierte Kalibrierung (ML/calibrate_rules.py):
# dort werden die Schwellen je Trainingsfalte neu bestimmt und der Detektor
# anschließend auf der Testfalte ausgewertet. Ohne diese Möglichkeit ließe sich
# der Regeldetektor nicht unter denselben Bedingungen bewerten wie das
# ML-Modell, und der Vergleich beider Verfahren wäre nicht aussagekräftig.
DEFAULT_LIMITS = {
    "RAPID_FIRE_HIGH":       LIMIT_RAPID_FIRE_HIGH,
    "RAPID_FIRE":            LIMIT_RAPID_FIRE,
    "REACTION_MEAN":         LIMIT_REACTION_MEAN,
    "REACTION_STD":          LIMIT_REACTION_STD,
    "INTERVAL_STD":          LIMIT_INTERVAL_STD,
    "AIM_FLIP_RATIO":        LIMIT_AIM_FLIP_RATIO,
    "AIM_ERROR_HIGH":        LIMIT_AIM_ERROR_HIGH,
    "AIM_ERROR":             LIMIT_AIM_ERROR,
    "ON_TARGET_RATIO":       LIMIT_ON_TARGET_RATIO,
    "SPEED_MAX":             LIMIT_SPEED_MAX,
    "SPEED_VIOLATION":       LIMIT_SPEED_VIOLATION,
    "GOD_KILLS":             LIMIT_GOD_KILLS,
    "GOD_DEATHS":            LIMIT_GOD_DEATHS,
    "MIN_AIM_ERROR_SAMPLES": MIN_AIM_ERROR_SAMPLES,
    "MIN_SHOTS_FOR_RATIO":   MIN_SHOTS_FOR_RATIO,
}


def analyze_player(row, limits=None):
    """
    Bewertet einen Spieler anhand von festen Regeln.
    Gibt einen Risk-Score (0-100%) und eine Liste verletzter Regeln zurück.

    limits: optionales Dict mit einzelnen Grenzwerten (Schlüssel wie in
            DEFAULT_LIMITS). Nicht angegebene Werte behalten den Standard.
            Ohne Angabe verhält sich die Funktion exakt wie zuvor.
    """
    L = DEFAULT_LIMITS if limits is None else {**DEFAULT_LIMITS, **limits}

    player_id = row.get("PlayerID", "Unbekannt")
    score = 0
    flags = []

    # 1. Schussfrequenz (Rapid-Fire basierend auf aktivem Schussintervall)
    sim = row.get("ShotIntervalMean", 0.0)
    active_sps = 1.0 / sim if sim > 0.0 else 0.0
    
    if active_sps > L["RAPID_FIRE_HIGH"]:
        score += 50
        flags.append(f"INSANE_RAPID_FIRE ({active_sps:.1f} active shots/s)")
    elif active_sps > L["RAPID_FIRE"]:
        score += 40
        flags.append(f"RAPID_FIRE ({active_sps:.1f} active shots/s)")

    # 2. Reaktionszeit (Triggerbot)
    rt_mean = row.get("ReactionTimeMean", 99.0)
    rt_std = row.get("ReactionTimeStdDev", 99.0)
    # Nur prüfen, wenn überhaupt geschossen wurde und ein Reaktions-Event stattfand (> 0)
    if 0.0 < rt_mean < L["REACTION_MEAN"] and rt_std < L["REACTION_STD"]:
        score += 40
        flags.append(f"TRIGGERBOT_REACTION ({rt_mean*1000:.1f}ms mean, {rt_std*1000:.1f}ms std)")

    # 3. Schussabstands-Abweichung (Makro / Autoclicker)
    interval_std = row.get("ShotIntervalStdDev", 99.0)
    if active_sps > 5.0 and interval_std < L["INTERVAL_STD"]:
        score += 30
        flags.append(f"MECHANICAL_AUTOFIRE (std: {interval_std*1000:.1f}ms)")

    # 4. Aim-Snapping (Aimbot-Schnittstelle)
    flip_ratio = row.get("AimFlipRatio", 0.0)
    if flip_ratio > L["AIM_FLIP_RATIO"]:
        score += 25
        flags.append(f"AIM_SNAPPING ({flip_ratio*100:.2f}% flips)")

    # 5. Aimbot-Präzision (mittlerer Winkelfehler zum Ziel im Schussmoment)
    # Wichtig: AimAngularErrorMean ist auch dann 0.0, wenn nie auf ein sichtbares
    # Ziel geschossen wurde. Ohne die Stichproben-Schranke würde jeder Spieler,
    # der nur ins Leere feuert, als perfekt zielender Aimbot gewertet.
    aim_err = row.get("AimAngularErrorMean", 0.0)
    aim_samples = int(row.get("AimErrorSampleCount", 0))

    if aim_samples >= L["MIN_AIM_ERROR_SAMPLES"]:
        if aim_err < L["AIM_ERROR_HIGH"]:
            score += 50
            flags.append(f"AIMBOT_LOCK ({aim_err:.2f} deg mean error, n={aim_samples})")
        elif aim_err < L["AIM_ERROR"]:
            score += 35
            flags.append(f"AIMBOT_PRECISION ({aim_err:.2f} deg mean error, n={aim_samples})")

    # 6. On-Target-Quote: Anteil der Schüsse, die überhaupt ein Ziel im Kegel hatten.
    # Unabhängig von der Zielgenauigkeit selbst — ein Aimbot feuert kaum ins Leere,
    # weil er nur schiesst wenn ein Ziel erfasst ist.
    total_shots = int(row.get("TotalShots", 0))
    if total_shots >= L["MIN_SHOTS_FOR_RATIO"]:
        on_target_ratio = aim_samples / total_shots
        if on_target_ratio > L["ON_TARGET_RATIO"]:
            score += 25
            flags.append(
                f"ON_TARGET_RATIO ({on_target_ratio*100:.1f}% der Schuesse auf ein Ziel gerichtet)"
            )

    # 7. Speed-Hack
    speed_max = row.get("MovementSpeedMax", 0.0)
    speed_viol = row.get("SpeedViolationRatio", 0.0)
    if speed_max > L["SPEED_MAX"]:
        score += 30
        flags.append(f"SPEED_LIMIT_EXCEEDED ({speed_max:.1f} cm/s)")
    elif speed_viol > L["SPEED_VIOLATION"]:
        score += 25
        flags.append(f"SPEED_VIOLATION_RATIO ({speed_viol*100:.1f}%)")

    # 8. God-Mode (Unsterblichkeit)
    kills = row.get("TotalKills", 0)
    deaths = row.get("TotalDeaths", 0)
    if kills >= L["GOD_KILLS"] and deaths == L["GOD_DEATHS"]:
        score += 60
        flags.append(f"GOD_MODE ({kills} Kills, 0 Deaths)")

    # Score auf 100% deckeln
    final_score = min(100, score)
    return final_score, flags

def analyze_file(csv_path):
    """Analysiert eine CSV-Datei und gibt die Ergebnisse aus."""
    filename = os.path.basename(csv_path)
    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"[FEHLER] Konnte {filename} nicht lesen: {e}")
        return

    # Nur aktive Spieler analysieren (analog zu predict.py)
    if "TotalShots" in df.columns:
        df_active = df[df["TotalShots"] > 0].copy()
    else:
        df_active = df.copy()

    if df_active.empty:
        print(f"--------------------------------------------------")
        print(f"Datei: {filename}")
        print(f"[INFO] Keine aktiven Spieler in dieser Session (0 Schüsse).")
        return

    print(f"--------------------------------------------------")
    print(f"DATEI: {filename}")
    print(f"--------------------------------------------------")
    print(f"  {'SpielerID':<28} | {'K/D':<6} | {'aSPS':<5} | {'AimErr':<9} | {'Risk':<5} | {'Urteil':<10}")
    print(f"  {'-'*28}-+-{'-'*6}-+-{'-'*5}-+-{'-'*9}-+-{'-'*5}-+-{'-'*10}")

    cheaters = []
    
    for _, row in df_active.iterrows():
        player = row.get("PlayerID", "Unbekannt")
        kills = int(row.get("TotalKills", 0))
        deaths = int(row.get("TotalDeaths", 0))
        sim = row.get("ShotIntervalMean", 0.0)
        asps = 1.0 / sim if sim > 0.0 else 0.0
        
        risk, flags = analyze_player(row)
        
        if risk >= 50:
            verdict = "! CHEATER"
            cheaters.append((player, risk, flags))
        elif risk >= 25:
            verdict = "? SUSPICIOUS"
        else:
            verdict = "  CLEAN"
            
        kd_str = f"{kills}/{deaths}"
        # Zielfehler nur anzeigen wenn genug Stichproben vorliegen, sonst "-"
        n_samples = int(row.get("AimErrorSampleCount", 0))
        if n_samples >= MIN_AIM_ERROR_SAMPLES:
            aim_str = f"{row.get('AimAngularErrorMean', 0.0):.2f} deg"
        else:
            aim_str = f"- (n={n_samples})"
        print(f"  {player:<28} | {kd_str:<6} | {asps:<5.1f} | {aim_str:<9} | {risk:>3}% | {verdict:<10}")
        
        if flags:
            for flag in flags:
                print(f"     -> [VERLETZUNG] {flag}")
                
    if cheaters:
        print(f"\n  -> ERKANNT: {', '.join([c[0] for c in cheaters])}")
    else:
        print(f"\n  -> Keine Cheater erkannt.")
    print(f"--------------------------------------------------\n")

def main():
    # Prüfen ob Argumente übergeben wurden
    if len(sys.argv) > 1:
        target = sys.argv[1]
        if os.path.isdir(target):
            files = sorted(glob.glob(os.path.join(target, "*.csv")))
        else:
            files = [target]
    else:
        # Standardordner nutzen
        if not os.path.exists(DEFAULT_TELEMETRY_DIR):
            print(f"[FEHLER] Standard-Ordner existiert nicht: {DEFAULT_TELEMETRY_DIR}")
            print(f"Bitte gib eine Datei oder einen Ordner als Parameter an.")
            sys.exit(1)
        files = sorted(glob.glob(os.path.join(DEFAULT_TELEMETRY_DIR, "*.csv")))

    print("=" * 60)
    print("  RULE-BASED CHEAT DETECTOR (Regelbasierte Erkennung)")
    print("=" * 60)
    print(f"  Gefundene CSV-Dateien: {len(files)}")
    print(f"  Grenzwerte: aSPS > {LIMIT_RAPID_FIRE} | RT < {LIMIT_REACTION_MEAN*1000}ms | God KDR ({LIMIT_GOD_KILLS}k/0d)")
    print(f"              AimErr < {LIMIT_AIM_ERROR} deg (n>={MIN_AIM_ERROR_SAMPLES}) | On-Target > {LIMIT_ON_TARGET_RATIO*100:.0f}%")
    print("=" * 60 + "\n")

    for f in files:
        analyze_file(f)

if __name__ == "__main__":
    main()
