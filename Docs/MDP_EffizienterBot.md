# MDP-Spezifikation – Effizienter RL-Bot (Bot 1)

> **Status:** Entwurf v0.1 · Stand: 2026-05-08
> **Autor:** Patrik Milakovic
> **Kontext:** Bachelorarbeit – Praxisteil, Bot 1 von 2
> **Engine:** Unreal Engine 5.7 · Template: Arena Shooter Variant
> **RL-Framework:** Epic Learning Agents Plugin (PPO)

---

## 1. Zweck dieses Dokuments

Dieses Dokument spezifiziert das Markov-Entscheidungsproblem (Markov Decision Process,
MDP) für den **effizienzorientierten** Reinforcement-Learning-Bot. Es legt verbindlich
fest, welche Information der Agent erhält (Beobachtungsraum), welche Eingaben er an die
Spielwelt zurückgeben kann (Aktionsraum) und nach welcher Belohnungsfunktion er
optimiert wird. Das zweite Bot-Modell ("Humanized Bot") wird denselben Beobachtungs-
und Aktionsraum verwenden, jedoch eine veränderte Belohnungsfunktion und gegebenenfalls
zusätzliche Verhaltensbeschränkungen. Die Vergleichbarkeit beider Modelle steht im
Mittelpunkt der späteren Detektionsanalyse und ist daher konstruktive Vorgabe an die
Modellierung.

---

## 2. Formaler Rahmen

Das Problem wird als episodisches, teilweise beobachtbares MDP modelliert
($\langle \mathcal{S}, \mathcal{O}, \mathcal{A}, \mathcal{P}, \mathcal{R}, \gamma \rangle$):

| Symbol | Bedeutung |
|---|---|
| $\mathcal{S}$ | Vollständiger Weltzustand (Engine-intern, dem Agenten **nicht** zugänglich) |
| $\mathcal{O}$ | Beobachtungsraum des Agenten (Abschnitt 4) |
| $\mathcal{A}$ | Aktionsraum (Abschnitt 5) |
| $\mathcal{P}$ | Übergangsdynamik – durch UE5-Physik, Schaden, KI-Logik des Gegners gegeben, deterministisch bis auf Pseudozufall (Spawnpositionen, Streukegel) |
| $\mathcal{R}$ | Belohnungsfunktion (Abschnitt 6) |
| $\gamma$ | Discount-Faktor, initial $\gamma = 0{,}99$ |

Da der Agent den vollen Weltzustand (Position des Gegners hinter Wänden, gegnerische
Munition usw.) nicht kennt, handelt es sich strenggenommen um ein POMDP. Dies ist
beabsichtigt und für die Vergleichbarkeit mit dem zweiten Bot wichtig: Beide Bots dürfen
ausschließlich auf Information zugreifen, die ein menschlicher Spieler grundsätzlich
auch wahrnehmen könnte.

---

## 3. Umgebung

### 3.1 Trainingsmap

Erste Trainingsphase auf einer eigens angelegten Karte `Lvl_TrainArena_01`:

- Geschlossene rechteckige Arena, ca. 30 × 30 m
- Boden eben, vier Wände, sechs bis acht statische Deckungswürfel
- Zwei Spawnpunkte an gegenüberliegenden Enden
- Keine Pickups in V1, kein Höhenunterschied

Spätere Curriculum-Phasen: Erweiterung auf `Lvl_ArenaShooter` (Originalkarte) und
randomisierte Layouts.

### 3.2 Episode

| Parameter | Wert |
|---|---|
| Episodenlänge | maximal 60 s = 3.600 Ticks @ 60 Hz |
| Tickrate des Agenten | 30 Hz (jede zweite Engine-Frame) |
| Episode endet bei | Tod eines Agenten **oder** Timeout |
| Reset | Sofortiger Respawn beider Agenten an zufällig gewähltem Spawnpunkt, volle Health, volle Munition |
| Zustand bei Reset | Look-Richtung randomisiert ±180°, kleine Positions-Jitter ±50 cm |

