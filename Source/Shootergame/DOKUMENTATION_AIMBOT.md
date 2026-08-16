# Dokumentation: `UAimbotComponent`

Kontrollierter Aimbot als **Messinstrument** für die kontrollierte Datenerhebung. Die Komponente
erzeugt gelabelte Cheat-Sitzungen, gegen die der regelbasierte Detektor und das ML-Modell
evaluiert werden.

Quellen: `Source/Shootergame/Public/AimbotComponent.h`, `Source/Shootergame/Private/AimbotComponent.cpp`

---

## 1. Warum die Komponente existiert

Die vorherige Blueprint-Variante bestimmte das Ziel automatisch als *nächstgelegenen Spieler*.
Für eine Datenerhebung ist das unbrauchbar:

* **Reproduzierbarkeit.** Bei "nächster Spieler" springt das Ziel bei jeder Bewegung um. In den
  Telemetriedaten vermischen sich dadurch Zielwechsel und Zielverfolgung, und zwei Runden mit
  identischer Cheat-Konfiguration erzeugen unterschiedliche Merkmalsausprägungen.
* **Auswertbarkeit.** Ein explizit gewähltes und festgehaltenes Ziel erzeugt vergleichbare
  Sitzungen.

Zusätzlich löst die Komponente ein Problem, das in der Arbeit ursprünglich als Ausschlussgrund
für die Aimbot-Evaluierung dokumentiert war: die Projektil-Flugzeit des UE5-Arena-Shooter-Templates
verkehrte automatisierte Zielsysteme ins Gegenteil. Die Komponente setzt die Blickrichtung direkt
über `SetControlRotation()` und ist von der Projektilphysik unabhängig.

---

## 2. Aufbau

### Zielauswahl

`BuildTargetList()` sammelt alle `ACharacter` der Welt und filtert über `IsValidTarget()`:

| Prüfung | Begründung |
| :--- | :--- |
| `IsValid(Target)` | ausgeschlossen: zerstörte Actors |
| `Target != Owner` | nicht auf sich selbst zielen |
| `HasAuthority() && !GetController()` | Controller ist **nur auf dem Server** gesetzt. Auf einem Client sind die Controller fremder Pawns nicht repliziert und immer `nullptr` — die Prüfung darf deshalb ausschließlich mit Authority laufen, sonst findet ein cheatender Client kein einziges Ziel. |
| `bPlayersOnly && !GetPlayerState()` | Der `PlayerState` wird zu allen Clients repliziert und ist damit die verlässliche Prüfung auf "aktiver Spieler" auf beiden Seiten. |
| Entfernung `<= MaxTargetDistance` | Reichweitenbegrenzung, `0` = unbegrenzt |

**Bewusst nicht geprüft wird hier die Sichtlinie.** `IsValidTarget()` beantwortet
*"ist das ein wählbares Ziel"*, nicht *"sehe ich es gerade"*. Die Sichtlinie flackert bei jeder
Deckung im Frame-Takt; stünde sie in der Zielauswahl, fiele das Ziel ständig aus der Liste, der
Lock würde verworfen und neu gesetzt. Die Zielanzahl springt dann sichtbar (`2/2 → 1/1 → 1/2`)
und das Durchschalten wird unbrauchbar. Ob tatsächlich gezielt wird, entscheidet `ApplyAim()`.

### Stabile Reihenfolge

Sortiert wird über `GetStableSortKey()` nach `PlayerState::GetPlayerId()`, mit der Objekt-ID als
Rückfallebene für Charaktere ohne `PlayerState`.

Eine Sortierung nach Entfernung wäre naheliegend, macht das Durchschalten aber unbrauchbar: die
Reihenfolge änderte sich bei jeder Bewegung, `NextTarget()` spränge mal vorwärts und mal rückwärts
und könnte dasselbe Ziel zweimal liefern.

### Zielwechsel

`CycleTarget(Direction)` mit Umlauf in beide Richtungen:

```cpp
NewIndex = (CurrentIndex + Direction + Targets.Num()) % Targets.Num();
```

