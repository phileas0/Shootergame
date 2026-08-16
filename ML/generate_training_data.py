"""
generate_training_data.py
=========================
Schritt 2 der Bachelorarbeit: Synthetische Trainingsdaten generieren.

Erzeugt einen CSV-Datensatz mit simulierten Spieler-Sessions:
  - ~950 legitime Spieler (Label=0), aufgeteilt in:
      * Casual-Spieler    (~600): mittelmäßige Werte, viel Varianz
      * Erfahrene Spieler (~250): gut, aber klar menschlich
      * E-Sportler        (~100): Ausreißer — können Cheater-Werten ähneln
  - ~50 Cheater (Label=1), aufgeteilt in 3 Profile:
      * Aimbot       (~20): extrem stabile Aim, kurze Reaktionszeit
      * Speedhack    (~15): SpeedViolationRatio hoch, Speed weit über Limit
      * Triggerbot   (~15): maschinell konstante ShotIntervals + Reaktionszeit

WICHTIG: Bewusste Überlappung zwischen E-Sportlern und Cheater-Profilen,
weil sehr gute Spieler in einzelnen Features ähnliche Werte erreichen können.
→ Erzeugt realistische Grauzone für τ*-Analyse (Kap. 9 Bachelorarbeit).

Ausgabe: training_data.csv (im selben Ordner)
"""

import numpy as np
# Windows-Konsole nutzt standardmaessig cp1252 und bricht bei Zeichen wie
# tau, lambda oder Pfeilen mit UnicodeEncodeError ab. UTF-8 erzwingen,
# damit die mathematische Notation in der Ausgabe erhalten bleibt.
import sys
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
import pandas as pd

SEED = 42
rng = np.random.default_rng(SEED)

N_CASUAL    = 3000
N_SKILLED   = 1250
N_ESPORT    =  500
N_AIMBOT    =  100
N_SPEED     =   75
N_TRIGGER   =   75
N_TOTAL     = N_CASUAL + N_SKILLED + N_ESPORT + N_AIMBOT + N_SPEED + N_TRIGGER


def clip(arr, lo, hi):
    return np.clip(arr, lo, hi)


# ──────────────────────────────────────────────
# 1a. Legitim – Casual-Spieler
#     Breite Streuung, schlechte bis mittelmäßige Stats
# ──────────────────────────────────────────────
def gen_casual(n):
    rows = {}
    rows["PlayerID"]               = [f"casual_{i:04d}" for i in range(n)]
    rows["SessionDurationSeconds"] = rng.uniform(30, 300, n)

    rows["AimAngularSpeedMean"]    = clip(rng.normal(70,  35,  n), 10, 250)
    rows["AimAngularSpeedStdDev"]  = clip(rng.normal(100, 50,  n), 20, 450)
    rows["AimAngularErrorMean"]    = clip(rng.normal(18,  9,   n),  2,  70)
    rows["AimAngularErrorStdDev"]  = clip(rng.normal(12,  6,   n),  1,  50)
    rows["AimFlipRatio"]           = clip(rng.normal(0.015, 0.012, n), 0, 0.12)

    rows["MovementSpeedMean"]          = clip(rng.normal(380, 90,  n), 50, 600)
    rows["MovementSpeedMax"]           = clip(rng.normal(560, 50,  n), 150, 620)
    rows["DirectionChangesPerSecond"]  = clip(rng.normal(0.35, 0.18, n), 0.02, 1.8)
    rows["SpeedViolationRatio"]        = clip(rng.exponential(0.008, n), 0, 0.04)
    rows["MovementPathEntropy"]        = clip(rng.normal(2.3, 0.5, n), 0.4, 3.5)

    # Reaktionszeit: Casual 300–900ms
    rows["ReactionTimeMean"]   = clip(rng.normal(0.55, 0.18, n), 0.20, 1.20)
    rows["ReactionTimeStdDev"] = clip(rng.normal(0.15, 0.06, n), 0.04, 0.55)
    rows["ShotIntervalMean"]   = clip(rng.normal(0.65, 0.25, n), 0.15, 2.5)
    rows["ShotIntervalStdDev"] = clip(rng.normal(0.22, 0.10, n), 0.03, 0.9)
    rows["ShotsPerSecond"]     = clip(rng.normal(0.9,  0.4,  n), 0.1,  3.5)

    rows["HitRate"]        = clip(rng.normal(0.20, 0.10, n), 0.02, 0.50)
    rows["HeadshotRate"]   = np.zeros(n)
    rows["KillsPerMinute"] = clip(rng.normal(1.0,  0.6,  n), 0.0,  4.0)
    rows["KillDeathRatio"] = clip(rng.normal(0.8,  0.45, n), 0.05, 3.5)

    total_shots = rng.integers(10, 180, n)
    # Anteil Schuesse mit Ziel im Kegel (gemessen: Mensch ~0.20, Aimbot ~0.66-0.80)
    on_target = clip(rng.normal(0.2, 0.06, n), 0.02, 0.95)
    rows["AimErrorSampleCount"] = (total_shots * on_target).astype(int)
    rows["TotalShots"]  = total_shots
    rows["TotalHits"]   = (total_shots * rows["HitRate"]).astype(int)
    rows["TotalKills"]  = rng.integers(0, 12, n)
    rows["TotalDeaths"] = rng.integers(1, 18, n)
    rows["Label"]       = np.zeros(n, dtype=int)
    return pd.DataFrame(rows)


