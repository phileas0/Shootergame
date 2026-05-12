# Implementierungsplan – BP_RLEfficientController & Trainings-Setup

> **Status:** Entwurf v0.1 · Stand: 2026-05-08
> **Autor:** Patrik Milakovic
> **Vorausgesetzt:** [`MDP_EffizienterBot.md`](./MDP_EffizienterBot.md)
> **Engine:** Unreal Engine 5.7 · RL-Framework: Learning Agents

Dieses Dokument spezifiziert die Klassenstruktur, Komponenten-Anordnung und Funktions-
signaturen für die Implementierung des effizienten Bots. Es ist als Bauanleitung
gedacht: jeder Abschnitt ist abarbeitbar und prüfbar.

---

## 1. Architektur auf einer Seite

```
┌─────────────────────────────────────────────────────────────────────┐
│  BP_RLTrainingArena (AActor, in Lvl_TrainArena_01 platziert)        │
│                                                                      │
│   ┌──────────────────────────────────────────────────────────────┐  │
│   │  ULearningAgentsManager (Component)                          │  │
│   │   ├─ URLEfficientInteractor   (C++ Subclass, BP-erweiterbar) │  │
│   │   ├─ URLEfficientTrainer      (C++ Subclass, BP-erweiterbar) │  │
│   │   ├─ ULearningAgentsPolicy    (Asset: NN_Policy_Eff)         │  │
│   │   ├─ ULearningAgentsCritic    (Asset: NN_Critic_Eff)         │  │
│   │   └─ URLEpisodeManager        (C++ Component, eigen)          │  │
│   └──────────────────────────────────────────────────────────────┘  │
│                                                                      │
│   N × Agent-Paare:                                                  │
│     ┌──────────────────────────┐   ┌──────────────────────────┐     │
│     │ BP_RLShooterNPC (Pawn)   │   │ BP_RLShooterNPC (Pawn)   │     │
│     │  └ BP_RLEfficient-       │   │  └ BP_RLEfficient-       │     │
│     │     Controller           │   │     Controller           │     │
│     │      └ URLAgentLogger    │   │      └ URLAgentLogger    │     │
│     └──────────────────────────┘   └──────────────────────────┘     │
└─────────────────────────────────────────────────────────────────────┘
```

**Datenfluss pro Tick (30 Hz):**

```
1. Arena.Tick
2. EpisodeManager prüft Tod / Timeout / Stuck pro Paar
   → bei Bedarf: Trainer.OnAgentCompletion + ResetAgentEpisode
3. Manager.RunInference()
   ├─ für jede AgentId: Interactor.GatherAgentObservation → Obs-Tensor
   ├─ Policy(Obs) → Action-Tensor
   └─ für jede AgentId: Interactor.PerformAgentAction
4. (Trainings-Modus) Manager.RunTraining()
   └─ Trainer.GatherAgentReward, sammelt Trajektorien, PPO-Update
5. Logger schreibt Per-Tick-Zeile, ggf. Per-Engagement-Zeile
```

---

## 2. Neue Assets & Klassen – vollständige Liste