### 3.3 Match-Konfiguration

Erste lauffähige Version: **1 vs 1**, beide Seiten mit `BP_ShooterWeapon_Rifle`.
Trainingsgegner in Phase 1 ist ein modifizierter `BP_ShooterAIController` (StateTree-
basiert, vorhanden im Template), in Phase 2+ Self-Play.

---

## 4. Beobachtungsraum $\mathcal{O}$

Der Beobachtungsvektor ist Float-wertig und auf $[-1, 1]$ bzw. $[0, 1]$ normalisiert.
Die Normalisierung erfolgt im `Interactor`-Component und ist symmetrisch zu Bot 2 zu
halten.

### 4.1 Eigenzustand (Self) – 14 Werte

| Index | Größe | Range vor Norm. | Beschreibung |
|---|---|---|---|
| 0 | Health | [0, 100] | Aktuelle Lebenspunkte |
| 1 | Ammo (geladen) | [0, MagSize] | Munition im Magazin |
| 2 | Ammo (Reserve) | [0, MaxReserve] | Reserve-Munition |
| 3 | IsReloading | {0, 1} | Aktuell im Nachlade-Vorgang |
| 4–6 | Velocity (lokal) | [-MaxSpeed, MaxSpeed]³ | Geschwindigkeit im eigenen Koordinatensystem (X = vorwärts, Y = rechts, Z = hoch) |
| 7 | Pitch | [-90°, 90°] | Look-Pitch |
| 8 | IsCrouching | {0, 1} | Hockt aktuell |
| 9 | IsAirborne | {0, 1} | In der Luft (Sprung/Fall) |
| 10 | TimeSinceLastShot | [0, 5 s] | Zeit seit letztem eigenen Schuss, geclippt |
| 11 | TimeSinceLastDamageTaken | [0, 5 s] | Zeit seit letzter erlittener Trefferwirkung |
| 12 | WeaponCooldownRemaining | [0, 1 s] | Verbleibende Feuerrate-Sperre |
| 13 | RecoilOffset | [0, 1] | Aktueller Rückstoßwert (Streukegel-Aufbau) |

Position im Welt-Koordinatensystem wird **nicht** beobachtet – das fördert
Generalisierung über Spawnpunkte und Karten.

### 4.2 Gegnerzustand (Enemy) – 11 Werte (in 1 vs 1)

Alle Vektoren werden im **lokalen Koordinatensystem des eigenen Agenten** angegeben.
"Gesehen" bedeutet aktuell sichtbar im FOV plus Line-of-Sight-Trace ohne Verdeckung.

| Index | Größe | Range vor Norm. | Beschreibung |
|---|---|---|---|
| 14 | IsVisible | {0, 1} | Gegner aktuell sichtbar |
| 15 | TimeSinceLastSeen | [0, 10 s] | Zeit seit letzter Sichtung, geclippt |
| 16–18 | RelPosLastSeen | [-50 m, 50 m]³ | Relativposition zum letzten Sichtungszeitpunkt |
| 19 | RelDistanceLastSeen | [0, 50 m] | Euklidischer Abstand bei letzter Sichtung |
| 20–22 | RelVelocityLastSeen | [-MaxSpeed, MaxSpeed]³ | Geschätzte Relativgeschwindigkeit bei letzter Sichtung |
| 23 | EnemyHealthEstimate | [0, 100] | Geschätzte Gegner-Health (siehe Hinweis) |
| 24 | EnemyAimingAtMe | {0, 1} | Heuristik: Gegner-Look-Vektor zeigt im Konus auf eigene Hitbox |

**Hinweis zu EnemyHealthEstimate:** Wird inkrementell aus eigenen registrierten Treffern
berechnet (`100 − ∑ damage_dealt_this_life`). Tatsächliche aktuelle Health des Gegners
wird nicht durchgereicht.

### 4.3 Wahrnehmungs-Raycasts – 24 Werte

Zur räumlichen Orientierung werden Distanzwerte aus Raycasts vom Kopf des Agenten
verwendet:

