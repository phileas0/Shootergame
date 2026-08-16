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
* **Respawn-Artefakt (behoben):** Ein frisch gespawntes Pawn initialisierte `LastViewDirection` mit seinem Default-Vorwärtsvektor. Der erste Tick verglich diesen gegen die tatsächliche Blickrichtung, wodurch **jeder Respawn einen Schein-Flip** erzeugte. Nachweis über zwei Messreihen: völlig inaktive Spieler (0 Schüsse, 0 Bewegung) hatten bei $0$ Toden `AimFlipRatio` $= 0{,}0000$, bei $3$ Toden dagegen $0{,}0022$–$0{,}0044$ sowie `AimAngularSpeedStdDev` von $253$–$398$. Da der Grenzwert der Aimbot-Regel bei $1{,}1\%$ liegt, hätte ein oft sterbender Spieler ohne jeden Cheat in dessen Nähe geraten können. Behoben über `bHasAimReference` / `bHasMoveReference`: der erste Tick nach einem (Re-)Spawn setzt nur noch die Referenz, ohne eine Differenz zu werten. **Verifikation (Messreihe vom 11.08.2026):** inaktive Spieler mit $1$, $2$ und $3$ Toden zeigen jetzt durchgängig `AimFlipRatio` $= 0{,}0000$; die Schein-Flips sind damit vollständig beseitigt. Ein **Restartefakt** bleibt: dieselben Spieler weisen `AimAngularSpeedStdDev` von $26$–$32$ auf (vorher $253$–$398$, also um rund den Faktor $10$ reduziert), Spieler ohne Tode dagegen exakt $0{,}0000$. Um den Sterbevorgang herum entstehen also weiterhin kleine Blickrichtungsänderungen (vermutlich Todesanimation/Kameraübergang), die unterhalb der $90^\circ$-Flip-Schwelle liegen. Für die Aimbot-Regel ist das unkritisch, für eine spätere Verwendung von `AimAngularSpeedStdDev` als ML-Feature ist es als bekannte Störgröße zu berücksichtigen.
* **AimAngularError:** Wird bei **jedem Schuss** in `RecordShot()` über `SampleAimError()` erfasst. Gemessen wird der Winkel zwischen der Blickrichtung und der Richtung zum Ziel, das dem Fadenkreuz am nächsten liegt. Gewertet wird ein Ziel nur, wenn es
  * innerhalb von `EnemyCheckDistance` ($5000\text{ cm}$) liegt,
  * innerhalb des Kegels `AimErrorMaxAngle` ($45^\circ$) liegt und
  * eine freie Sichtlinie besitzt (Line-Trace auf `ECC_Visibility`).

  Schüsse ohne passendes Ziel (z. B. Sprayen gegen eine Wand) erzeugen bewusst **keine** Stichprobe und verwässern die Verteilung daher nicht.
* **Mehrpunkt-Messung (Korrektur nach erstem Aimbot-Test):** Der Winkel wird als **Minimum über mehrere Körperpunkte** des Ziels bestimmt (Kapselmitte/Torso und Kopfknochen), nicht mehr nur gegen die Kapselmitte. Grund: im ersten A/B-Test lieferte ein nachweislich aktiver Aimbot `AimAngularErrorMean` $= 10{,}31^\circ$ und lag damit **mitten im Bereich sauberen Spiels** ($6{,}07$–$25{,}61^\circ$) — das Feature trennte nicht. Erklärung: rastet der Aimbot auf den Kopf, während gegen die Kapselmitte gemessen wird, entsteht ein systematischer Versatz von rund $10^\circ$ bei $\approx 3{,}4\text{ m}$ Distanz. Über das Minimum mehrerer Körperpunkte ergibt "Fadenkreuz irgendwo auf dem Gegner" nun annähernd $0^\circ$, unabhängig vom anvisierten Körperteil.

* **Validierung der Korrektur (Messreihe vom 11.08.2026):** Die vor dem Test formulierte Hypothese — der gemessene Fehler des Aimbots muss deutlich sinken, während sauberes Spiel klar darüber bleibt — wurde bestätigt:

