# Bachelorarbeit – Implementierungsdokumentation

**Thema:** Kostenasymmetrisches ML-Klassifikationsmodell zur serverseitigen Cheat-Erkennung
**Projekt:** UE5 Shooter + Python ML Pipeline
**GitHub:** https://github.com/phileas0/Shootergame (Branch: vs2022-build-env)
**UE Version:** 5.7

---

## Regel

> Jede Änderung an Code, Dateien oder Konfiguration wird hier eingetragen.
> Format: Datum | Datei | Was geändert | Warum

---

## Schritt 1 – UE5 Telemetrie-Logger ✅ (Abgeschlossen: 07.04.2026)

### Neue Dateien

| Datum | Datei | Beschreibung |
|---|---|---|
| 07.04.2026 | `Source/Shootergame/Public/TelemetryLogger.h` | Struct `FPlayerSessionData` (25 Features) + `UTelemetryLogger` UObject mit `FlushToCSV()` |
| 07.04.2026 | `Source/Shootergame/Private/TelemetryLogger.cpp` | CSV-Schreiblogik nach `Saved/Telemetry/`, Append-Modus wenn Datei bereits existiert |
| 07.04.2026 | `Source/Shootergame/Public/TelemetryCollector.h` | `UTelemetryCollector` ActorComponent — tick-basiertes Sampling alle 100ms (server-only) |
| 07.04.2026 | `Source/Shootergame/Private/TelemetryCollector.cpp` | Aim/Movement/Timing/Rate Sampling, `FinalizeSession()` berechnet alle Aggregat-Features |
| 07.04.2026 | `Source/Shootergame/Public/ShooterGameMode.h` | `AShooterGameMode` C++ GameMode — verwaltet Logger, ruft `EndPlay` → `FlushToCSV` auf |
| 07.04.2026 | `Source/Shootergame/Private/ShooterGameMode.cpp` | `BeginPlay` erstellt Logger, `OnPlayerSessionEnd` finalisiert Session, `EndPlay` schreibt CSV |

### Geänderte Dateien

| Datum | Datei | Was geändert | Warum |
|---|---|---|---|
| 07.04.2026 | `Source/Shootergame/Shootergame.Build.cs` | `EnhancedInput` zu `PublicDependencyModuleNames` hinzugefügt | Benötigt für `IA_Shoot` Enhanced Input Actions im Blueprint |

### Blueprint-Änderungen

| Datum | Blueprint | Was geändert | Warum |
|---|---|---|---|
| 07.04.2026 | `BP_ShooterGameMode` | Parent Class → `ShooterGameMode` (C++) | Telemetrie-Logik komplett in C++ verlagert |
| 07.04.2026 | `BP_ShooterGameMode` | `Construct TelemetryLogger`, `OnPlayerSessionEnd`, `Event EndPlay` → `FlushToCSV` Nodes entfernt | Wird jetzt von C++ GameMode übernommen |
| 07.04.2026 | `BP_ShooterCharacter` | Im Die-Event: `Cast To BP_ShooterGameMode` → `On Player Session End` ersetzt durch `Cast To ShooterGameMode` → `On Player Session End` | Parent Class geändert, alter Cast war broken |
| 07.04.2026 | `UI_Shooter` | `Cast To BP_ShooterGameMode` bleibt (für Score Updated Dispatcher) | Score Updated ist Blueprint-Dispatcher, kein C++ Delegate — kein Konflikt |

### Features in CSV (25 Spalten)

| Kategorie | Features |
|---|---|
| Aim | AimAngularSpeedMean, AimAngularSpeedStdDev, AimAngularErrorMean, AimAngularErrorStdDev, AimFlipRatio |
| Movement | MovementSpeedMean, MovementSpeedMax, DirectionChangesPerSecond, SpeedViolationRatio, MovementPathEntropy |
| Timing | ReactionTimeMean, ReactionTimeStdDev, ShotIntervalMean, ShotIntervalStdDev, ShotsPerSecond |
| Rate | HitRate, HeadshotRate, KillsPerMinute, KillDeathRatio, TotalShots, TotalHits, TotalKills, TotalDeaths |
| Meta | PlayerID, SessionDurationSeconds, Label |

### Ausgabe
- **Pfad:** `Saved/Telemetry/session_01.csv`
- **Trigger:** `EndPlay` (Stop im Editor, Level-Wechsel, Spielende) + `OnPlayerSessionEnd` bei jedem Tod
- **Ergebnis:** ✅ CSV wird zuverlässig geschrieben

### Bekannte Einschränkungen
- `PlayerID` ist leer im PIE-Modus (Get Player Display Name liefert leeren String lokal) — im Multiplayer-Betrieb korrekt
- `AimAngularErrorMean` = 0 weil `RecordEnemyVisible()` noch nicht im Blueprint verdrahtet ist

---

## Schritt 2 – Synthetische Trainingsdaten ✅ (Abgeschlossen: 07.04.2026)

| Datum | Datei | Beschreibung |
|---|---|---|
| 07.04.2026 | `ML/generate_training_data.py` | Python-Skript generiert 1000 simulierte Spieler-Sessions |
| 07.04.2026 | `ML/training_data.csv` | Fertiger Trainingsdatensatz — 950 legitim (Label=0), 50 Cheater (Label=1) |

### Datensatz-Details

| Profil | Anzahl | Label | Erkennungsmerkmale |
|---|---|---|---|
| Legitime Spieler | 950 | 0 | Hohe AimStdDev, ReactionTime 200–800ms, HitRate ~25% |
| Aimbot | 20 | 1 | AimAngularSpeedStdDev <20, ReactionTime ~40ms, HitRate >70%, HeadshotRate >50% |
| Speedhack | 15 | 1 | SpeedViolationRatio >0.3, MovementSpeedMax >800 cm/s |
| Triggerbot | 15 | 1 | ReactionTimeStdDev ≈0, ShotIntervalStdDev ≈0 |

- Klassenverteilung: 95% / 5% (wie in Vorlage Kapitel 2.3 beschrieben)
- Spalten identisch mit UE5 `session_01.csv` → direkt zusammenführbar
- Seed: 42 (reproduzierbar)

---

## Schritt 3 – ML-Modell (Ausstehend)

| Datum | Datei | Beschreibung |
|---|---|---|
| — | — | — |

---

## Schritt 4 – Evaluation (Ausstehend)

| Datum | Datei | Beschreibung |
|---|---|---|
| — | — | — |

---

## Schritt 5 – UE5 Integration (Ausstehend)

| Datum | Datei | Beschreibung |
|---|---|---|
| — | — | — |