| Pfad | Typ | Sprache | Zweck |
|---|---|---|---|
| `Source/Shootergame/AI/RL/RLEfficientInteractor.h/.cpp` | `ULearningAgentsInteractor`-Subclass | C++ | Observations & Actions |
| `Source/Shootergame/AI/RL/RLEfficientTrainer.h/.cpp` | `ULearningAgentsTrainer`-Subclass | C++ | Reward, Completion, Reset |
| `Source/Shootergame/AI/RL/RLEpisodeManager.h/.cpp` | `UActorComponent` | C++ | Episodensteuerung pro Paar |
| `Source/Shootergame/AI/RL/RLObservationLibrary.h/.cpp` | `UBlueprintFunctionLibrary` | C++ | Raycasts, Sicht, Hilfsmathematik |
| `Source/Shootergame/AI/RL/RLAgentLogger.h/.cpp` | `UActorComponent` | C++ | Per-Tick / Per-Engagement Logging |
| `Source/Shootergame/AI/RL/RLRewardTracker.h/.cpp` | `UActorComponent` | C++ | Sammelt Reward-Events pro Tick |
| `Content/Variant_Shooter/Blueprints/AI/RL/BP_RLEfficientController.uasset` | `AAIController` BP | BP | Possessing-Logik, Manager-Registrierung |
| `Content/Variant_Shooter/Blueprints/AI/RL/BP_RLShooterNPC.uasset` | `BP_ShooterNPC`-Subclass | BP | RL-Aktions-Schnittstelle auf Pawn |
| `Content/Variant_Shooter/Blueprints/AI/RL/BP_RLTrainingArena.uasset` | `AActor` BP | BP | Komposition aller RL-Komponenten |
| `Content/Variant_Shooter/Maps/Lvl_TrainArena_01.umap` | Level | – | Trainingsarena (siehe MDP §3.1) |
| `Content/RL/Policies/NN_Policy_Eff.uasset` | `ULearningAgentsPolicy` | Asset | Initial leer, wird beim ersten Run erzeugt |
| `Content/RL/Policies/NN_Critic_Eff.uasset` | `ULearningAgentsCritic` | Asset | Wertfunktion |

**Begründung der C++/BP-Mischung:** Per-Tick-kritische Pfade (Interactor, Trainer,
Reward-Tracker, Logger) liegen in C++, weil sie 30 Hz × N Agenten × Anzahl Felder im
Beobachtungsvektor ausführen. Hochlevel-Komposition (Controller, Arena, NPC-Subclass)
in Blueprint, weil dort Iteration und Sichtbarkeit für die Bachelorarbeit wichtiger
sind als Performance.

---

## 3. Schritt-für-Schritt-Setup (in dieser Reihenfolge!)

### 3.1 Plugins & Modul-Setup

1. **Edit → Plugins → Learning Agents** aktivieren. Editor neu starten.
2. `Shootergame.Build.cs` ergänzen:
   ```csharp
   PublicDependencyModuleNames.AddRange(new string[]
   {
       "Core", "CoreUObject", "Engine", "InputCore",
       "LearningAgents",          // PPO, Manager, Interactor, Trainer
       "LearningAgentsTraining",   // Training-spezifisches
       "AIModule", "GameplayTasks", "NavigationSystem"
   });
   ```
3. Projekt aus Visual Studio rebuilden.

### 3.2 C++-Klassen anlegen (siehe Abschnitt 4 für Inhalte)

Reihenfolge:
1. `RLObservationLibrary` (keine Abhängigkeiten)
2. `RLRewardTracker` (Component, hängt am Pawn)
3. `RLAgentLogger` (Component, hängt am Controller)
4. `RLEfficientInteractor` (braucht Library)
5. `RLEfficientTrainer` (braucht RewardTracker)
6. `RLEpisodeManager` (Component an Arena)

### 3.3 Blueprints ableiten

1. `BP_RLShooterNPC` von `BP_ShooterNPC` ableiten, Funktionen aus §5 hinzufügen.
2. `BP_RLEfficientController` von `AAIController` (oder direkt `BP_ShooterAIController`,
   die `Run StateTree`-Logik aber **deaktivieren**).
3. `BP_RLTrainingArena` von `AActor` ableiten, Komponenten arrangieren.

### 3.4 Map & Spielmodus

1. Neue Map `Lvl_TrainArena_01` anlegen, geometrisch wie in MDP §3.1.
2. 8 Spawnpunkt-Paare als `PlayerStart`-Actoren platzieren, pro Paar weit
   gegenüberliegend.
3. Eine Instanz von `BP_RLTrainingArena` ins Level setzen.
4. World Settings → GameMode auf `BP_ShooterGameMode` lassen, aber DefaultPawnClass
   leerlassen (Spawning erfolgt über die Arena, nicht über GameMode).