# ──────────────────────────────────────────────
# 1b. Legitim – Erfahrene Spieler
#     Gut, aber klar menschlich — moderate Überlappung mit Cheatern
# ──────────────────────────────────────────────
def gen_skilled(n):
    rows = {}
    rows["PlayerID"]               = [f"skilled_{i:04d}" for i in range(n)]
    rows["SessionDurationSeconds"] = rng.uniform(30, 280, n)

    rows["AimAngularSpeedMean"]    = clip(rng.normal(95,  25,  n), 30, 250)
    rows["AimAngularSpeedStdDev"]  = clip(rng.normal(80,  30,  n), 15, 300)
    rows["AimAngularErrorMean"]    = clip(rng.normal(10,  5,   n),  1,  40)
    rows["AimAngularErrorStdDev"]  = clip(rng.normal(7,   3,   n),  1,  25)
    rows["AimFlipRatio"]           = clip(rng.normal(0.02, 0.012, n), 0, 0.12)

    rows["MovementSpeedMean"]          = clip(rng.normal(430, 70,  n), 100, 610)
    rows["MovementSpeedMax"]           = clip(rng.normal(590, 30,  n), 300, 620)
    rows["DirectionChangesPerSecond"]  = clip(rng.normal(0.42, 0.15, n), 0.05, 1.6)
    rows["SpeedViolationRatio"]        = clip(rng.exponential(0.010, n), 0, 0.05)
    rows["MovementPathEntropy"]        = clip(rng.normal(2.55, 0.38, n), 0.8, 3.5)

    # Reaktionszeit: Erfahren 180–550ms
    rows["ReactionTimeMean"]   = clip(rng.normal(0.32, 0.10, n), 0.12, 0.70)
    rows["ReactionTimeStdDev"] = clip(rng.normal(0.10, 0.04, n), 0.03, 0.35)
    rows["ShotIntervalMean"]   = clip(rng.normal(0.50, 0.18, n), 0.10, 1.8)
    rows["ShotIntervalStdDev"] = clip(rng.normal(0.16, 0.07, n), 0.02, 0.7)
    rows["ShotsPerSecond"]     = clip(rng.normal(1.3,  0.4,  n), 0.2,  4.5)

    rows["HitRate"]        = clip(rng.normal(0.35, 0.10, n), 0.10, 0.65)
    rows["HeadshotRate"]   = np.zeros(n)
    rows["KillsPerMinute"] = clip(rng.normal(2.2,  0.8,  n), 0.2,  6.0)
    rows["KillDeathRatio"] = clip(rng.normal(1.6,  0.7,  n), 0.1,  5.5)

    total_shots = rng.integers(20, 200, n)
    # Anteil Schuesse mit Ziel im Kegel (gemessen: Mensch ~0.20, Aimbot ~0.66-0.80)
    on_target = clip(rng.normal(0.28, 0.07, n), 0.02, 0.95)
    rows["AimErrorSampleCount"] = (total_shots * on_target).astype(int)
    rows["TotalShots"]  = total_shots
    rows["TotalHits"]   = (total_shots * rows["HitRate"]).astype(int)
    rows["TotalKills"]  = rng.integers(2, 20, n)
    rows["TotalDeaths"] = rng.integers(0, 14, n)
    rows["Label"]       = np.zeros(n, dtype=int)
    return pd.DataFrame(rows)


