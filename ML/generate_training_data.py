"""
generate_training_data.py
=========================
Schritt 2 der Bachelorarbeit: Synthetische Trainingsdaten generieren.

Erzeugt einen CSV-Datensatz mit simulierten Spieler-Sessions:
  - ~950 legitime Spieler (Label=0)
  - ~50 Cheater (Label=1), aufgeteilt in 3 Profile:
      * Aimbot       (~20 Spieler)
      * Speedhack    (~15 Spieler)
      * Triggerbot   (~15 Spieler)

Die Spalten sind identisch mit der UE5-generierten session_01.csv,
sodass beide Datensätze direkt zusammengeführt werden können.

Ausgabe: training_data.csv (im selben Ordner)

Verwendung:
    python generate_training_data.py

Abhängigkeiten: numpy, pandas
    pip install numpy pandas
"""

import numpy as np
import pandas as pd

# Reproduzierbarkeit
SEED = 42
rng = np.random.default_rng(SEED)

N_LEGIT   = 950
N_AIMBOT  = 20
N_SPEED   = 15
N_TRIGGER = 15
N_TOTAL   = N_LEGIT + N_AIMBOT + N_SPEED + N_TRIGGER


def clip(arr, lo, hi):
    return np.clip(arr, lo, hi)


# ------------------------------------------------------------------
# 1. Legitime Spieler
# ------------------------------------------------------------------
def gen_legit(n):
    rows = {}

    # Meta
    rows["PlayerID"]              = [f"legit_{i:04d}" for i in range(n)]
    rows["SessionDurationSeconds"] = rng.uniform(20, 300, n)

    # Aim — hohe natürliche Varianz
    rows["AimAngularSpeedMean"]   = clip(rng.normal(80,  30,  n), 10, 300)
    rows["AimAngularSpeedStdDev"] = clip(rng.normal(90,  40,  n), 20, 400)
    rows["AimAngularErrorMean"]   = clip(rng.normal(15,  8,   n),  0,  60)
    rows["AimAngularErrorStdDev"] = clip(rng.normal(10,  5,   n),  0,  40)
    rows["AimFlipRatio"]          = clip(rng.normal(0.02,0.015,n), 0, 0.15)

    # Movement — normale Bewegung, gelegentlich kurze Sprints
    rows["MovementSpeedMean"]          = clip(rng.normal(420, 80,  n), 50, 600)
    rows["MovementSpeedMax"]           = clip(rng.normal(580, 60,  n), 200, 620)
    rows["DirectionChangesPerSecond"]  = clip(rng.normal(0.4, 0.15,n), 0.05, 1.5)
    rows["SpeedViolationRatio"]        = clip(rng.exponential(0.01,n), 0, 0.05)
    rows["MovementPathEntropy"]        = clip(rng.normal(2.5, 0.4, n), 0.5, 3.5)

    # Timing — menschliche Reaktionszeiten 200–800ms
    rows["ReactionTimeMean"]   = clip(rng.normal(0.45, 0.15, n), 0.15, 1.2)
    rows["ReactionTimeStdDev"] = clip(rng.normal(0.12, 0.05, n), 0.03, 0.5)
    rows["ShotIntervalMean"]   = clip(rng.normal(0.55, 0.20, n), 0.1,  2.0)
    rows["ShotIntervalStdDev"] = clip(rng.normal(0.18, 0.08, n), 0.02, 0.8)
    rows["ShotsPerSecond"]     = clip(rng.normal(1.1,  0.4,  n), 0.1,  4.0)

    # Rate
    rows["HitRate"]        = clip(rng.normal(0.25, 0.10, n), 0.02, 0.65)
    rows["HeadshotRate"]   = clip(rng.normal(0.18, 0.08, n), 0.0,  0.45)
    rows["KillsPerMinute"] = clip(rng.normal(1.5,  0.7,  n), 0.0,  6.0)
    rows["KillDeathRatio"] = clip(rng.normal(1.0,  0.5,  n), 0.0,  5.0)

    total_shots = rng.integers(10, 200, n)
    rows["TotalShots"]  = total_shots
    rows["TotalHits"]   = (total_shots * rows["HitRate"]).astype(int)
    rows["TotalKills"]  = rng.integers(0, 15, n)
    rows["TotalDeaths"] = rng.integers(0, 15, n)
    rows["Label"]       = np.zeros(n, dtype=int)

    return pd.DataFrame(rows)