### 3.5 Smoke Test (vor jedem RL-Code)

Ohne Learning Agents zuerst überprüfen, dass:
- 16 Instanzen von `BP_RLShooterNPC` mit `BP_RLEfficientController` parallel im Level
  stehen können, ohne dass die Engine bei 60 fps unter 30 fps fällt.
- Manuelles Aufrufen von `RL_ApplyMovementInput(1, 0)` den Pawn nach vorn bewegt und
  `RL_StartFire()` einen Schuss auslöst.
- Schaden auf andere `BP_RLShooterNPC` korrekt wirkt (über `BPI_Shooter`-Interface,
  schon im Template vorhanden).

Erst danach LearningAgents-Komponenten aktivieren.

---

## 4. C++-Klassenspezifikation

### 4.1 `URLObservationLibrary`

```cpp
UCLASS()
class SHOOTERGAME_API URLObservationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** 16 horizontale + 8 vertikale Raycasts, normalisiert. */
    UFUNCTION(BlueprintCallable, Category="RL|Observation")
    static void GatherRaycastDistances(
        const AActor* FromActor,
        float MaxDistance,
        TArray<float>& OutNormalized);  // Größe 24

    /** Liefert true, wenn Enemy im FOV-Konus liegt UND Line-of-Sight frei ist. */
    UFUNCTION(BlueprintCallable, Category="RL|Observation")
    static bool IsEnemyVisible(
        const AActor* Self,
        const AActor* Enemy,
        float FovCosThreshold,           // z.B. cos(50°) = 0.6428
        float MaxRange,                  // z.B. 5000
        FHitResult& OutTraceHit);

    /** Vektor von Self → Other in Self-lokalem Koordinatensystem. */
    UFUNCTION(BlueprintPure, Category="RL|Observation")
    static FVector ToLocalFrame(const AActor* Self, const FVector& WorldPos);

    /** Heuristik: zeigt Enemy.Forward in einen Konus auf Self.Hitbox? */
    UFUNCTION(BlueprintCallable, Category="RL|Observation")
    static bool IsEnemyAimingAtMe(
        const AActor* Self,
        const AActor* Enemy,
        float ConeCosThreshold);
};
```

### 4.2 `URLRewardTracker` (Pawn-Component)

Wird beim BeginPlay des `BP_RLShooterNPC` automatisch hinzugefügt. Sammelt
Reward-relevante Events während des Ticks; der Trainer ruft `ConsumeReward()` einmal
pro Tick auf, was den internen Akkumulator zurücksetzt.

```cpp
UCLASS(ClassGroup=(RL), meta=(BlueprintSpawnableComponent))
class SHOOTERGAME_API URLRewardTracker : public UActorComponent
{
    GENERATED_BODY()

public:
    /** Vom Pawn bei OnTakeDamage / OnDealDamage zu rufen. */
    UFUNCTION(BlueprintCallable) void OnDamageDealt(float Damage, bool bHeadshot);
    UFUNCTION(BlueprintCallable) void OnDamageTaken(float Damage);
    UFUNCTION(BlueprintCallable) void OnKill();
    UFUNCTION(BlueprintCallable) void OnDeath();
    UFUNCTION(BlueprintCallable) void OnShotFired(bool bHit);
    UFUNCTION(BlueprintCallable) void OnTimeout();

    /** Vom Trainer pro Tick aufgerufen, leert internen Akkumulator. */
    float ConsumeReward();

    /** Werte aus MDP §6.1 als UPROPERTY exponiert für Sweeps. */
    UPROPERTY(EditAnywhere, Category="RL|Rewards") float R_Kill        = +50.f;
    UPROPERTY(EditAnywhere, Category="RL|Rewards") float R_Death       = -50.f;
    UPROPERTY(EditAnywhere, Category="RL|Rewards") float R_DamageDealt = +1.f;
    UPROPERTY(EditAnywhere, Category="RL|Rewards") float R_DamageTaken = -1.f;
    UPROPERTY(EditAnywhere, Category="RL|Rewards") float R_Headshot    = +5.f;
    UPROPERTY(EditAnywhere, Category="RL|Rewards") float R_Miss        = -0.05f;
    UPROPERTY(EditAnywhere, Category="RL|Rewards") float R_TickPenalty = -0.01f;
    UPROPERTY(EditAnywhere, Category="RL|Rewards") float R_Timeout     = -10.f;

private:
    float Accumulator = 0.f;
    virtual void TickComponent(float DeltaTime, ELevelTick, FActorComponentTickFunction*) override;
};
```