# ──────────────────────────────────────────────
# 1c. Legitim – E-Sportler / Pro-Spieler
#     Ausreißer: sehr gut, manche Werte überlappen mit Cheater-Bereich.
#     Entscheidend für Grauzone: ReactionTime ~100–200ms, HitRate bis 0.75,
#     KillDeathRatio bis 8, aber mit menschlicher Varianz (StdDev bleibt hoch).
# ──────────────────────────────────────────────
def gen_esport(n):
    rows = {}
    rows["PlayerID"]               = [f"esport_{i:04d}" for i in range(n)]
    rows["SessionDurationSeconds"] = rng.uniform(60, 300, n)

    rows["AimAngularSpeedMean"]    = clip(rng.normal(130, 30,  n), 50, 280)
    rows["AimAngularSpeedStdDev"]  = clip(rng.normal(55,  20,  n), 15, 180)  # höher als Aimbot
    rows["AimAngularErrorMean"]    = clip(rng.normal(5,   3,   n),  0.5, 20)
    rows["AimAngularErrorStdDev"]  = clip(rng.normal(4,   2,   n),  0.5, 15)
    rows["AimFlipRatio"]           = clip(rng.normal(0.04, 0.02, n), 0, 0.15)

    rows["MovementSpeedMean"]          = clip(rng.normal(460, 60,  n), 200, 620)
    rows["MovementSpeedMax"]           = clip(rng.normal(605, 20,  n), 540, 625)  # am Limit
    rows["DirectionChangesPerSecond"]  = clip(rng.normal(0.55, 0.18, n), 0.1, 2.0)
    rows["SpeedViolationRatio"]        = clip(rng.exponential(0.012, n), 0, 0.06)
    rows["MovementPathEntropy"]        = clip(rng.normal(2.75, 0.35, n), 1.2, 3.5)

    # Reaktionszeit Pro: 100–250ms — ÜBERLAPPT mit Triggerbot-Bereich
    rows["ReactionTimeMean"]   = clip(rng.normal(0.17, 0.05, n), 0.08, 0.35)
    rows["ReactionTimeStdDev"] = clip(rng.normal(0.06, 0.02, n), 0.02, 0.18)  # menschlich: bleibt >0.02
    rows["ShotIntervalMean"]   = clip(rng.normal(0.42, 0.12, n), 0.10, 1.2)
    rows["ShotIntervalStdDev"] = clip(rng.normal(0.12, 0.05, n), 0.03, 0.40)  # menschlich: bleibt >0.03
    rows["ShotsPerSecond"]     = clip(rng.normal(1.9,  0.5,  n), 0.5,  5.5)

    # HitRate bis 0.75 — ÜBERLAPPT mit Aimbot (min 0.55)
    rows["HitRate"]        = clip(rng.normal(0.55, 0.12, n), 0.30, 0.78)
    rows["HeadshotRate"]   = np.zeros(n)
    # KDR bis ~8 — ÜBERLAPPT mit Aimbot (min 3.0)
    rows["KillsPerMinute"] = clip(rng.normal(4.0,  1.2,  n), 1.5, 8.0)
    rows["KillDeathRatio"] = clip(rng.normal(3.5,  1.5,  n), 0.8, 8.5)

    total_shots = rng.integers(30, 200, n)
    # Anteil Schuesse mit Ziel im Kegel (gemessen: Mensch ~0.20, Aimbot ~0.66-0.80)
    on_target = clip(rng.normal(0.35, 0.08, n), 0.02, 0.95)
    rows["AimErrorSampleCount"] = (total_shots * on_target).astype(int)
    rows["TotalShots"]  = total_shots
    rows["TotalHits"]   = (total_shots * rows["HitRate"]).astype(int)
    rows["TotalKills"]  = rng.integers(5, 35, n)
    rows["TotalDeaths"] = rng.integers(0, 10, n)
    rows["Label"]       = np.zeros(n, dtype=int)
    return pd.DataFrame(rows)