| Merkmal | Sauberes Spiel | Aimbot |
| :--- | :--- | :--- |
| `AimAngularErrorMean` | $17{,}16^\circ$ | $3{,}59^\circ$ |
| `AimAngularErrorStdDev` | $10{,}14^\circ$ | $6{,}34^\circ$ |
| `AimErrorSampleCount` / `TotalShots` | $44/219 = 0{,}201$ | $160/241 = 0{,}664$ |

  Vor der Korrektur lag der Aimbot bei $10{,}31^\circ$ und damit innerhalb des sauberen Bereichs; nach der Korrektur beträgt der Abstand rund den **Faktor 4,8**. Als zusätzliches Nebenergebnis zeigt sich, dass nicht nur der Fehler selbst, sondern auch der **Anteil der Schüsse mit einem Ziel im Kegel** (`AimErrorSampleCount / TotalShots`) trennt: der Aimbot richtet zwei Drittel seiner Schüsse auf ein sichtbares Ziel, der menschliche Spieler nur ein Fünftel.

* **Einschränkung dieser Messung:** Die beiden Läufe sind kein vollständig kontrollierter Vergleich. Der saubere Spieler erzielte $0$ Treffer bei $219$ Schüssen, schoss also überwiegend ins Leere, während der Aimbot-Lauf gezielt geführt wurde. Ein Teil des gemessenen Abstands geht daher auf unterschiedliche Spielabsicht und nicht allein auf den Cheat zurück. Für die Datenerhebung der ML-Trainingsdaten ist deshalb ein Protokoll nötig, das beide Bedingungen mit **derselben Spielabsicht** (aktives Zielen auf Gegner) erhebt.
* **AimErrorSampleCount:** Zählt die Schüsse, die tatsächlich eine Stichprobe geliefert haben. Notwendig zur Unterscheidung von *"perfektes Zielen"* (Fehler $\approx 0$) und *"nie auf jemanden geschossen"* (Fehler ebenfalls $0$, weil keine Stichproben vorliegen) — ohne diese Spalte wäre ein passiver Spieler von einem Aimbot nicht unterscheidbar.
* **Messgenauigkeit (Limitation):** `GetBaseAimRotation()` nutzt für entfernte Clients die replizierte `RemoteViewPitch`, die auf 1 Byte komprimiert ist ($\approx 1{,}4^\circ$ Auflösung im Pitch). Der Winkelfehler ist bei Remote-Clients daher grobkörniger als beim Listen-Server-Host. Für die Trennung Aimbot ($\approx 0^\circ$) vs. Mensch (mehrere Grad) ist die Auflösung ausreichend.

#### B. Bewegungsverhalten (Movement-Features)
* **Horizontale Auswertung (wichtig):** Alle Bewegungsfeatures werden aus der **Bodenebene** (`Velocity.X`, `Velocity.Y`) berechnet, nicht aus dem vollen 3D-Vektor. Grund: bei $\approx 980\text{ cm/s}^2$ Gravitation überschreitet jeder Sprung oder Fall bereits nach knapp $0{,}6\text{s}$ das Bodenlimit von $600\text{ cm/s}$ allein durch die Z-Komponente. Eine Messreihe mit sauberem Spiel (viel Springen) erreichte so `MovementSpeedMax` $= 1094\text{ cm/s}$ und `SpeedViolationRatio` $= 0{,}20$, ohne dass ein Cheat aktiv war. Da Speedhacks auf die Bodenbewegung wirken und die Fallgeschwindigkeit für alle Spieler identisch ist, wird die Vertikale nicht gewertet.
* **MovementSpeeds:** Speichert jeden Tick die aktuelle **horizontale** Geschwindigkeit.
* **SpeedViolationRatio:** Vergleicht die horizontale Geschwindigkeit mit dem Limit von $600\text{ cm/s}$. Jeder Tick mit $>610\text{ cm/s}$ zählt als Verstoß.
* **DirectionChangesPerSecond:** Zählt Richtungsänderungen von $>45^\circ$ im **horizontalen** Bewegungsvektor zwischen zwei Ticks. Die Horizontale ist auch hier nötig: am Sprungscheitel wechselt `Velocity.Z` das Vorzeichen, was im 3D-Vektor als starke Richtungsänderung erschiene, obwohl die Laufrichtung unverändert bleibt.
* **MovementPathEntropy:** Ermittelt die Shannon-Entropie der Bewegungsrichtungen in 8 Richtungsboxen (Chaotisches vs. geradliniges Bewegen).