# ------------------------------------------------------------------
# 2. Cheater Profil A: Aimbot
#    Kennzeichen: extrem niedrige AimStdDev, hohe HitRate/HeadshotRate,
#                 kurze und konstante Reaktionszeiten
# ------------------------------------------------------------------
def gen_aimbot(n):
    rows = {}

    rows["PlayerID"]              = [f"aimbot_{i:04d}" for i in range(n)]
    rows["SessionDurationSeconds"] = rng.uniform(30, 200, n)

    # Aim — unnatürlich stabil (Aimbot zielt perfekt)
    rows["AimAngularSpeedMean"]   = clip(rng.normal(150, 20, n), 80, 250)
    rows["AimAngularSpeedStdDev"] = clip(rng.normal(8,   4,  n),  1,  20)   # ← verdächtig niedrig
    rows["AimAngularErrorMean"]   = clip(rng.normal(1.5, 1.0,n),  0,   5)   # ← fast immer auf Kopf
    rows["AimAngularErrorStdDev"] = clip(rng.normal(0.8, 0.4,n),  0,   3)
    rows["AimFlipRatio"]          = clip(rng.normal(0.08,0.03,n), 0,  0.2)  # Snap-Aim

    # Movement — unauffällig
    rows["MovementSpeedMean"]         = clip(rng.normal(400, 70, n), 50, 600)
    rows["MovementSpeedMax"]          = clip(rng.normal(570, 50, n), 200, 620)
    rows["DirectionChangesPerSecond"] = clip(rng.normal(0.38,0.12,n),0.05,1.5)
    rows["SpeedViolationRatio"]       = clip(rng.exponential(0.008,n),0, 0.03)
    rows["MovementPathEntropy"]       = clip(rng.normal(2.4, 0.4, n),0.5, 3.5)

    # Timing — sehr kurze, konstante Reaktionszeit
    rows["ReactionTimeMean"]   = clip(rng.normal(0.04, 0.015,n), 0.01, 0.12)  # ← ~40ms
    rows["ReactionTimeStdDev"] = clip(rng.normal(0.008,0.004,n), 0.001,0.03)  # ← fast 0
    rows["ShotIntervalMean"]   = clip(rng.normal(0.50, 0.15, n), 0.1,  1.5)
    rows["ShotIntervalStdDev"] = clip(rng.normal(0.05, 0.02, n), 0.01, 0.15)
    rows["ShotsPerSecond"]     = clip(rng.normal(1.8,  0.4,  n), 0.5,  5.0)

    # Rate — sehr hohe Trefferquote
    rows["HitRate"]        = clip(rng.normal(0.88, 0.05, n), 0.70, 1.0)  # ← ~88%
    rows["HeadshotRate"]   = clip(rng.normal(0.75, 0.08, n), 0.50, 1.0)  # ← ~75% Headshots
    rows["KillsPerMinute"] = clip(rng.normal(6.5,  1.0,  n), 3.0, 12.0)
    rows["KillDeathRatio"] = clip(rng.normal(8.0,  2.0,  n), 3.0, 20.0)

    total_shots = rng.integers(30, 150, n)
    rows["TotalShots"]  = total_shots
    rows["TotalHits"]   = (total_shots * rows["HitRate"]).astype(int)
    rows["TotalKills"]  = rng.integers(10, 40, n)
    rows["TotalDeaths"] = rng.integers(0,  5,  n)
    rows["Label"]       = np.ones(n, dtype=int)

    return pd.DataFrame(rows)


