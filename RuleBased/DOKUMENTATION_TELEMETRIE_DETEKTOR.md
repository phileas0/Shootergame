# Dokumentation: Telemetrie-System & Cheat-Detektor

Diese Dokumentation beschreibt die Funktionsweise der gesamten Cheat-Erkennungs-Pipeline, aufgeteilt in die serverseitige **UE5-C++ Telemetrie-Erfassung** und den **regelbasierten Python-Detektor**.

---

## Teil 1: Das C++ Telemetrie-Erfassungssystem (UE5)

Das System besteht aus drei C++ Hauptkomponenten:
1. `AShooterGameMode` (Steuert die Sessions und das Zusammenführen)
2. `UTelemetryCollector` (Erfasst die Rohdaten pro Charakter-Lebensspanne)
3. `UTelemetryLogger` (Schreibt die finalisierten Daten als CSV-Dateien)

### 1.1 Auslösung und Filterung von Schüssen (`AShooterGameMode`)
Jedes Mal, wenn ein Spieler im Spiel eine Waffe abfeuert, spuckt der Code ein Projektil (Bullet) in die Spielwelt.
* **OnActorSpawned-Delegat:** In `AShooterGameMode::OnActorSpawned` fängt der Server jedes gespawnte Objekt ab.
* **Filterung:**
  * Klassen wie `AController` (Controller) und `AInfo` (Spielleiter-Daten) werden übersprungen.
  * Waffen-Blueprints (z. B. `BP_ShooterWeapon_Rifle_C`) werden ignoriert, damit das Ausrüsten oder Ablegen einer Waffe nicht als Schuss gezählt wird.
  * Der Verursacher (`InstigatorPawn`) wird ermittelt.
  * Befindet sich das Spiel in der aktiven Phase (`InProgress`), wird der `UTelemetryCollector` des Spielers aufgerufen: `Collector->RecordShot()`.

### 1.2 Datensammlung (`UTelemetryCollector`)
Der TelemetryCollector läuft als Actor-Component auf dem Charakter-Pawn und tastet das Verhalten des Spielers im `TickComponent` mit **10 Hz (alle 0,1 Sekunden)** ab:

#### A. Blickverhalten (Aim-Features)
* **AimAngularSpeed:** Berechnet die Winkelgeschwindigkeit der Blickrichtungsänderung pro Sekunde:
  $$\text{Speed} = \frac{\text{Winkeländerung in Grad}}{\text{DeltaTime}}$$
* **AimFlipRatio:** Zählt, wie oft sich der Blickwinkel in einem einzigen Tick um mehr als $90^\circ$ gedreht hat (Indikator für sprunghaftes Aimbot-Snapping).
* **Limitation (Pipeline-Bug):** Die Spalten `AimAngularErrorMean` und `AimAngularErrorStdDev` verbleiben konsistent auf `0.0`. Die zugehörige Liste `AimAngularErrors` wird im C++ Code deklariert, aber an keiner Stelle mit Werten befüllt.

#### B. Bewegungsverhalten (Movement-Features)
* **MovementSpeeds:** Speichert jeden Tick die aktuelle Geschwindigkeit.
* **SpeedViolationRatio:** Vergleicht die Geschwindigkeit mit dem Limit von $600\text{ cm/s}$. Jeder Tick mit $>610\text{ cm/s}$ zählt als Verstoß.
* **DirectionChangesPerSecond:** Zählt Richtungsänderungen von $>45^\circ$ im Bewegungsvektor zwischen zwei Ticks.
* **MovementPathEntropy:** Ermittelt die Shannon-Entropie der Bewegungsrichtungen in 8 Richtungsboxen (Chaotisches vs. geradliniges Bewegen).

#### C. Reaktionszeiten (ReactionTime)
* **Sichtbarkeits-Trace:** Ein Line-Trace tastet alle $0,2\text{s}$ das Sichtfeld des Spielers ab. Trifft er einen gegnerischen NPC, wird `RecordEnemyVisible()` aufgerufen und ein Timer gestartet (`bWaitingForReactionShot = true`).
* **Messung:** Schießt der Spieler innerhalb von $5$ Sekunden, wird die Reaktionszeit aufgezeichnet. Nach $5$ Sekunden oder nach dem Schuss wird der Timer zurückgesetzt.

#### D. Schuss- & Trefferverhalten (Rates)
* **RecordShot:** Inkrementiert `TotalShots` und misst den Zeitabstand zwischen Schüssen (`ShotIntervals`).
* **RecordHit (OnTakeAnyDamage):** Wenn ein Gegner Schaden nimmt, wird der Verursacher gesucht und auf dessen Collector `RecordHit()` aufgerufen.
* **Dedup-Schutz:** Treffer desselben Projektils innerhalb von $300\text{ms}$ werden ignoriert, um Mehrfachtreffer-Wertungen durch Projektil-Überlappung zu verhindern.
* **Kopfschüsse (Pipeline-Bug):** Die Spalte `HeadshotRate` verbleibt konsistent auf `0.0` in den CSVs. Der Knochenname des Treffers ist im `OnTakeAnyDamage`-Delegat der Engine standardmäßig nicht direkt verfügbar, weshalb im C++ Code immer `RecordHit(false)` (kein Kopfschuss) aufgerufen wird.