# ──────────────────────────────────────────────
# 2. Cheater Profil A: Aimbot
#    Unnatürlich stabile Aim, kurze Reaktion — aber abgeschwächt
#    damit Überlappung mit E-Sportlern bleibt
# ──────────────────────────────────────────────
def gen_aimbot(n):
    rows = {}
    rows["PlayerID"]               = [f"aimbot_{i:04d}" for i in range(n)]
    rows["SessionDurationSeconds"] = rng.uniform(30, 200, n)

    rows["AimAngularSpeedMean"]    = clip(rng.normal(145, 25,  n), 70, 260)
    # AimAngularSpeedStdDev: Aimbot hat niedrigere StdDev — aber nicht unmöglich für E-Sportler
    rows["AimAngularSpeedStdDev"]  = clip(rng.normal(14,  6,   n),  3, 40)
    rows["AimAngularErrorMean"]    = clip(rng.normal(2.0, 1.2, n),  0,  8)
    rows["AimAngularErrorStdDev"]  = clip(rng.normal(1.0, 0.6, n),  0,  5)
    rows["AimFlipRatio"]           = clip(rng.normal(0.07, 0.03, n), 0, 0.20)

    rows["MovementSpeedMean"]          = clip(rng.normal(420, 75,  n), 100, 610)
    rows["MovementSpeedMax"]           = clip(rng.normal(580, 35,  n), 300, 625)
    rows["DirectionChangesPerSecond"]  = clip(rng.normal(0.40, 0.14, n), 0.05, 1.5)
    rows["SpeedViolationRatio"]        = clip(rng.exponential(0.009, n), 0, 0.04)
    rows["MovementPathEntropy"]        = clip(rng.normal(2.45, 0.40, n), 0.8, 3.5)

    # Reaktionszeit Aimbot: 20–130ms — überlappt mit bestem E-Sportler-Bereich
    rows["ReactionTimeMean"]   = clip(rng.normal(0.06, 0.03, n), 0.015, 0.18)
    rows["ReactionTimeStdDev"] = clip(rng.normal(0.010, 0.005, n), 0.002, 0.03)  # maschinell
    rows["ShotIntervalMean"]   = clip(rng.normal(0.48, 0.14, n), 0.1,  1.5)
    rows["ShotIntervalStdDev"] = clip(rng.normal(0.06, 0.025, n), 0.01, 0.18)
    rows["ShotsPerSecond"]     = clip(rng.normal(1.9,  0.5,  n), 0.5,  5.5)

    # HitRate: 0.55–0.95 — unterer Bereich überlappt mit E-Sportlern
    rows["HitRate"]        = clip(rng.normal(0.80, 0.08, n), 0.55, 0.98)
    rows["HeadshotRate"]   = np.zeros(n)
    rows["KillsPerMinute"] = clip(rng.normal(6.0,  1.2,  n), 3.0, 12.0)
    rows["KillDeathRatio"] = clip(rng.normal(7.0,  2.5,  n), 2.5, 18.0)

    total_shots = rng.integers(30, 160, n)
    # Anteil Schuesse mit Ziel im Kegel (gemessen: Mensch ~0.20, Aimbot ~0.66-0.80)
    on_target = clip(rng.normal(0.7, 0.09, n), 0.02, 0.95)
    rows["AimErrorSampleCount"] = (total_shots * on_target).astype(int)
    rows["TotalShots"]  = total_shots
    rows["TotalHits"]   = (total_shots * rows["HitRate"]).astype(int)
    rows["TotalKills"]  = rng.integers(10, 40, n)
    rows["TotalDeaths"] = rng.integers(0,  6,  n)
    rows["Label"]       = np.ones(n, dtype=int)
    return pd.DataFrame(rows)


