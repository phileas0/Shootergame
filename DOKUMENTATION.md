# Bachelorarbeit – Implementierungsdokumentation

**Thema:** Kostenasymmetrisches ML-Klassifikationsmodell zur serverseitigen Cheat-Erkennung  
**Projekt:** UE5 Shooter + Python ML Pipeline  
**GitHub:** https://github.com/phileas0/Shootergame (Branch: vs2022-build-env)  
**UE Version:** 5.7  
**Stand:** 04.06.2026

---

## Regel

> Jede Änderung an Code, Dateien oder Konfiguration wird hier eingetragen.  
> Format: Datum | Datei | Was geändert | Warum

### Letzte Änderungen (Projekt-Setup & ML-Pipeline)

| Datum | Datei | Was geändert | Warum |
|---|---|---|---|
| 04.06.2026 | `requirements.txt` | [NEW] Abhängigkeitsliste im Root erstellt | Ermöglicht einfache Installation aller Python-Bibliotheken (`numpy`, `pandas`, `matplotlib`, `scikit-learn`, `joblib`) für die ML-Pipeline. |
| 04.06.2026 | `.gitignore` | Caching-Regeln `__pycache__/` und `*.pyc` hinzugefügt | Hält das Repository frei von temporär kompilierten Python-Dateien. |
| 04.06.2026 | `ML/*.py` | Umfassende Kommentierung und Quellcode-Dokumentation ergänzt | Verknüpft die Implementierung (Profile, Hyperparameter, Regularisierung, τ*-Verschiebung) direkt mit den Kapiteln der Bachelorarbeit (Kap. 4, 5, 6, 8, 9). |

---

## Schritt 1 – UE5 Telemetrie-Logger ✅ (Abgeschlossen: 07.–08.04.2026)

### Neue C++ Dateien

| Datum | Datei | Beschreibung |
|---|---|---|
| 07.04.2026 | `Source/Shootergame/Public/TelemetryLogger.h` | Struct `FPlayerSessionData` (25 Features) + `UTelemetryLogger` UObject mit `FlushToCSV()` |
| 07.04.2026 | `Source/Shootergame/Private/TelemetryLogger.cpp` | CSV-Schreiblogik nach `Saved/Telemetry/`, Timestamp-basierter Dateiname (kein Datei-Lock) |
| 07.04.2026 | `Source/Shootergame/Public/TelemetryCollector.h` | `UTelemetryCollector` ActorComponent — tick-basiertes Sampling, Hit/Kill/Death/ReactionTime Tracking |
| 07.04.2026 | `Source/Shootergame/Private/TelemetryCollector.cpp` | Aim/Movement/Timing/Rate Sampling, Line Trace alle 0.2s für ReactionTime, `FinalizeSession()` berechnet Aggregate |
| 07.04.2026 | `Source/Shootergame/Public/ShooterGameMode.h` | `AShooterGameMode` C++ GameMode — KillerMap, OnActorSpawned, OnCharacterDestroyed, EndPlay→FlushToCSV |
| 07.04.2026 | `Source/Shootergame/Private/ShooterGameMode.cpp` | Bindet `OnTakeAnyDamage` an alle Characters (inkl. NPCs), KillerMap für Kill-Tracking, `EndPlay` schreibt CSV |

### Geänderte Dateien

| Datum | Datei | Was geändert | Warum |
|---|---|---|---|
| 07.04.2026 | `Source/Shootergame/Shootergame.Build.cs` | `EnhancedInput` zu `PublicDependencyModuleNames` hinzugefügt | Benötigt für Enhanced Input Actions |
| 08.04.2026 | `Source/Shootergame/Public/TelemetryCollector.h` | `RecordHitWithBone()`, `CheckEnemyLineOfSight()`, `OnOwnerTakeAnyDamage()` hinzugefügt | Automatische Enemy-Visibility- und Hit-Erkennung |
| 08.04.2026 | `Source/Shootergame/Private/TelemetryCollector.cpp` | Line Trace alle 0.2s → `RecordEnemyVisible()`; `OnOwnerTakeAnyDamage` findet Schützen via InstigatedBy | ReactionTimeMean/StdDev, HitRate waren 0 |
| 08.04.2026 | `Source/Shootergame/Public/ShooterGameMode.h` | `OnPawnTakeAnyDamage`, `OnActorSpawned`, `OnCharacterDestroyed`, `KillerMap` hinzugefügt | TotalKills war immer 0 |
| 08.04.2026 | `Source/Shootergame/Private/ShooterGameMode.cpp` | `TActorIterator` bindet Damage-Delegate an alle existierenden Characters bei BeginPlay; `AddOnActorSpawnedHandler` für neue Spawns; KillerMap + OnDestroyed für Kill-Tracking | Hits auf NPCs wurden nicht getrackt; Kill-Zähler funktionierte nicht |