#### C. Reaktionszeiten (ReactionTime)
* **Sichtbarkeits-Trace:** Ein Line-Trace tastet alle $0,2\text{s}$ das Sichtfeld des Spielers ab. Trifft er einen gegnerischen NPC, wird `RecordEnemyVisible()` aufgerufen und ein Timer gestartet (`bWaitingForReactionShot = true`).
* **Messung:** Schießt der Spieler innerhalb von $5$ Sekunden, wird die Reaktionszeit aufgezeichnet. Nach $5$ Sekunden oder nach dem Schuss wird der Timer zurückgesetzt.
* **Limitation (Verzerrung nach unten):** Gemessen wird der Abstand *"Gegner betritt den Trace"* $\rightarrow$ *"nächster Schuss"*. Feuert ein Spieler ohnehin dauerhaft und läuft ein Gegner in sein Fadenkreuz, ist dieser Abstand nahezu null — das ist jedoch keine Reaktion, sondern ein Zufall. Sprayendes Spielverhalten verzerrt das Feature daher systematisch nach unten. In einer Messreihe mit sauberem Spiel erreichte ein Spieler `ReactionTimeMean` $= 70\text{ms}$ und lag damit **unter** dem Grenzwert `LIMIT_REACTION_MEAN` ($80\text{ms}$). Ein Fehlalarm wurde nur dadurch verhindert, dass die Triggerbot-Regel zusätzlich eine niedrige `ReactionTimeStdDev` fordert (`AND`-Verknüpfung, hier $96\text{ms}$). Die Standardabweichung ist bei diesem Feature also das belastbarere Kriterium als der Mittelwert.

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

> **Hinweis:** Die folgenden Grenzwerte sind die von Hand gesetzten Standardwerte. Sie stammen
> aus einer einzelnen frühen Sitzung und generalisieren nicht — die datenbasiert kalibrierten
> Werte stehen weiter unten. Seit dem 16.08.2026 nimmt `analyze_player(row, limits=None)` die
> Grenzwerte optional als Parameter entgegen; ohne Angabe gilt unverändert die Tabelle hier.