### 1.3 Lebenszeit und Merging-Logik
* **Lebenszeit-Definition (`SessionDurationSeconds`):** Da der Collector auf dem Charakter-Pawn liegt, erfasst er nur die Zeit, in der das Pawn existiert und aktiv tickt (reine **Alive-Time**). Todzeiten oder Ladebildschirme fließen nicht ein.
* **Merging:** Stirbt ein Spieler oder spawnt neu, wird sein alter Pawn-Collector ausgelesen und dessen Rohdaten über `MergeTelemetry()` an die persistente Collector-Komponente im `PlayerState` angehängt.
* **CSV-Export:** Am Ende der Runde (`EndPlay`) werden die Rohdaten aggregiert (Mittelwerte und Standardabweichungen berechnet) und über den `TelemetryLogger` im Pfad `Saved/Telemetry/session_<timestamp>.csv` gespeichert.

---

## Teil 2: Der regelbasierte Python Cheat-Detektor

Das Skript `rule_based_detector.py` analysiert gesammelte CSV-Dateien offline und stuft Spieler anhand biologischer und physikalischer Grenzwerte in Risikoklassen ein.

### 2.1 Echte Schussfrequenz: Die aSPS-Metrik
Da `ShotsPerSecond` in der C++ Telemetrie durch die gesamte Session-Dauer geteilt wird, verwässern lange Laufpausen das Bild. Ein Cheater mit einem kurzen Rapid-Fire-Burst würde unentdeckt bleiben.
Das Skript berechnet daher die **aktive Schussrate (`aSPS`)** aus dem Kehrwert des mittleren Schussabstands:
$$\text{aSPS} = \frac{1.0}{\text{ShotIntervalMean}} \quad (\text{falls } \text{ShotIntervalMean} > 0)$$

### 2.2 Die Erkennungsregeln

Das Skript vergibt bei Regelverletzungen Risikopunkte (max. 100 %):

| Regel | Grenzwert | Risiko | Cheat-Typ / Beschreibung |
|---|---|---|---|
| **INSANE_RAPID_FIRE** | $\text{aSPS} > 20.0$ | +50 % | Extremer Rapid-Fire-Hack / Makro |
| **RAPID_FIRE** | $\text{aSPS} > 12.0$ | +40 % | Unmenschlich schnelle Schussrate bei Semi-Auto |
| **TRIGGERBOT_REACTION** | $\text{RTMean} < 80\text{ms}$ & $\text{RTStdDev} < 15\text{ms}$ | +40 % | Triggerbot (Unmenschlich schnelle, absolut konstante Reaktion) |
| **MECHANICAL_AUTOFIRE** | $\text{aSPS} > 5.0$ & $\text{IntervalStdDev} < 15\text{ms}$ | +30 % | Autoclicker/Makro (Nahezu null Varianz bei hoher Frequenz). *Hinweis: Vollauto-Waffen lösen dies regulär als "SUSPICIOUS" aus.* |
| **AIM_SNAPPING** | $\text{AimFlipRatio} > 1.1\ \%$ | +25 % | Aimbot (Häufige Blick-Teleportationen um $>90^\circ$ in 100ms) |
| **SPEED_LIMIT_EXCEEDED** | $\text{SpeedMax} > 1500\text{ cm/s}$ | +30 % | Speedhack (Charakter bewegte sich mit unmöglicher Geschwindigkeit) |
| **SPEED_VIOLATION_RATIO** | $\text{ViolationRatio} > 35\ \%$ | +25 % | Dauerhafter Speedhack (Spieler läuft $>35\%$ der Zeit zu schnell) |
| **GOD_MODE** | $\text{Kills} \ge 20$ & $\text{Deaths} == 0$ | +60 % | God Mode Cheat (Unsterblichkeit + aktives Killsammeln) |

### 2.3 Risikobewertung & Urteil
* **`Risk >= 50%`** $\rightarrow$ **`! CHEATER`** (Sicheres Vergehen, z. B. God Mode alleine oder Rapid-Fire + Aim Snapping)
* **`Risk >= 25%`** $\rightarrow$ **`? SUSPICIOUS`** (Verdächtiges Verhalten für Log-Audits, z. B. konstantes Dauerfeuer mit Vollautomatik-Waffen)
* **`Risk < 25%`** $\rightarrow$ **`CLEAN`** (Unauffälliges Verhalten)

### 2.4 Terminal-Kompatibilität
Das Skript ist vollständig ASCII-kompatibel (`-+-` statt Sonderzeichen). Es läuft ohne Abstürze in Windows-Systemen unter Standardcodierungen (wie CP1252) und gibt eine sauber formatierte Tabelle im Terminal aus.