- **16 horizontale Strahlen**, gleichmäßig über 360° verteilt, max. Reichweite 20 m
- **8 vertikale Strahlen** in Vorwärtsebene (4 nach oben, 4 nach unten gefächert), max. 20 m
- Rückgabewert pro Strahl: getroffene Distanz, normalisiert auf $[0, 1]$ (1 = nichts getroffen)

Zweck: implizite Repräsentation von Wänden, Deckungen und Spielfeldgrenze, ohne explizite
Karten-Codierung. Hält den Bot generalisierungsfähig über verschiedene Layouts.

### 4.4 Gesamtdimension

$$\dim(\mathcal{O}) = 14 + 11 + 24 = 49$$

Bei Erweiterung auf Mehrwaffen-Setups oder mehrere sichtbare Gegner ist eine padding-
oder masking-basierte Erweiterung dokumentiert vorzunehmen, um die Vergleichbarkeit zu
Bot 2 zu wahren.

---

## 5. Aktionsraum $\mathcal{A}$

Hybrider Aktionsraum mit kontinuierlichen und binären Komponenten. Learning Agents
unterstützt diese Mischung nativ über kombinierte Action-Schemas.

### 5.1 Kontinuierliche Aktionen – 4 Werte, jeweils $[-1, 1]$

| Index | Aktion | Mapping |
|---|---|---|
| 0 | MoveForward | $\rightarrow$ `AddMovementInput(Forward, value)` |
| 1 | MoveRight | $\rightarrow$ `AddMovementInput(Right, value)` |
| 2 | YawDelta | $\rightarrow$ `AddControllerYawInput(value · YawScale)`, `YawScale = 3{,}0` |
| 3 | PitchDelta | $\rightarrow$ `AddControllerPitchInput(value · PitchScale)`, `PitchScale = 2{,}0` |

`YawScale`/`PitchScale` sind Hyperparameter; sie bestimmen die maximale Drehgeschwindigkeit
pro Tick. Für den **effizienten** Bot werden diese Werte **nicht künstlich begrenzt** –
übermenschlich schnelles Zielen ist explizit erlaubt und Teil des Designs. (Bot 2 wird
hier später eine biomechanisch motivierte Begrenzung erhalten.)

### 5.2 Diskrete / binäre Aktionen – 4 Werte, jeweils $\{0, 1\}$

| Index | Aktion | Wirkung |
|---|---|---|
| 4 | Jump | `Jump()` bei Übergang 0 → 1 |
| 5 | Crouch | Toggle bei Übergang 0 → 1 |
| 6 | Fire | `StartFire()` bei 1, `StopFire()` bei 0 |
| 7 | Reload | `Reload()` bei Übergang 0 → 1, ignoriert bei vollem Magazin |

### 5.3 Gesamtdimension

$$\dim(\mathcal{A}) = 4_{\text{cont}} + 4_{\text{disc}} = 8$$

### 5.4 Action-Frequenz

Eine Aktion pro Agent-Tick (30 Hz). Zwischen den Ticks behält die Engine den letzten
Aktionsvektor bei (z. B. `Fire = 1` bedeutet kontinuierliches Feuern, bis der Agent in
einem späteren Tick `0` schreibt).

---

## 6. Belohnungsfunktion $\mathcal{R}$

Die Belohnungsfunktion ist das zentrale Unterscheidungsmerkmal zwischen Bot 1 und Bot 2.
Bot 1 wird **ausschließlich auf Spielergebnis** optimiert. Es gibt keinerlei Strafterme
für übermenschliche Reaktionszeiten, perfekte Aim-Kurven oder repetitive Bewegungsmuster.

### 6.1 Reward-Komponenten (initiale Werte)