| Regel | Grenzwert | Risiko | Cheat-Typ / Beschreibung |
|---|---|---|---|
| **INSANE_RAPID_FIRE** | $\text{aSPS} > 20.0$ | +50 % | Extremer Rapid-Fire-Hack / Makro |
| **RAPID_FIRE** | $\text{aSPS} > 12.0$ | +40 % | Unmenschlich schnelle Schussrate bei Semi-Auto |
| **TRIGGERBOT_REACTION** | $\text{RTMean} < 80\text{ms}$ & $\text{RTStdDev} < 15\text{ms}$ | +40 % | Triggerbot (Unmenschlich schnelle, absolut konstante Reaktion) |
| **MECHANICAL_AUTOFIRE** | $\text{aSPS} > 5.0$ & $\text{IntervalStdDev} < 15\text{ms}$ | +30 % | Autoclicker/Makro (Nahezu null Varianz bei hoher Frequenz). *Hinweis: Vollauto-Waffen lösen dies regulär als "SUSPICIOUS" aus.* |
| **AIM_SNAPPING** | $\text{AimFlipRatio} > 1.1\ \%$ | +25 % | Aimbot (Häufige Blick-Teleportationen um $>90^\circ$ in 100ms) |
| **AIMBOT_LOCK** | $\text{AimErrMean} < 2.5^\circ$ & $n \ge 20$ | +50 % | Aimbot (praktisch perfekte Rastung auf das Ziel) |
| **AIMBOT_PRECISION** | $\text{AimErrMean} < 6.0^\circ$ & $n \ge 20$ | +35 % | Aimbot (Zielgenauigkeit deutlich unter menschlichem Niveau) |
| **ON_TARGET_RATIO** | $\text{AimErrSamples}/\text{Shots} > 45\ \%$ & $\text{Shots} \ge 30$ | +25 % | Aimbot (feuert kaum ins Leere, weil nur bei erfasstem Ziel geschossen wird) |
| **SPEED_LIMIT_EXCEEDED** | $\text{SpeedMax} > 1500\text{ cm/s}$ | +30 % | Speedhack (Charakter bewegte sich mit unmöglicher Geschwindigkeit) |
| **SPEED_VIOLATION_RATIO** | $\text{ViolationRatio} > 35\ \%$ | +25 % | Dauerhafter Speedhack (Spieler läuft $>35\%$ der Zeit zu schnell) |
| **GOD_MODE** | $\text{Kills} \ge 20$ & $\text{Deaths} == 0$ | +60 % | God Mode Cheat (Unsterblichkeit + aktives Killsammeln) |

#### Anmerkungen zu den Aimbot-Regeln

* **Stichproben-Schranke ($n \ge 20$) als Schutz vor Fehlalarmen:** `AimAngularErrorMean` ist auch dann $0{,}0$, wenn ein Spieler *nie* auf ein sichtbares Ziel geschossen hat, weil in diesem Fall überhaupt keine Stichprobe erzeugt wird. Ohne die Schranke über `AimErrorSampleCount` würde ausgerechnet der harmloseste Fall — ein Spieler, der nur ins Leere feuert — als perfekt zielender Aimbot gewertet. Die Schranke unterscheidet "perfektes Zielen" von "nie gezielt".

> **Korrigiert am 16.08.2026 nach der Erhebung von 72 Sitzungen (296 Spielerzeilen, 8 Personen).**
> Die zuvor an dieser Stelle dokumentierten Grenzwerte und die daraus abgeleiteten Aussagen
> beruhten auf je einer einzelnen Messreihe pro Klasse und haben sich auf breiter Datenbasis
> als nicht haltbar erwiesen. Der ursprüngliche Abschnitt ist unten als *widerlegt* markiert
> erhalten, damit der Erkenntnisweg nachvollziehbar bleibt.

#### Was die breite Datenbasis ergeben hat

Gemessen über 296 Spielerzeilen (nur Zeilen mit $n \ge 20$ Zielfehler-Stichproben):

| Merkmal | saubere Runden | Cheat-Runden | trennt? |
| :--- | ---: | ---: | :--- |
| `AimAngularErrorMean` | $9{,}47^\circ$ | $8{,}19^\circ$ | nein |
| On-Target-Quote | $73{,}4\ \%$ | $73{,}3\ \%$ | nein |

Der schlechteste reine Aimbot-Lauf hatte $8{,}4^\circ$ Zielfehler — **104 von 266 sauberen
Runden waren genauer**. Es existiert kein Schwellenwert, der beide Klassen trennt.

**Damit ist die frühere Annahme widerlegt**, die On-Target-Quote sei ein von der
Zielgenauigkeit unabhängiges Aimbot-Signal. Beide Merkmale trennen auf realen Daten nicht,
und zwar unabhängig voneinander. Die ursprünglich angenommene Normalquote von ca. $20\ \%$
war ein Artefakt einer einzelnen Sitzung; der tatsächliche Wert liegt bei $73\ \%$.

Praktische Folge: die Regel `ON_TARGET_RATIO` (Auslösung ab $45\ \%$) markiert auf der
Verdachtsstufe **264 von 268 sauberen Spielern**. Die Stufe `SUSPICIOUS` ist mit den
ursprünglichen Grenzwerten wertlos.