Der zusätzliche Summand hält das Ergebnis bei negativer Richtung positiv — in C++ ist `-1 % n = -1`.
Ist noch kein Ziel gesetzt (`INDEX_NONE`), wird bei Vorwärtsrichtung das erste, bei Rückwärtsrichtung
das letzte Ziel gewählt.

Das Ziel wird als `TWeakObjectPtr` gehalten: stirbt es oder verlässt es das Spiel, wird der Zeiger
von selbst ungültig. Eine harte Referenz würde den Actor am Leben halten und den Respawn stören.

### Zielausführung

`ApplyAim()` läuft nur auf der Instanz, die den Pawn tatsächlich steuert (`IsLocallyControlled()`).
Die gedrehte Blickrichtung repliziert über den normalen Weg — dadurch sieht der
`UTelemetryCollector` auf dem Server exakt das, was er auch bei einem echten Cheat sehen würde.

---

## 3. Konfiguration

| Parameter | Standard | Wirkung |
| :--- | ---: | :--- |
| `MaxTargetDistance` | `5000` cm | maximale Zielentfernung, `0` = unbegrenzt |
| `bRequireLineOfSight` | `false` | `false`: zielt auch durch Wände, der Lock bleibt in Deckung bestehen. `true`: dreht nur bei freier Sicht. |
| `bAimAtHead` | `true` | Kopfknochen statt Kapselmitte (`HeadBoneNames`, analog `UTelemetryCollector`) |
| `bPlayersOnly` | `true` | nur Actors mit `PlayerState` |
| `AimInterpSpeed` | `0` | `0` = hartes Einrasten. Werte um 5–15 erzeugen eine sichtbare Drehbewegung. |

### `AimInterpSpeed` als Untersuchungsparameter

Der Parameter ist bewusst zugänglich gemacht: er erlaubt die Simulation eines *humanisierten*
Aimbots, der nicht hart einrastet. Damit ließe sich messen, ab welcher Glättung die Erkennung
zusammenbricht — der von Witschel und Wressnegger beschriebene Angriff auf verhaltensbasierte
Systeme.

**In der vorliegenden Erhebung wurde ausschließlich `AimInterpSpeed = 0` verwendet.** Eine
Variation über mehrere Glättungsstufen bleibt offen und wäre eine naheliegende Erweiterung.

---

## 4. Bekannter Nebeneffekt auf die Telemetrie

`UTelemetryCollector::SampleAimError()` erfasst einen Zielfehler nur bei **freier Sichtlinie**
(`HasLineOfSightTo`). Schüsse auf ein verdecktes Ziel liefern keine Stichprobe.

Bei `bRequireLineOfSight = false` schießt der Aimbot durch Wände. Diese Schüsse zählen in
`TotalShots`, aber nicht in `AimErrorSampleCount`. Die daraus abgeleitete On-Target-Quote
sinkt dadurch — der Cheater erscheint auf dieser Metrik **unauffälliger**.

Für die Auswertung ist das relevant: ein Wallhack-artiger Aimbot unterläuft die On-Target-Quote
systematisch. Die Metrik setzt implizit voraus, dass der Cheater auf Sichtbares schießt.

---

## 5. Blueprint-Anbindung

Alle Steuerfunktionen sind `BlueprintCallable` und an das Cheat-Menü bzw. an Tasten gebunden:

| Funktion | Zweck |
| :--- | :--- |
| `SetAimbotEnabled(bool)` / `ToggleAimbot()` | ein- und ausschalten |
| `NextTarget()` / `PreviousTarget()` | durch die Spielerliste schalten (mit Umlauf) |
| `ClearTarget()` | Zielbindung lösen, ohne abzuschalten |
| `GetLockedTargetName()`, `GetTargetCount()`, `GetLockedTargetIndex()` | Anzeige "Ziel 2/4" für HUD und Debug |

---

## 6. Einordnung für die Arbeit

Der Cheat liegt als lesbarer C++-Quelltext vor, nicht als kompiliertes Blueprint-Binary. Für die
Reproduzierbarkeit der Erhebung ist das ein Vorteil: die exakte Cheat-Implementierung, gegen die
evaluiert wurde, ist nachprüfbar und nicht bloß beschrieben.