### 4.3 `URLEfficientInteractor`

Definiert den 49-dim Observation-Vektor und den 8-dim Action-Vektor exakt nach
MDP §4 und §5. In Learning Agents werden Observations/Actions semantisch
strukturiert; folgender Aufbau wird empfohlen:

```cpp
UCLASS()
class SHOOTERGAME_API URLEfficientInteractor : public ULearningAgentsInteractor
{
    GENERATED_BODY()

public:
    // ── Schema-Definitionen, einmal beim Setup aufgerufen ────────────────
    virtual void SpecifyAgentObservation_Implementation(
        FLearningAgentsObservationSchemaElement& OutObservation,
        ULearningAgentsObservationSchema* Schema) override;

    virtual void SpecifyAgentAction_Implementation(
        FLearningAgentsActionSchemaElement& OutAction,
        ULearningAgentsActionSchema* Schema) override;

    // ── Pro Tick / Agent ─────────────────────────────────────────────────
    virtual void GatherAgentObservation_Implementation(
        FLearningAgentsObservationObjectElement& OutObservation,
        ULearningAgentsObservationObject* ObservationObject,
        const int32 AgentId) override;

    virtual void PerformAgentAction_Implementation(
        const ULearningAgentsActionObject* ActionObject,
        const FLearningAgentsActionObjectElement& Action,
        const int32 AgentId) override;

private:
    // Hilfsmethoden, eine pro Block in MDP §4
    void GatherSelfObs(const ABP_RLShooterNPC* Pawn, TArray<float>& Out) const;
    void GatherEnemyObs(const ABP_RLShooterNPC* Pawn, const ABP_RLShooterNPC* Enemy,
                        TArray<float>& Out) const;
    void GatherRayObs(const ABP_RLShooterNPC* Pawn, TArray<float>& Out) const;
};
```

**Implementierungs-Skizze `GatherAgentObservation`:**

```cpp
void URLEfficientInteractor::GatherAgentObservation_Implementation(...)
{
    auto* Pawn  = GetAgentPawn<ABP_RLShooterNPC>(AgentId);   // Helper auf Manager
    auto* Enemy = GetEnemyOf(Pawn);                          // siehe EpisodeManager

    TArray<float> SelfBuf;  SelfBuf.Reserve(14);
    TArray<float> EnemyBuf; EnemyBuf.Reserve(11);
    TArray<float> RayBuf;   RayBuf.Reserve(24);

    GatherSelfObs(Pawn, SelfBuf);
    GatherEnemyObs(Pawn, Enemy, EnemyBuf);
    GatherRayObs(Pawn, RayBuf);

    UE::LearningAgents::Observation::SetStruct(OutObservation, ObservationObject, {
        UE::LearningAgents::Observation::SetFloatArray(SelfBuf,  ObservationObject),
        UE::LearningAgents::Observation::SetFloatArray(EnemyBuf, ObservationObject),
        UE::LearningAgents::Observation::SetFloatArray(RayBuf,   ObservationObject)
    });
}
```

> **Hinweis:** Die exakte LA-API hat sich zwischen 5.4, 5.5 und 5.6 zweimal verändert.
> Bei Build-Fehlern den `Make…Observation`/`Add…Observation`-Stil aus den Sample-
> Projekten der installierten Engine-Version übernehmen. Konzeptionell bleibt:
> Schema einmal definieren, pro Tick Werte hineinschreiben.