### Blueprint-Änderungen

| Datum | Blueprint | Was geändert | Warum |
|---|---|---|---|
| 07.04.2026 | `BP_ShooterGameMode` | Parent Class → `ShooterGameMode` (C++) | Telemetrie-Logik komplett in C++ |
| 07.04.2026 | `BP_ShooterCharacter` | Die-Event: Cast zu `ShooterGameMode` → `On Player Session End` mit `Killer Actor` Parameter | Kill-Tracking braucht Killer-Referenz |
| 07.04.2026 | `BP_ShooterProjectile_Bullet` | Break Hit Result → `RecordHitWithBone` (Bone Name weitergeleitet) | Headshot-Erkennung vorbereitet |
| 07.04.2026 | `UI_Shooter` | `Cast To BP_ShooterGameMode` bleibt (Score Updated Dispatcher ist Blueprint-only) | Kein Konflikt mit C++ GameMode |

### Features in CSV (25 Spalten)

| Kategorie | Features | Status |
|---|---|---|
| Aim | AimAngularSpeedMean, AimAngularSpeedStdDev, AimAngularErrorMean, AimAngularErrorStdDev, AimFlipRatio | ✅ (ErrorMean/StdDev = 0, ignoriert in ML) |
| Movement | MovementSpeedMean, MovementSpeedMax, DirectionChangesPerSecond, SpeedViolationRatio, MovementPathEntropy | ✅ |
| Timing | ReactionTimeMean, ReactionTimeStdDev, ShotIntervalMean, ShotIntervalStdDev, ShotsPerSecond | ✅ |
| Rate | HitRate, HeadshotRate, KillsPerMinute, KillDeathRatio, TotalShots, TotalHits, TotalKills, TotalDeaths | ✅ (HeadshotRate = 0, ignoriert in ML) |
| Meta | PlayerID, SessionDurationSeconds, Label | ✅ |

### Ausgabe
- **Pfad:** `Saved/Telemetry/session_<YYYY-MM-DD_HH-MM-SS>.csv`
- **Trigger:** `EndPlay` (Stop im Editor, Level-Wechsel) + `OnPlayerSessionEnd` bei jedem Tod
- **Ergebnis:** ✅ Alle 22 aktiven Features werden korrekt getracked

### Dauerhaft-0-Features (werden im ML ignoriert)
| Feature | Grund |
|---|---|
| `AimAngularErrorMean/StdDev` | Crosshair-zu-Feind-Winkel-Messung nicht implementiert (zu komplex für UE5-Blueprint) |
| `HeadshotRate` | Bone-Name aus `AnyDamage` nicht verfügbar |

---

## Schritt 2 – Synthetische Trainingsdaten ✅ (Abgeschlossen: 08.04.2026)

### Dateien

| Datum | Datei | Beschreibung |
|---|---|---|
| 08.04.2026 | `ML/generate_training_data.py` | Python-Skript generiert 5000 simulierte Spieler-Sessions mit 3 legitimen Profilen + 3 Cheater-Profilen |
| 08.04.2026 | `ML/training_data.csv` | Fertiger Trainingsdatensatz — 4750 legitim (Label=0), 250 Cheater (Label=1) |

### Datensatz-Details