# ──────────────────────────────────────────────
# 3. Cheater Profil B: Speedhack
#    SpeedViolationRatio und MovementSpeedMax klar über Limit —
#    aber Aim und Reaktion bleiben unauffällig
# ──────────────────────────────────────────────
def gen_speedhack(n):
    rows = {}
    rows["PlayerID"]               = [f"speed_{i:04d}" for i in range(n)]
    rows["SessionDurationSeconds"] = rng.uniform(20, 200, n)

    rows["AimAngularSpeedMean"]    = clip(rng.normal(88,  32,  n), 15, 260)
    rows["AimAngularSpeedStdDev"]  = clip(rng.normal(92,  40,  n), 18, 380)
    rows["AimAngularErrorMean"]    = clip(rng.normal(13,  7,   n),  1,  55)
    rows["AimAngularErrorStdDev"]  = clip(rng.normal(9,   4,   n),  1,  35)
    rows["AimFlipRatio"]           = clip(rng.normal(0.022, 0.013, n), 0, 0.13)

    # Speed klar außerhalb — aber mit Varianz (nicht alle Frames gleich)
    rows["MovementSpeedMean"]          = clip(rng.normal(820, 150, n), 500, 1600)
    rows["MovementSpeedMax"]           = clip(rng.normal(1300, 250, n), 700, 3000)
    rows["DirectionChangesPerSecond"]  = clip(rng.normal(0.65, 0.22, n), 0.1, 2.2)
    rows["SpeedViolationRatio"]        = clip(rng.normal(0.55, 0.18, n), 0.20, 0.95)
    rows["MovementPathEntropy"]        = clip(rng.normal(2.85, 0.30, n), 1.8, 3.5)

    rows["ReactionTimeMean"]   = clip(rng.normal(0.40, 0.14, n), 0.14, 1.1)
    rows["ReactionTimeStdDev"] = clip(rng.normal(0.11, 0.05, n), 0.03, 0.45)
    rows["ShotIntervalMean"]   = clip(rng.normal(0.57, 0.20, n), 0.1,  2.2)
    rows["ShotIntervalStdDev"] = clip(rng.normal(0.18, 0.08, n), 0.02, 0.75)
    rows["ShotsPerSecond"]     = clip(rng.normal(1.2,  0.4,  n), 0.1,  4.5)

    rows["HitRate"]        = clip(rng.normal(0.32, 0.10, n), 0.05, 0.60)
    rows["HeadshotRate"]   = np.zeros(n)
    rows["KillsPerMinute"] = clip(rng.normal(3.2,  1.0,  n), 0.8,  7.0)
    rows["KillDeathRatio"] = clip(rng.normal(2.8,  1.1,  n), 0.4,  7.0)

    total_shots = rng.integers(15, 180, n)
    # Anteil Schuesse mit Ziel im Kegel (gemessen: Mensch ~0.20, Aimbot ~0.66-0.80)
    on_target = clip(rng.normal(0.2, 0.06, n), 0.02, 0.95)
    rows["AimErrorSampleCount"] = (total_shots * on_target).astype(int)
    rows["TotalShots"]  = total_shots
    rows["TotalHits"]   = (total_shots * rows["HitRate"]).astype(int)
    rows["TotalKills"]  = rng.integers(5, 28, n)
    rows["TotalDeaths"] = rng.integers(0, 10, n)
    rows["Label"]       = np.ones(n, dtype=int)
    return pd.DataFrame(rows)