**Implementierungs-Skizze `PerformAgentAction`:**

```cpp
void URLEfficientInteractor::PerformAgentAction_Implementation(...)
{
    auto* Pawn = GetAgentPawn<ABP_RLShooterNPC>(AgentId);

    // Kontinuierliche Achsen → Bewegung & Look
    const float MoveFwd  = ReadFloat(Action, /*idx*/ 0);
    const float MoveRgt  = ReadFloat(Action, /*idx*/ 1);
    const float YawDelta = ReadFloat(Action, /*idx*/ 2);
    const float PitDelta = ReadFloat(Action, /*idx*/ 3);

    Pawn->RL_ApplyMovementInput(MoveFwd, MoveRgt);
    Pawn->RL_ApplyLookInput(YawDelta * YawScale, PitDelta * PitchScale);

    // Binäre Aktionen
    if (ReadBool(Action, 4) && !Pawn->bIsAirborne) Pawn->RL_Jump();
    Pawn->RL_SetCrouch(ReadBool(Action, 5));

    if (ReadBool(Action, 6))  Pawn->RL_StartFire();
    else                       Pawn->RL_StopFire();

    if (ReadBool(Action, 7))  Pawn->RL_Reload();
}
```

### 4.4 `URLEfficientTrainer`

```cpp
UCLASS()
class SHOOTERGAME_API URLEfficientTrainer : public ULearningAgentsTrainer
{
    GENERATED_BODY()

public:
    virtual void GatherAgentReward_Implementation(
        float& OutReward, const int32 AgentId) override;

    virtual void GatherAgentCompletion_Implementation(
        ELearningAgentsCompletion& OutCompletion, const int32 AgentId) override;

    virtual void ResetAgentEpisode_Implementation(const int32 AgentId) override;

    UPROPERTY(EditAnywhere, Category="RL|Training")
    int32 MaxEpisodeTicks = 1800;   // 60 s @ 30 Hz
};
```

`GatherAgentReward` delegiert direkt an `URLRewardTracker::ConsumeReward()` des Pawns.
`GatherAgentCompletion` prüft Tod, Timeout, Stuck (siehe MDP §7) und gibt entsprechend
`Termination` oder `Truncation` zurück. `ResetAgentEpisode` ruft die Arena-Reset-Methode.

### 4.5 `URLEpisodeManager` (Arena-Component)

```cpp
UCLASS(ClassGroup=(RL), meta=(BlueprintSpawnableComponent))
class SHOOTERGAME_API URLEpisodeManager : public UActorComponent
{
    GENERATED_BODY()

public:
    /** Beim BeginPlay aufgerufen, befüllt PairTable mit aktuellen NPCs. */
    UFUNCTION(BlueprintCallable) void RegisterPair(int32 PairIdx,
        ABP_RLShooterNPC* AgentA, ABP_RLShooterNPC* AgentB);

    UFUNCTION(BlueprintCallable) bool ShouldResetPair(int32 PairIdx) const;

    UFUNCTION(BlueprintCallable) void ResetPair(int32 PairIdx);

    /** Hilfsmethode für Trainer/Interactor: liefert Gegner zu AgentId. */
    ABP_RLShooterNPC* GetEnemyOf(const ABP_RLShooterNPC* Self) const;

private:
    struct FAgentPair
    {
        TWeakObjectPtr<ABP_RLShooterNPC> A;
        TWeakObjectPtr<ABP_RLShooterNPC> B;
        float ElapsedTime = 0.f;
        float StuckTimer  = 0.f;
        FVector LastPosA  = FVector::ZeroVector;
        FVector LastPosB  = FVector::ZeroVector;
    };
    TArray<FAgentPair> Pairs;

    UPROPERTY(EditAnywhere) TArray<TObjectPtr<APlayerStart>> SpawnPoints;
    UPROPERTY(EditAnywhere) float MaxEpisodeSeconds = 60.f;
    UPROPERTY(EditAnywhere) float StuckThreshold    = 5.f;
    UPROPERTY(EditAnywhere) float StuckMinMovement  = 30.f; // cm in 5s
};
```