| Ereignis | Wert | Begründung |
|---|---|---|
| Kill (Gegner getötet) | $+50$ | Hauptziel, sparse |
| Tod | $-50$ | Hauptbestrafung, sparse |
| Schaden ausgeteilt | $+1{,}0 \cdot \Delta\text{HP}$ | Dichtes Signal, beschleunigt Lernen |
| Schaden erlitten | $-1{,}0 \cdot \Delta\text{HP}$ | Symmetrische Kostenstruktur |
| Headshot-Bonus | $+5$ | Verstärkt präzises Zielen, Markenzeichen "effizient" |
| Schuss daneben | $-0{,}05$ | Diskreter Munitions-/Verratsstrafterm |
| Zeit-Tick | $-0{,}01$ | Zeitdruck: schnelle Eliminationen werden bevorzugt |
| Episode-Timeout ohne Kill | $-10$ | Disincentiviert reines Verstecken |

Die Werte sind explizit als **initiale Hyperparameter** zu verstehen. Eine Sweep-
Tabelle wird im Trainingskapitel der Arbeit dokumentiert.

### 6.2 Was bewusst **nicht** in $\mathcal{R}$ enthalten ist

Diese Auflassung ist methodisch entscheidend und für die spätere Detektionsanalyse:

- **Keine Strafe für Sub-100-ms-Reaktionszeiten.** Der Bot darf binnen eines einzigen
  Ticks nach erstmaliger Sichtung des Gegners feuern.
- **Keine Strafe für perfekte Aim-Trajektorien** (gerade Linien, sprunghafte Snaps,
  exakte Headshots auf große Distanz).
- **Keine Strafe für inhuman schnelles Drehen** (z. B. 360°-Flicks).
- **Keine Strafe für repetitive oder mechanisch identische Bewegungsmuster** zwischen
  Episoden.
- **Keine Aktivitäts-/Diversitäts-Boni**, keine Entropy-Regularisierung über das hinaus,
  was PPO standardmäßig in der Policy-Loss verwendet.

Genau diese Weglassungen produzieren die für Detektoren später beobachtbaren
Signaturen: extrem niedrige Reaktionszeiten, geringe Aim-Streuung, hohe Aktions-
Konsistenz. Dies ist gewollt.

### 6.3 Reward-Skalierung

Alle Reward-Werte werden vor dem Übergeben an PPO durch einen laufenden
Standardabweichungs-Normalizer skaliert (Standardvorgehen in Learning Agents). Dies
verhindert, dass die sparse Kill-/Death-Terme die dichten Schadensterme dominieren.

---

## 7. Episodenabschluss & Reset-Logik

Eine Episode endet (mit Bonus/Strafterm laut Tabelle in 6.1):

1. **Tod** des eigenen Agenten oder des Gegners
2. **Timeout** nach 60 s
3. **Out-of-Bounds**: Falls der Agent durch Engine-Glitch unter die Map fällt
   ($z < z_{\text{kill}}$), Strafterm $-25$ und Reset.
4. **Stuck-Detection**: Falls der Agent über 5 s keine Positionsänderung > 30 cm zeigt,
   Strafterm $-5$ und Reset (wird nur in frühen Trainingsphasen aktiviert).

Reset stellt beide Agenten an zufälligen Spawnpunkten wieder her, randomisiert
Look-Richtung (Yaw $\in [-180°, 180°]$, Pitch = 0°) und setzt alle internen Timer
(`TimeSinceLastShot` etc.) auf den Maximalwert ihres Clipping-Intervalls.

---

## 8. Logging-Anforderungen für die Detektionsanalyse

**Wichtig:** Diese Logs sind nicht Teil des MDP, müssen aber von Beginn an parallel
geschrieben werden, um später Detektionsmodelle vergleichend auf Bot 1 und Bot 2 zu
trainieren bzw. zu evaluieren.

### 8.1 Per-Tick-Log (CSV oder Parquet)

| Spalte | Beschreibung |
|---|---|
| `match_id`, `episode_id`, `tick` | Zeitschlüssel |
| `agent_id` | Identifikation des Bots |
| `pos_x/y/z` | Weltposition |
| `look_yaw`, `look_pitch` | Blickrichtung in Grad |
| `velocity_x/y/z` | Geschwindigkeit |
| `enemy_visible` | Bool |
| `enemy_pos_x/y/z` | Wenn sichtbar |
| `action[0..7]` | Roher Aktionsvektor |
| `shot_fired` | Bool, dieser Tick |
| `damage_dealt`, `damage_taken` | Diesen Tick |
| `health_after` | Eigene Health |