Vermutete Ursache für die fehlende Trennung: die Telemetrie aggregiert eine ganze Runde zu
einem Mittelwert. War der Aimbot nur zeitweise aktiv — was beim Spielen der Normalfall ist —
verschwindet der Effekt im Durchschnitt der übrigen Spielzeit. Ein kürzeres Messfenster
(z. B. 15 s) wäre die naheliegende Gegenmaßnahme, lässt sich aus den vorhandenen Aggregaten
aber nicht nachträglich bilden.

#### Datenbasierte Kalibrierung

`ML/calibrate_rules.py` bestimmt die Grenzwerte aus den Daten statt von Hand. Verfahren:
personengruppierte Kreuzvalidierung (`StratifiedGroupKFold` über `Person`, 10 Faltenaufteilungen).
Je Trainingsfalte wird jede Regel so gesetzt, dass sie höchstens den Anteil $\alpha$ der
**sauberen** Spieler auslöst; $\alpha$ und die Risk-Schwelle werden gemeinsam kostenminimal
gewählt ($C_{FP} = 1$, $C_{FN} = 10$). Bewertet wird ausschließlich auf der Testfalte.

Damit gibt es statt zwölf frei gesetzten Schwellen nur zwei Parameter — das begrenzt die
Überanpassung an die kleine Stichprobe. Gewählt wurde im Median $\alpha = 0{,}01$.

| Regel | von Hand | kalibriert (Median) | Streuung |
| :--- | ---: | ---: | ---: |
| `RAPID_FIRE` | $12{,}0$ | $9{,}60$ | $0{,}25$ |
| `RAPID_FIRE_HIGH` | $20{,}0$ | $9{,}95$ | $0{,}16$ |
| `REACTION_MEAN` | $0{,}080$ | $0{,}021$ | $0{,}009$ |
| `REACTION_STD` | $0{,}015$ | $0{,}017$ | $0{,}007$ |
| `INTERVAL_STD` | $0{,}015$ | $0{,}006$ | $0{,}014$ |
| `AIM_FLIP_RATIO` | $0{,}011$ | $0{,}0023$ | $0{,}0005$ |
| `AIM_ERROR` | $6{,}0^\circ$ | $4{,}16^\circ$ | $0{,}37$ |
| `AIM_ERROR_HIGH` | $2{,}5^\circ$ | $3{,}37^\circ$ | $0{,}26$ |
| `ON_TARGET_RATIO` | $45\ \%$ | $90{,}1\ \%$ | $2{,}6$ |
| `SPEED_MAX` | $1500$ | $903$ | $102$ |
| `SPEED_VIOLATION` | $35\ \%$ | $0{,}16\ \%$ | $0{,}02$ |
| `GOD_KILLS` | $20$ | $20$ (unverändert) | $0$ |

Zwei Werte bedürfen einer Erklärung:

* **`SPEED_VIOLATION` bei $0{,}16\ \%$** wirkt extrem, ist aber korrekt: $92{,}9\ \%$ der
  sauberen Spieler haben exakt $0$. Jede messbare Geschwindigkeitsverletzung ist tatsächlich
  auffällig.
* **`GOD_KILLS` blieb bei $20$**, weil es nur **zwei** saubere Zeilen mit null Toden gibt.
  Aus zwei Beobachtungen lässt sich keine Schwelle ableiten; die Sperre im Skript
  (mindestens 10 saubere Beobachtungen) hat korrekt gegriffen. Das ist eine dokumentierte
  Grenze des Verfahrens, kein Fehler — und es erklärt, warum der kalibrierte Detektor
  God-Mode weiterhin verfehlt.

#### Leistung auf den 296 Zeilen

| Variante | Recall | Precision | Kosten |
| :--- | ---: | ---: | ---: |
| Handkalibrierung, `Risk >= 50` | $42{,}9\ \%$ | $38{,}7\ \%$ | $179$ |
| datenbasiert kalibriert (out-of-fold) | $60{,}7\ \%$ | $43{,}6\ \%$ | $132$ |
| ML-Modell (Random Forest, out-of-fold) | $75{,}0\ \%$ | $50{,}0\ \%$ | $91$ |