### 4.6 `URLAgentLogger` (Controller-Component)

Schreibt das Per-Tick- und Per-Engagement-Log gemäß MDP §8. Implementierung mit
`FArchive` und Parquet (über `Apache Arrow` als Drittbibliothek) wäre overkill –
für die Bachelorarbeit reicht **CSV mit gepufferten Zeilen-Writes**, alle 1.000 Ticks
geflusht. Per-Engagement-Tracking als kleine Statemachine im Tick:

```cpp
UCLASS(ClassGroup=(RL), meta=(BlueprintSpawnableComponent))
class SHOOTERGAME_API URLAgentLogger : public UActorComponent
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable) void StartRun(FString RunId);
    UFUNCTION(BlueprintCallable) void EndRun();

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick,
                               FActorComponentTickFunction*) override;

private:
    void EmitTickRow();
    void OnEnemyVisibilityChanged(bool bNowVisible);
    void OnShotFiredEvent(bool bHit, bool bHeadshot);

    bool bEnemyWasVisible = false;
    double TFirstVisible  = -1.0;
    TArray<FVector2f> AimPathSinceVisible; // (yaw, pitch)
    FString PerTickPath, PerEngagementPath;
};
```

Ablageort gemäß MDP §8.3: `Saved/RLLogs/<RunId>/per_tick.csv` und
`per_engagement.csv`. `RunId` wird von der Arena beim Trainings-Start gesetzt
(z. B. ISO-Timestamp + kurzer Hash der Reward-Konfig).

---

## 5. Blueprint-Spezifikation

### 5.1 `BP_RLShooterNPC` (extends `BP_ShooterNPC`)

**Neue Komponenten:**
- `RLRewardTracker` (Komponente)

**Neue Funktionen / Events** (alle BlueprintCallable, kategorie `RL|Action`):

| Name | Parameter | Implementierung |
|---|---|---|
| `RL_ApplyMovementInput` | `Forward: float, Right: float` | `AddMovementInput(GetActorForwardVector(), Forward)` + analog Right |
| `RL_ApplyLookInput` | `YawDelta: float, PitchDelta: float` | `AddControllerYawInput`, `AddControllerPitchInput` |
| `RL_Jump` | – | `Jump()` |
| `RL_SetCrouch` | `bWantsCrouch: bool` | `Crouch()` / `UnCrouch()` |
| `RL_StartFire` | – | Vorhandene Trigger-Logik aus `BPI_Shooter` aufrufen |
| `RL_StopFire` | – | analog |
| `RL_Reload` | – | analog |

**Hooks für RewardTracker** (im `BP_ShooterCharacter` Event-Graph):
- `OnTakeAnyDamage` → `RewardTracker.OnDamageTaken`
- `OnDealtDamage` (Custom Event aus Waffenlogik) → `OnDamageDealt`
- `OnDeath` (existiert im Template) → `OnDeath`
- Beim Schuss-Trace-Ergebnis → `OnShotFired(bHit)` und ggf. `OnDamageDealt(d, bHeadshot)`

**Wichtig:** Die existierende StateTree-Anbindung im `BP_ShooterAIController` darf
**nicht** mehr aktiv sein, sonst überschreiben StateTree-Tasks die RL-Inputs. Daher
braucht der RL-Controller eine andere Basisklasse.

### 5.2 `BP_RLEfficientController` (extends `AAIController`)

| Event | Implementierung |
|---|---|
| `OnPossess` | `Cast<BP_RLTrainingArena>(GetWorld()->GetActorOfClass(...))->RegisterController(Self)` |
| `OnUnPossess` | Arena.UnregisterController(Self) |