# ------------------------------------------------------------------
# 3. Cheater Profil B: Speedhack
#    Kennzeichen: SpeedViolationRatio > 0, MovementSpeedMax >> 600
# ------------------------------------------------------------------
def gen_speedhack(n):
    rows = {}

    rows["PlayerID"]              = [f"speed_{i:04d}" for i in range(n)]
    rows["SessionDurationSeconds"] = rng.uniform(20, 180, n)

    # Aim — durchschnittlich
    rows["AimAngularSpeedMean"]   = clip(rng.normal(85,  35, n), 10, 300)
    rows["AimAngularSpeedStdDev"] = clip(rng.normal(95,  45, n), 20, 400)
    rows["AimAngularErrorMean"]   = clip(rng.normal(14,  7,  n),  0,  60)
    rows["AimAngularErrorStdDev"] = clip(rng.normal(9,   4,  n),  0,  40)
    rows["AimFlipRatio"]          = clip(rng.normal(0.025,0.015,n),0,0.15)

    # Movement — deutlich über dem Limit
    rows["MovementSpeedMean"]         = clip(rng.normal(900, 120, n), 650, 1500)  # ← über Max
    rows["MovementSpeedMax"]          = clip(rng.normal(1400,200, n), 800, 3000)  # ← weit über Max
    rows["DirectionChangesPerSecond"] = clip(rng.normal(0.6, 0.2, n), 0.1,  2.0)
    rows["SpeedViolationRatio"]       = clip(rng.normal(0.6, 0.15,n), 0.3,  1.0)  # ← hoch
    rows["MovementPathEntropy"]       = clip(rng.normal(2.8, 0.3, n), 1.5,  3.5)

    # Timing — normal
    rows["ReactionTimeMean"]   = clip(rng.normal(0.42, 0.14,n), 0.15, 1.2)
    rows["ReactionTimeStdDev"] = clip(rng.normal(0.11, 0.05,n), 0.03, 0.5)
    rows["ShotIntervalMean"]   = clip(rng.normal(0.58, 0.20,n), 0.1,  2.0)
    rows["ShotIntervalStdDev"] = clip(rng.normal(0.17, 0.08,n), 0.02, 0.8)
    rows["ShotsPerSecond"]     = clip(rng.normal(1.2,  0.4, n), 0.1,  4.0)

    # Rate — leicht besser durch Mobilität
    rows["HitRate"]        = clip(rng.normal(0.30, 0.10,n), 0.05, 0.65)
    rows["HeadshotRate"]   = clip(rng.normal(0.20, 0.08,n), 0.0,  0.45)
    rows["KillsPerMinute"] = clip(rng.normal(3.5,  1.0, n), 1.0,  8.0)
    rows["KillDeathRatio"] = clip(rng.normal(3.0,  1.0, n), 0.5,  8.0)

    total_shots = rng.integers(15, 180, n)
    rows["TotalShots"]  = total_shots
    rows["TotalHits"]   = (total_shots * rows["HitRate"]).astype(int)
    rows["TotalKills"]  = rng.integers(5, 25, n)
    rows["TotalDeaths"] = rng.integers(0, 8,  n)
    rows["Label"]       = np.ones(n, dtype=int)

    return pd.DataFrame(rows)