# ──────────────────────────────────────────────
# 4. Cheater Profil C: Triggerbot
#    Maschinell konstante ShotIntervalStdDev und ReactionTimeStdDev —
#    das ist der Hauptunterschied zu E-Sportlern, die menschliche Varianz behalten
# ──────────────────────────────────────────────
def gen_triggerbot(n):
    rows = {}
    rows["PlayerID"]               = [f"trigger_{i:04d}" for i in range(n)]
    rows["SessionDurationSeconds"] = rng.uniform(30, 260, n)

    rows["AimAngularSpeedMean"]    = clip(rng.normal(95,  28,  n), 20, 260)
    rows["AimAngularSpeedStdDev"]  = clip(rng.normal(42,  16,  n),  6, 160)
    rows["AimAngularErrorMean"]    = clip(rng.normal(7,   4,   n),  0.5, 28)
    rows["AimAngularErrorStdDev"]  = clip(rng.normal(4,   2,   n),  0.3, 14)
    rows["AimFlipRatio"]           = clip(rng.normal(0.02, 0.01, n), 0, 0.10)

    rows["MovementSpeedMean"]          = clip(rng.normal(415, 75,  n), 60, 615)
    rows["MovementSpeedMax"]           = clip(rng.normal(578, 40,  n), 200, 622)
    rows["DirectionChangesPerSecond"]  = clip(rng.normal(0.40, 0.14, n), 0.04, 1.6)
    rows["SpeedViolationRatio"]        = clip(rng.exponential(0.010, n), 0, 0.05)
    rows["MovementPathEntropy"]        = clip(rng.normal(2.50, 0.36, n), 0.6, 3.5)

    # Reaktionszeit Triggerbot: 60–180ms — überlappt mit E-Sportlern
    # ABER: StdDev ist maschinell niedrig (0.002–0.010) — das ist der Schlüssel
    rows["ReactionTimeMean"]   = clip(rng.normal(0.10, 0.04, n), 0.04, 0.25)
    rows["ReactionTimeStdDev"] = clip(rng.normal(0.005, 0.003, n), 0.001, 0.015)  # ← maschinell
    rows["ShotIntervalMean"]   = clip(rng.normal(0.46, 0.06, n), 0.1,  0.9)
    rows["ShotIntervalStdDev"] = clip(rng.normal(0.005, 0.003, n), 0.001, 0.012)  # ← maschinell
    rows["ShotsPerSecond"]     = clip(rng.normal(2.1,  0.4,  n), 0.5,  5.5)

    rows["HitRate"]        = clip(rng.normal(0.58, 0.10, n), 0.30, 0.85)
    rows["HeadshotRate"]   = np.zeros(n)
    rows["KillsPerMinute"] = clip(rng.normal(4.2,  1.1,  n), 1.2,  8.5)
    rows["KillDeathRatio"] = clip(rng.normal(4.2,  1.8,  n), 0.8, 11.0)

    total_shots = rng.integers(20, 170, n)
    # Anteil Schuesse mit Ziel im Kegel (gemessen: Mensch ~0.20, Aimbot ~0.66-0.80)
    on_target = clip(rng.normal(0.55, 0.1, n), 0.02, 0.95)
    rows["AimErrorSampleCount"] = (total_shots * on_target).astype(int)
    rows["TotalShots"]  = total_shots
    rows["TotalHits"]   = (total_shots * rows["HitRate"]).astype(int)
    rows["TotalKills"]  = rng.integers(8, 38, n)
    rows["TotalDeaths"] = rng.integers(0,  8, n)
    rows["Label"]       = np.ones(n, dtype=int)
    return pd.DataFrame(rows)


# ──────────────────────────────────────────────
# Zusammenführen, mischen, speichern
# ──────────────────────────────────────────────
df_casual  = gen_casual(N_CASUAL)
df_skilled = gen_skilled(N_SKILLED)
df_esport  = gen_esport(N_ESPORT)
df_aimbot  = gen_aimbot(N_AIMBOT)
df_speed   = gen_speedhack(N_SPEED)
df_trigger = gen_triggerbot(N_TRIGGER)

df = pd.concat([df_casual, df_skilled, df_esport,
                df_aimbot, df_speed,   df_trigger], ignore_index=True)
df = df.sample(frac=1, random_state=SEED).reset_index(drop=True)

col_order = [
    "PlayerID", "SessionDurationSeconds",
    "AimAngularSpeedMean", "AimAngularSpeedStdDev",
    "AimAngularErrorMean", "AimAngularErrorStdDev",
    "AimErrorSampleCount", "AimFlipRatio",
    "MovementSpeedMean", "MovementSpeedMax",
    "DirectionChangesPerSecond", "SpeedViolationRatio", "MovementPathEntropy",
    "ReactionTimeMean", "ReactionTimeStdDev",
    "ShotIntervalMean", "ShotIntervalStdDev", "ShotsPerSecond",
    "HitRate", "HeadshotRate", "KillsPerMinute", "KillDeathRatio",
    "TotalShots", "TotalHits", "TotalKills", "TotalDeaths",
    "Label"
]
df = df[col_order]

import os
output_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "training_data.csv")
df.to_csv(output_path, index=False, float_format="%.4f")

print(f"[OK] Datensatz generiert: {output_path}")
print(f"   Gesamt:          {len(df)} Spieler")
print(f"   Legitim:         {(df.Label == 0).sum()} ({(df.Label==0).mean()*100:.1f}%)")
print(f"     - Casual:      {N_CASUAL}")
print(f"     - Skilled:     {N_SKILLED}")
print(f"     - E-Sportler:  {N_ESPORT}")
print(f"   Cheater:         {(df.Label == 1).sum()} ({(df.Label==1).mean()*100:.1f}%)")
print(f"     - Aimbot:      {N_AIMBOT}")
print(f"     - Speedhack:   {N_SPEED}")
print(f"     - Triggerbot:  {N_TRIGGER}")