Keine StateTree-Komponente, kein BehaviorTree, keine Wahrnehmungs-Komponente
(`AIPerceptionComponent`) — Sicht wird vollständig im Interactor per Raycast
ermittelt, um die Vergleichbarkeit zu Bot 2 zu sichern (gleicher Sicht-Mechanismus).

### 5.3 `BP_RLTrainingArena` (extends `AActor`)

**Komponenten:**
- `LearningAgentsManager` (vorgefertigte Komponente aus dem Plugin)
- `URLEpisodeManager`
- `SceneComponent` „Root"

**Variablen (EditAnywhere):**
- `int32 PairCount = 8`
- `TSubclassOf<ABP_RLShooterNPC> AgentClass`
- `TSubclassOf<ABP_RLEfficientController> ControllerClass`
- `bool bTrainingMode = true` (false = Inferenz, lädt Policy-Asset und friert es ein)
- `TObjectPtr<ULearningAgentsPolicy> PolicyAsset`
- `TObjectPtr<ULearningAgentsCritic> CriticAsset`

**BeginPlay-Logik (Pseudocode):**

```text
RunId = MakeRunId()
EpisodeManager.SetSpawnPoints(GetAllPlayerStarts())
Logger.StartRun(RunId)

Manager.SetupManager(...)
Manager.SetupInteractor(URLEfficientInteractor::StaticClass())
Manager.SetupTrainer(URLEfficientTrainer::StaticClass())
Manager.SetupPolicy(PolicyAsset)
Manager.SetupCritic(CriticAsset)

For PairIdx in 0..PairCount-1:
    A = SpawnAgent(SpawnPoints[2*PairIdx])
    B = SpawnAgent(SpawnPoints[2*PairIdx + 1])
    EpisodeManager.RegisterPair(PairIdx, A, B)
    Manager.AddAgent(A.Controller)
    Manager.AddAgent(B.Controller)

If bTrainingMode: Manager.BeginTraining()
Else:             Manager.BeginInference()
```

**Tick-Logik:**

```text
For PairIdx in Pairs:
    If EpisodeManager.ShouldResetPair(PairIdx):
        EpisodeManager.ResetPair(PairIdx)

If bTrainingMode: Manager.RunTraining()
Else:             Manager.RunInference()
```

---

## 6. Trainings-Curriculum (operativ)

Phasen exakt wie in der ersten Antwort beschrieben, hier mit konkreten Schaltern:

| Phase | Gegner | bTrainingMode | Reset-Regel | Ziel |
|---|---|---|---|---|
| 1 | `BP_TrainingDummy` (statisch, dreht sich nicht) | true | Tod oder 30 s | Aim & Shoot lernen |
| 2 | `BP_ScriptedNPC` (StateTree, langsam) | true | Tod oder 60 s | Bewegen + Schießen |
| 3 | `BP_RLEfficient` Self-Play | true | Tod oder 60 s | Reaktion auf adaptiven Gegner |
| 4 | `BP_RLEfficient` mit Domain-Randomization | true | wie 3 | Generalisierung |
| 5 | Auf `Lvl_ArenaShooter`, gegen `BP_ShooterNPC` aus Template | false (Inference) | Match-Format | Evaluation |

Phasenwechsel werden in der Arena über einen Enum-Schalter `ETrainingPhase` getriggert.

---

## 7. Stolpersteine (häufige Fehler, die Tage kosten)

1. **Tickreihenfolge falsch.** Wenn Manager.RunTraining vor EpisodeManager.Reset
   läuft, sammelt PPO Belohnungen aus inkonsistenten Zuständen. Tickgroups setzen:
   Arena auf `TG_PrePhysics`, EpisodeManager als erste Komponente.
2. **StateTree-Reste.** Wenn der NPC noch eine StateTree-Komponente hat, überschreibt
   sie pro Tick die RL-Eingaben. Die geerbte StateTree-Komponente in
   `BP_RLShooterNPC` explizit deaktivieren oder die Komponente entfernen.