### 8.2 Per-Engagement-Log

Eine "Engagement" beginnt mit erstmaliger Sichtung des Gegners nach einer Phase ohne
Sicht und endet beim ersten ausgelösten eigenen Schuss oder Sichtverlust.

| Spalte | Beschreibung |
|---|---|
| `engagement_id`, `match_id`, `episode_id` | Schlüssel |
| `t_first_visible`, `t_first_shot` | Tick-Zeitstempel |
| `reaction_time_ms` | $= t_{\text{first\_shot}} - t_{\text{first\_visible}}$ |
| `aim_path_yaw[0..N]`, `aim_path_pitch[0..N]` | Yaw/Pitch in den 200 ms vor erstem Schuss (samples @ 30 Hz) |
| `initial_angular_offset` | Grad-Abstand zum Gegner-Zielpunkt bei `t_first_visible` |
| `final_angular_offset` | Grad-Abstand bei `t_first_shot` |
| `outcome` | hit / miss / headshot |

Diese Engagement-Statistiken sind die Hauptdatenquelle für Detektoren, die auf
"unmenschliche" Reaktion und Aim-Charakteristik prüfen.

### 8.3 Speicherort

`Saved/RLLogs/<run_id>/per_tick.parquet`,
`Saved/RLLogs/<run_id>/per_engagement.parquet`

Nicht in Versionskontrolle aufnehmen (siehe `.gitignore`).

---

## 9. Hyperparameter (Platzhalter)

Werden im Trainingskapitel ausgefüllt. Initialer Vorschlag für PPO über Learning Agents:

| Parameter | Initialwert |
|---|---|
| Discount $\gamma$ | 0,99 |
| GAE $\lambda$ | 0,95 |
| Clip $\varepsilon$ | 0,2 |
| Learning Rate (Policy) | $3 \cdot 10^{-4}$ |
| Learning Rate (Critic) | $1 \cdot 10^{-3}$ |
| Batch-Größe | 4096 Transitionen |
| Mini-Batch | 256 |
| Epochen pro Update | 4 |
| Entropie-Bonus | 0,005 |
| Anzahl paralleler Agenten in Welt | 16 (8 Paare) |
| Trainingsdauer | initial 5 Mio. Steps, mit Convergenz-Check |

---

## 10. Abgrenzung zu Bot 2 (Humanized Bot)

Damit die Detektionsanalyse aussagekräftig bleibt, **darf** sich Bot 2 später nur in
folgenden Punkten unterscheiden:

- **Erweiterte Belohnungsfunktion** mit Strafen für übermenschliches Verhalten (z. B.
  $-1$ pro Schuss, dessen `reaction_time_ms < 180`; Strafe für Yaw-Sprünge
  $> 180°/\text{s}$; Bonus für moderat verrauschte Aim-Trajektorien)
- **Optionale Aktions-Filter** (Low-Pass auf Yaw/Pitch, simulierte Maus-Trägheit)
- **Eventuell Imitation-Learning-Vorlauf** auf menschlicher Demo-Daten (BC-Pretraining)

Beobachtungs- und Aktionsraum, Umgebung, Episodendefinition und Logging bleiben
**identisch**. Andernfalls vergleicht man später Detektionsergebnisse über zwei
unterschiedliche Probleme – und die Arbeit verliert an Aussagekraft.

---

## 11. Versionierung dieses Dokuments

| Version | Datum | Änderung |
|---|---|---|
| v0.1 | 2026-05-08 | Erstentwurf |

Änderungen am MDP nach Beginn des Trainings sind im Methodikteil der Arbeit explizit
zu dokumentieren (welche Werte wann verändert wurden, mit Begründung), damit die
Reproduzierbarkeit gewahrt bleibt.