| Profil | Anzahl | Label | Erkennungsmerkmale |
|---|---|---|---|
| Casual-Spieler | 3000 | 0 | Breite Streuung, ReactionTime 300–900ms, HitRate ~20% |
| Erfahrene Spieler | 1250 | 0 | Solide Werte, ReactionTime 180–550ms, HitRate ~35% |
| E-Sportler | 500 | 0 | Ausreißer: ReactionTime 80–350ms, HitRate bis 78%, KDR bis 8.5 — **überlappt bewusst mit Cheatern** |
| Aimbot | 100 | 1 | AimAngularSpeedStdDev <40, ReactionTime 15–180ms, HitRate >55% |
| Speedhack | 75 | 1 | SpeedViolationRatio >0.20, MovementSpeedMax >700 cm/s |
| Triggerbot | 75 | 1 | ReactionTimeStdDev <0.015, ShotIntervalStdDev <0.012 (maschinell konstant) |

**Wichtig:** E-Sportler können in einzelnen Features Cheater-Werte erreichen (z.B. ReactionTimeMean ~100ms). Der entscheidende Unterschied zu Triggerbot/Aimbot ist die menschliche **Varianz**: legitime Spieler haben immer ReactionTimeStdDev > 0.02 und ShotIntervalStdDev > 0.03.

- Klassenverteilung: 95% / 5% (entspricht Bachelorarbeit Kap. 2.3)
- Spalten identisch mit UE5-CSV → direkt zusammenführbar
- Seed: 42 (reproduzierbar)
- Drei 0-Features (`AimAngularErrorMean`, `AimAngularErrorStdDev`, `HeadshotRate`) konsistent auf 0

---

## Schritt 3 – ML-Modell + τ*-Analyse ✅ (Abgeschlossen: 08.04.2026)

### Dateien

| Datum | Datei | Beschreibung |
|---|---|---|
| 08.04.2026 | `ML/train_model.py` | ML-Training: LR + RF, 5-fold CV, τ-Variation 0.01–0.99, Kostenmatrix C_FP=1/C_FN=10, Plots, Export |
| 08.04.2026 | `ML/cost_threshold_analysis.py` | **Zentraler Beitrag Kap. 9:** Simuliert Score-Verteilungen mit Grauzone, berechnet τ* für C_FN/C_FP ∈ {1,2,5,10,20,50} |
| 08.04.2026 | `ML/results/model_lr.pkl` | Trainiertes Logistic Regression Modell (joblib, für Schritt 5) |
| 08.04.2026 | `ML/results/model_rf.pkl` | Trainiertes Random Forest Modell (joblib, für Schritt 5) |
| 08.04.2026 | `ML/results/evaluation_plots.png` | ROC-Kurven, Precision-Recall, Kosten vs. τ, Confusion Matrices, Feature Importances (6 Plots) |
| 08.04.2026 | `ML/results/tau_vs_cost_ratio.png` | **Kernplot:** τ*-Verschiebung bei steigender Kostenasymmetrie + Score-Verteilungen + Kosten-Kurven |
| 08.04.2026 | `ML/results/tau_vs_cost_ratio.csv` | τ* je Kostenverhältnis mit FP/FN/Kosten |
| 08.04.2026 | `ML/results/summary.csv` | Modellvergleich: CV-AUC, Test-AUC, Avg. Precision, opt. τ, Kosten |
| 08.04.2026 | `ML/results/feature_importance.csv` | Feature Importances RF (sortiert) |
| 08.04.2026 | `ML/results/threshold_analysis_logistic_regression.csv` | τ-Analyse LR: TP/FP/FN/TN, Precision, Recall, F1, Kosten |
| 08.04.2026 | `ML/results/threshold_analysis_random_forest.csv` | τ-Analyse RF: TP/FP/FN/TN, Precision, Recall, F1, Kosten |

### Modell-Konfiguration