3. **Schuss-Cooldown ignoriert.** `Action[6]=1` jeden Tick erzeugt nicht 30 Schüsse/s,
   sondern wird durch die Waffen-`FireRate` limitiert. Das ist korrekt – der
   `WeaponCooldownRemaining` im Observationvektor (Self §4.1 Idx 12) erlaubt der
   Policy, das zu lernen. Nicht versehentlich entfernen.
4. **Yaw-Wraparound.** `RelLook`-Werte über 180° müssen normalisiert werden, sonst
   gibt es Diskontinuitäten im Observationsraum.
5. **Networking.** Learning Agents läuft **single-player / standalone**. Multiplayer-
   Simulation funktioniert nicht direkt. In den GameMode-Settings sicherstellen,
   dass `NetMode = Standalone` für Trainings-PIE.
6. **Logging-Performance.** CSV-Append im Spiel-Thread bei 30 Hz × 16 Agenten = 480
   Schreibvorgänge/s. Logger muss pro Agent in einem `TArray<FString>` puffern und
   auf einem `AsyncTask`-Thread flushen.
7. **Reward-Skalen.** Wenn der Tick-Penalty zu hoch ist, lernt der Bot zu suiziden,
   um die Episode zu beenden. Bei `R_TickPenalty = -0.01` und `R_Death = -50`
   bricht die Rechnung erst nach > 5.000 Ticks zugunsten des Suizids – ok, im Auge
   behalten.

---

## 8. Test- und Verifizierungsplan

**Unit-Level (vor Training):**
1. `RLObservationLibrary::GatherRaycastDistances` per `UE_LOG` Werte gegen manuell
   gemessene Distanzen prüfen.
2. `RLRewardTracker::ConsumeReward` mit fingierten Events anrufen, Wert-Tabelle
   gegen MDP §6.1 abgleichen.
3. `RLEpisodeManager::ResetPair` ohne Manager testen: beide NPCs werden an
   gegenüberliegenden Spawnpoints platziert, Health voll, Yaw randomisiert.

**Integration (vor langem Training):**
4. 1 Paar für 200 Ticks `Manager.RunInference()` mit zufällig initialisierter Policy
   laufen lassen, Logger-CSV inspizieren: enthält 400 Zeilen (2×200), keine NaN.
5. 100k Schritte Phase-1-Training: Reward-Curve in TensorBoard
   (`Saved/LearningAgents/.../`) muss steigen.

**Akzeptanzkriterium für Bot 1:**
- Phase 3 erreicht ≥ 70 % Win-Rate gegen StateTree-Bot über 1.000 Test-Episoden.
- Median Reaktionszeit (per_engagement.csv) < 100 ms.
- Trefferquote > 60 %.

Diese Werte sind explizit so gewählt, dass sie für Detektoren leicht von menschlichem
Verhalten unterscheidbar sind (Median menschlicher FPS-Reaktion ~250 ms, Trefferquote
in der Regel < 30 %).

---

## 9. Was als nächstes zu tun ist

1. Plugins aktivieren, `Build.cs` anpassen, Modul rebuilden (§3.1).
2. C++-Klassen-Skelette anlegen (§3.2). Nur Header + leere Method-Bodies, einmal
   compilen.
3. `BP_RLShooterNPC` ableiten und Smoke Test (§3.5) durchführen.
4. Erst danach Interactor und Trainer mit Logik füllen.
5. Phase-1-Training (statischer Dummy) für 100k Steps anwerfen, Reward-Curve prüfen.

Sobald Schritt 5 funktioniert, ist der Hauptrisiko-Block der Arbeit überstanden – ab
da ist es Iteration über Reward, Hyperparameter und Curriculum.

---

## 10. Versionierung

| Version | Datum | Änderung |
|---|---|---|
| v0.1 | 2026-05-08 | Erstentwurf |