Die Kalibrierung verbessert den Regeldetektor deutlich. Erst dieser Wert darf dem ML-Modell
gegenübergestellt werden — der Vergleich mit der Handkalibrierung wäre ein Vergleich der
Kalibrierung, nicht der Verfahren.

Nach der Kalibrierung erkennt der Regeldetektor **keinen einzigen Fall, den das ML-Modell
übersieht**. Eine Kombination beider Verfahren bringt entsprechend nichts ($92$ statt $91$
Kosten). Beide stützen sich auf dieselben Merkmale und scheitern an denselben Fällen.

#### ~~Validierung auf dem vorhandenen Datenbestand~~ (widerlegt, 16.08.2026)

> Der folgende Abschnitt beruhte auf vier Sitzungen eines einzigen Spielers. Die dort
> beobachtete Trennung ($17{,}16^\circ$ gegen $3{,}59^\circ$) hat sich auf 296 Zeilen nicht
> bestätigt — siehe oben. Erhalten als Dokumentation des Erkenntniswegs.

| Sitzung | Zustand | `AimErrMean` | On-Target | Urteil |
| :--- | :--- | ---: | ---: | :--- |
| `2026-08-06_13-28-08` | sauber | $25{,}61^\circ$ | $24{,}6\ \%$ | CLEAN ($0\ \%$) |
| `2026-08-06_13-33-24` | Aimbot | $10{,}31^\circ$ | $80{,}4\ \%$ | CHEATER ($50\ \%$) |
| `2026-08-11_16-16-58` | sauber | $17{,}16^\circ$ | $20{,}1\ \%$ | CLEAN ($0\ \%$) |
| `2026-08-11_16-18-15` | Aimbot | $3{,}59^\circ$ | $66{,}4\ \%$ | CHEATER ($60\ \%$) |

Damals bereits vermerkt: $n = 4$ Sitzungen von einem einzigen Spieler sind eine
Plausibilitätsprüfung, **keine Evaluation**. Diese Einschränkung hat sich als
zutreffend erwiesen — die vier Sitzungen waren nicht repräsentativ.

### 2.3 Risikobewertung & Urteil
* **`Risk >= 50%`** $\rightarrow$ **`! CHEATER`** (Sicheres Vergehen, z. B. God Mode alleine oder Rapid-Fire + Aim Snapping)
* **`Risk >= 25%`** $\rightarrow$ **`? SUSPICIOUS`** (Verdächtiges Verhalten für Log-Audits, z. B. konstantes Dauerfeuer mit Vollautomatik-Waffen)

> **Korrektur 16.08.2026 zur Stufe `SUSPICIOUS`:** Mit den handgesetzten Grenzwerten markiert
> diese Stufe auf den 296 real erhobenen Zeilen **264 von 268 sauberen Spielern** bei einem
> Recall von $100\ \%$. Sie trägt damit keine Information und ist in dieser Form unbrauchbar.
> Ursache ist die Regel `ON_TARGET_RATIO`, deren Schwelle von $45\ \%$ weit unter dem
> tatsächlichen Normalwert von $73\ \%$ liegt. Mit den kalibrierten Grenzwerten entfällt das
> Problem; die kostenoptimale Risk-Schwelle liegt dann bei $5\ \%$, was faktisch bedeutet:
> auffällig, sobald eine einzelne Regel auslöst.
* **`Risk < 25%`** $\rightarrow$ **`CLEAN`** (Unauffälliges Verhalten)

### 2.4 Terminal-Kompatibilität
Das Skript ist vollständig ASCII-kompatibel (`-+-` statt Sonderzeichen). Es läuft ohne Abstürze in Windows-Systemen unter Standardcodierungen (wie CP1252) und gibt eine sauber formatierte Tabelle im Terminal aus.