| Parameter | Wert | Begründung |
|---|---|---|
| Features genutzt | 22 von 25 | AimAngularErrorMean/StdDev, HeadshotRate ausgeschlossen (dauerhaft 0) |
| Train/Test Split | 80/20 stratifiziert | Klassenverteilung 95/5 bleibt erhalten |
| Cross-Validation | 5-Fold Stratified | Robuste Schätzung bei kleiner Cheater-Klasse |
| class_weight | `balanced` | Kompensiert 95/5 Imbalance |
| C_FP | 1 | Kosten: legitimer Spieler fälschlich gebannt |
| C_FN | 10 | Kosten: Cheater unentdeckt — 10× teurer |

### τ*-Analyse Ergebnisse (cost_threshold_analysis.py)

| C_FN/C_FP | τ* | FP | FN |
|---|---|---|---|
| 1:1 | 0.60 | 40 | 118 |
| 1:2 | 0.53 | 120 | 63 |
| 1:5 | 0.50 | 175 | 52 |
| 1:10 | 0.48 | 242 | 40 |
| 1:20 | 0.44 | 415 | 26 |
| 1:50 | 0.40 | 676 | 17 |

**Interpretation:** Je teurer ein unentdeckter Cheater (steigende C_FN), desto niedriger wird τ* — der Klassifikator akzeptiert mehr False Positives um keine Cheater zu verpassen. Das ist der empirische Kernbeitrag der Bachelorarbeit (Kap. 9).

### Top-5 Feature Importances (Random Forest)

| Feature | Importance |
|---|---|
| SpeedViolationRatio | 0.0708 |
| MovementSpeedMean | 0.0597 |
| HitRate | 0.0451 |
| ShotIntervalStdDev | 0.0318 |
| ReactionTimeStdDev | ~0.030 |

---

## Schritt 4 – L2-Regularisierung & λ-Tuning ✅ (Abgeschlossen: 08.04.2026)

### Dateien

| Datum | Datei | Beschreibung |
|---|---|---|
| 08.04.2026 | `ML/regularization_analysis.py` | Regularisierungspfad λ ∈ {0.001–1000}, GridSearchCV für optimales λ, Gewichtspfad, FP/FN vs. λ |
| 08.04.2026 | `ML/results/regularization_plots.png` | 4 Plots: λ vs. AUC, λ vs. Kosten, Gewichtspfad Top-8 Features, FP/FN vs. λ |
| 08.04.2026 | `ML/results/regularization_path.csv` | AUC, Kosten, FP, FN je λ-Wert |
| 08.04.2026 | `ML/results/gridsearch_cv_results.csv` | 5-Fold CV Ergebnisse für alle C-Werte |
| 08.04.2026 | `ML/results/model_lr_regularized.pkl` | LR-Modell mit optimalem λ=1.0 (joblib) |

### Ergebnisse

| λ | C | AUC | Kosten | Interpretation |
|---|---|---|---|---|
| 0.001 | 1000 | 1.0000 | 10 | Keine Regularisierung → 1 FN |
| 0.1–5.0 | 10–0.2 | 1.0000 | 0 | Optimaler Bereich |
| **1.0** | **1** | **1.0000** | **0** | **Optimales λ (GridSearchCV)** |
| 10.0 | 0.1 | 1.0000 | 2 | Zu stark → erste FP entstehen |
| 100.0 | 0.01 | 0.9994 | 24 | Overfitting in Gegenrichtung |
| 1000.0 | 0.001 | 0.9953 | 70 | Stark überregularisiert |

**Optimales λ = 1.0** (per 5-Fold GridSearchCV, CV AUC = 0.9996)

### Bezug zur Bachelorarbeit

- **Kap. 6 (Regularisierung):** Empirischer Beleg für J(w) = Loss + λ·||w||² — Gewichtspfad zeigt wie Koeffizienten mit steigendem λ gegen 0 schrumpfen
- **Kap. 9 (Diskussion):** Zu wenig Regularisierung (λ→0) führt zu FN (Cheater übersehen); zu viel (λ→∞) zu FP (legitime Spieler gebannt) — Kostenasymmetrie macht optimales λ wichtig

---

## Schritt 5 – UE5 Integration (Ausstehend)

Geplant: FastAPI Inferenz-Server, UE5 sendet Session-Features per HTTP → bekommt Wahrscheinlichkeit + Flag zurück.