# ------------------------------------------------------------------
# 4. Cheater Profil C: Triggerbot
#    Kennzeichen: ReactionTimeStdDev ≈ 0, konstante ShotIntervals
# ------------------------------------------------------------------
def gen_triggerbot(n):
    rows = {}

    rows["PlayerID"]              = [f"trigger_{i:04d}" for i in range(n)]
    rows["SessionDurationSeconds"] = rng.uniform(25, 250, n)

    # Aim — leicht überdurchschnittlich
    rows["AimAngularSpeedMean"]   = clip(rng.normal(90,  30, n), 10, 300)
    rows["AimAngularSpeedStdDev"] = clip(rng.normal(40,  15, n),  5, 150)   # etwas niedriger
    rows["AimAngularErrorMean"]   = clip(rng.normal(8,   4,  n),  0,  30)
    rows["AimAngularErrorStdDev"] = clip(rng.normal(4,   2,  n),  0,  15)
    rows["AimFlipRatio"]          = clip(rng.normal(0.02,0.01,n), 0, 0.10)

    # Movement — normal
    rows["MovementSpeedMean"]         = clip(rng.normal(410, 75, n), 50, 600)
    rows["MovementSpeedMax"]          = clip(rng.normal(575, 55, n), 200, 620)
    rows["DirectionChangesPerSecond"] = clip(rng.normal(0.38,0.13,n),0.05,1.5)
    rows["SpeedViolationRatio"]       = clip(rng.exponential(0.009,n),0, 0.04)
    rows["MovementPathEntropy"]       = clip(rng.normal(2.45,0.38,n),0.5, 3.5)

    # Timing — maschinell präzise, fast keine Varianz
    rows["ReactionTimeMean"]   = clip(rng.normal(0.08, 0.02, n), 0.03, 0.18)  # ← ~80ms
    rows["ReactionTimeStdDev"] = clip(rng.normal(0.003,0.002,n), 0.0,  0.01)  # ← fast 0
    rows["ShotIntervalMean"]   = clip(rng.normal(0.48, 0.05, n), 0.1,  0.8)
    rows["ShotIntervalStdDev"] = clip(rng.normal(0.004,0.002,n), 0.0,  0.01)  # ← fast 0
    rows["ShotsPerSecond"]     = clip(rng.normal(2.0,  0.3,  n), 0.5,  5.0)

    # Rate — gut aber nicht extrem
    rows["HitRate"]        = clip(rng.normal(0.60, 0.08, n), 0.35, 0.90)
    rows["HeadshotRate"]   = clip(rng.normal(0.35, 0.08, n), 0.10, 0.65)
    rows["KillsPerMinute"] = clip(rng.normal(4.5,  1.0,  n), 1.5,  9.0)
    rows["KillDeathRatio"] = clip(rng.normal(4.5,  1.5,  n), 1.0, 12.0)

    total_shots = rng.integers(20, 160, n)
    rows["TotalShots"]  = total_shots
    rows["TotalHits"]   = (total_shots * rows["HitRate"]).astype(int)
    rows["TotalKills"]  = rng.integers(8, 35, n)
    rows["TotalDeaths"] = rng.integers(0, 6,  n)
    rows["Label"]       = np.ones(n, dtype=int)

    return pd.DataFrame(rows)


# ------------------------------------------------------------------
# Zusammenführen, mischen, speichern
# ------------------------------------------------------------------
df_legit   = gen_legit(N_LEGIT)
df_aimbot  = gen_aimbot(N_AIMBOT)
df_speed   = gen_speedhack(N_SPEED)
df_trigger = gen_triggerbot(N_TRIGGER)

df = pd.concat([df_legit, df_aimbot, df_speed, df_trigger], ignore_index=True)
df = df.sample(frac=1, random_state=SEED).reset_index(drop=True)

# Spaltenreihenfolge exakt wie UE5 CSV
col_order = [
    "PlayerID", "SessionDurationSeconds",
    "AimAngularSpeedMean", "AimAngularSpeedStdDev",
    "AimAngularErrorMean", "AimAngularErrorStdDev", "AimFlipRatio",
    "MovementSpeedMean", "MovementSpeedMax",
    "DirectionChangesPerSecond", "SpeedViolationRatio", "MovementPathEntropy",
    "ReactionTimeMean", "ReactionTimeStdDev",
    "ShotIntervalMean", "ShotIntervalStdDev", "ShotsPerSecond",
    "HitRate", "HeadshotRate", "KillsPerMinute", "KillDeathRatio",
    "TotalShots", "TotalHits", "TotalKills", "TotalDeaths",
    "Label"
]
df = df[col_order]

output_path = "training_data.csv"
df.to_csv(output_path, index=False, float_format="%.4f")

print(f"✅ Datensatz generiert: {output_path}")
print(f"   Gesamt:   {len(df)} Spieler")
print(f"   Legitim:  {(df.Label == 0).sum()} ({(df.Label==0).mean()*100:.1f}%)")
print(f"   Cheater:  {(df.Label == 1).sum()} ({(df.Label==1).mean()*100:.1f}%)")
print(f"     - Aimbot:    {N_AIMBOT}")
print(f"     - Speedhack: {N_SPEED}")
print(f"     - Triggerbot:{N_TRIGGER}")
print(f"\nSpalten: {list(df.columns)}")
print(f"\nVorschau:")
print(df.head(5).to_string())
