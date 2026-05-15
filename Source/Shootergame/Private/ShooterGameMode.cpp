#include "ShooterGameMode.h"
#include "ShooterGameState.h"
#include "ShooterPlayerState.h"
#include "TelemetryCollector.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "Misc/DateTime.h"
#include "EngineUtils.h"
#include "TimerManager.h"

AShooterGameMode::AShooterGameMode()
    : TelemetryLogger(nullptr)
    , ConnectedPlayerCount(0)
    , MatchDuration(300.f)
    , CountdownDuration(5.f)
    , PostGameDuration(15.f)
    , MinPlayersToStart(2)
{
    CSVFilename        = TEXT("session_01");
    GameStateClass     = AShooterGameState::StaticClass();
    PlayerStateClass   = AShooterPlayerState::StaticClass();
}

// ============================================================
//  Lifecycle
// ============================================================

void AShooterGameMode::BeginPlay()
{
    Super::BeginPlay();

    TelemetryLogger = NewObject<UTelemetryLogger>(this, UTelemetryLogger::StaticClass());

    if (UWorld* World = GetWorld())
    {
        World->AddOnActorSpawnedHandler(
            FOnActorSpawned::FDelegate::CreateUObject(this, &AShooterGameMode::OnActorSpawned));

        for (TActorIterator<ACharacter> It(World); It; ++It)
        {
            ACharacter* Char = *It;
            if (!Char->OnTakeAnyDamage.IsAlreadyBound(this, &AShooterGameMode::OnPawnTakeAnyDamage))
                Char->OnTakeAnyDamage.AddDynamic(this, &AShooterGameMode::OnPawnTakeAnyDamage);
        }
    }

    SetGamePhase(EShooterGamePhase::WaitingForPlayers);
    UE_LOG(LogTemp, Log, TEXT("[GameMode] Gestartet — warte auf Spieler."));
}

void AShooterGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorld()->GetTimerManager().ClearAllTimersForObject(this);
    FlushAllSessionsToCSV();
    Super::EndPlay(EndPlayReason);
}

// ============================================================
//  Player Connect / Disconnect
// ============================================================

void AShooterGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    ConnectedPlayerCount++;

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Spieler verbunden. Gesamt: %d"), ConnectedPlayerCount);

    AShooterGameState* GS = GetShooterGameState();
    if (!GS) return;

    if (ConnectedPlayerCount >= MinPlayersToStart &&
        GS->GamePhase == EShooterGamePhase::WaitingForPlayers)
    {
        StartCountdown();
    }
}

void AShooterGameMode::Logout(AController* Exiting)
{
    ConnectedPlayerCount = FMath::Max(0, ConnectedPlayerCount - 1);

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Spieler getrennt. Gesamt: %d"), ConnectedPlayerCount);

    AShooterGameState* GS = GetShooterGameState();
    if (GS && ConnectedPlayerCount < MinPlayersToStart &&
        GS->GamePhase == EShooterGamePhase::Countdown)
    {
        // Nicht genug Spieler — Countdown abbrechen
        GetWorld()->GetTimerManager().ClearTimer(CountdownTickHandle);
        SetGamePhase(EShooterGamePhase::WaitingForPlayers);
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] Zu wenige Spieler — Countdown abgebrochen."));
    }

    Super::Logout(Exiting);
}

void AShooterGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    Super::HandleStartingNewPlayer_Implementation(NewPlayer);

    if (!NewPlayer) return;

    APawn* Pawn = NewPlayer->GetPawn();
    if (!Pawn) return;

    if (!Pawn->OnTakeAnyDamage.IsAlreadyBound(this, &AShooterGameMode::OnPawnTakeAnyDamage))
        Pawn->OnTakeAnyDamage.AddDynamic(this, &AShooterGameMode::OnPawnTakeAnyDamage);

    // Neuer Spieler während PostGame direkt einfrieren
    AShooterGameState* GS = GetShooterGameState();
    if (GS && GS->GamePhase == EShooterGamePhase::PostGame)
        NewPlayer->SetIgnoreMoveInput(true);
}

// ============================================================
//  State Machine
// ============================================================

void AShooterGameMode::SetGamePhase(EShooterGamePhase NewPhase)
{
    AShooterGameState* GS = GetShooterGameState();
    if (!GS) return;

    GS->GamePhase = NewPhase;

    // RepNotify manuell auf Server auslösen (Server ruft OnRep nicht selbst auf)
    GS->OnGamePhaseChanged(NewPhase);

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Phase gewechselt: %d"), static_cast<int32>(NewPhase));
}

void AShooterGameMode::StartCountdown()
{
    AShooterGameState* GS = GetShooterGameState();
    if (!GS) return;

    GS->CountdownTime = static_cast<int32>(CountdownDuration);
    SetGamePhase(EShooterGamePhase::Countdown);

    GetWorld()->GetTimerManager().SetTimer(
        CountdownTickHandle,
        this,
        &AShooterGameMode::CountdownTick,
        1.f,
        true,   // looping
        1.f);   // first tick after 1 second

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Countdown gestartet (%d Sekunden)."),
        GS->CountdownTime);
}

void AShooterGameMode::CountdownTick()
{
    AShooterGameState* GS = GetShooterGameState();
    if (!GS) return;

    GS->CountdownTime--;
    UE_LOG(LogTemp, Log, TEXT("[GameMode] Countdown: %d"), GS->CountdownTime);

    if (GS->CountdownTime <= 0)
    {
        GetWorld()->GetTimerManager().ClearTimer(CountdownTickHandle);
        StartMatch();
    }
}

void AShooterGameMode::StartMatch()
{
    AShooterGameState* GS = GetShooterGameState();
    if (!GS) return;

    GS->MatchTimeRemaining = static_cast<int32>(MatchDuration);
    SetGamePhase(EShooterGamePhase::InProgress);
    SetAllPlayersRecording(true);
    SetAllPlayersFrozen(false);

    GetWorld()->GetTimerManager().SetTimer(
        MatchTickHandle,
        this,
        &AShooterGameMode::MatchTick,
        1.f,
        true,
        1.f);

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Match gestartet — %d Sekunden."),
        GS->MatchTimeRemaining);
}

void AShooterGameMode::MatchTick()
{
    AShooterGameState* GS = GetShooterGameState();
    if (!GS) return;

    GS->MatchTimeRemaining--;

    if (GS->MatchTimeRemaining <= 0)
    {
        GetWorld()->GetTimerManager().ClearTimer(MatchTickHandle);
        StartPostGame();
    }
}

void AShooterGameMode::StartPostGame()
{
    SetGamePhase(EShooterGamePhase::PostGame);
    SetAllPlayersRecording(false);
    SetAllPlayersFrozen(true);
    FlushAllSessionsToCSV();

    GetWorld()->GetTimerManager().SetTimer(
        PostGameHandle,
        this,
        &AShooterGameMode::EndPostGame,
        PostGameDuration,
        false);

    UE_LOG(LogTemp, Log, TEXT("[GameMode] PostGame — Spieler eingefroren, CSV geschrieben."));
}

void AShooterGameMode::EndPostGame()
{
    ResetAllPlayerStats();
    SetAllPlayersFrozen(false);

    AShooterGameState* GS = GetShooterGameState();
    if (!GS) return;

    if (ConnectedPlayerCount >= MinPlayersToStart)
        StartCountdown();
    else
        SetGamePhase(EShooterGamePhase::WaitingForPlayers);

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Neue Runde — Phase zurückgesetzt."));
}

// ============================================================
//  Telemetrie
// ============================================================

void AShooterGameMode::OnPlayerSessionEnd(AActor* DyingCharacter, AActor* KillerActor)
{
    if (!DyingCharacter || !TelemetryLogger) return;

    // Nur im laufenden Match aufzeichnen
    AShooterGameState* GS = GetShooterGameState();
    if (!GS || GS->GamePhase != EShooterGamePhase::InProgress) return;

    // Death auf Opfer-TelemetryCollector
    UTelemetryCollector* VictimCollector =
        DyingCharacter->FindComponentByClass<UTelemetryCollector>();
    if (VictimCollector)
    {
        VictimCollector->RecordDeath();
        VictimCollector->FinalizeSession(TelemetryLogger);
    }

    // PlayerState Death
    if (AController* VictimCtrl = Cast<APawn>(DyingCharacter)->GetController())
    {
        if (AShooterPlayerState* PS = VictimCtrl->GetPlayerState<AShooterPlayerState>())
        {
            PS->AddDeath();
            UpdateLeaderboard();
        }
    }

    // Kill auf Killer
    if (KillerActor && KillerActor != DyingCharacter)
    {
        UTelemetryCollector* KillerCollector =
            KillerActor->FindComponentByClass<UTelemetryCollector>();
        if (KillerCollector)
            KillerCollector->RecordKill();

        if (APawn* KillerPawn = Cast<APawn>(KillerActor))
        {
            if (AController* KillerCtrl = KillerPawn->GetController())
            {
                if (AShooterPlayerState* KPS = KillerCtrl->GetPlayerState<AShooterPlayerState>())
                {
                    KPS->AddKill();
                    UpdateLeaderboard();
                }
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[GameMode] Session finalisiert für: %s"),
        *DyingCharacter->GetName());
}

UTelemetryLogger* AShooterGameMode::GetTelemetryLogger() const
{
    return TelemetryLogger;
}

void AShooterGameMode::FlushAllSessionsToCSV()
{
    if (!TelemetryLogger) return;

    if (UWorld* World = GetWorld())
    {
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = It->Get();
            if (!PC) continue;
            APawn* Pawn = PC->GetPawn();
            if (!Pawn) continue;
            UTelemetryCollector* Collector = Pawn->FindComponentByClass<UTelemetryCollector>();
            if (Collector)
                Collector->FinalizeSession(TelemetryLogger);
        }
    }

    if (TelemetryLogger->GetBufferedSessionCount() > 0)
    {
        FString Timestamp     = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
        FString FinalFilename = FString::Printf(TEXT("session_%s"), *Timestamp);
        TelemetryLogger->FlushToCSV(FinalFilename);
        UE_LOG(LogTemp, Log, TEXT("[GameMode] CSV gespeichert: %s.csv"), *FinalFilename);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[GameMode] Buffer leer — keine CSV erstellt."));
    }
}

void AShooterGameMode::SetAllPlayersRecording(bool bEnabled)
{
    if (!GetWorld()) return;

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC) continue;
        APawn* Pawn = PC->GetPawn();
        if (!Pawn) continue;
        UTelemetryCollector* Collector = Pawn->FindComponentByClass<UTelemetryCollector>();
        if (Collector)
            Collector->SetRecordingEnabled(bEnabled);
    }
}

void AShooterGameMode::SetAllPlayersFrozen(bool bFrozen)
{
    if (!GetWorld()) return;

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC) continue;

        if (bFrozen)
        {
            PC->SetIgnoreMoveInput(true);
            PC->SetIgnoreLookInput(true);
        }
        else
        {
            PC->ResetIgnoreMoveInput();
            PC->ResetIgnoreLookInput();
        }
    }
}

void AShooterGameMode::ResetAllPlayerStats()
{
    if (!GetWorld()) return;

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC) continue;
        if (AShooterPlayerState* PS = PC->GetPlayerState<AShooterPlayerState>())
            PS->ResetStats();
    }

    UpdateLeaderboard();
}

// ============================================================
//  Leaderboard
// ============================================================

void AShooterGameMode::UpdateLeaderboard()
{
    AShooterGameState* GS = GetShooterGameState();
    if (!GS || !GetWorld()) return;

    TArray<FLeaderboardEntry> Entries;

    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!PC) continue;

        AShooterPlayerState* PS = PC->GetPlayerState<AShooterPlayerState>();
        if (!PS) continue;

        FLeaderboardEntry Entry;
        Entry.PlayerName = PS->GetPlayerName();
        Entry.Kills      = PS->Kills;
        Entry.Deaths     = PS->Deaths;
        Entry.KDRatio    = PS->GetKDRatio();
        Entries.Add(Entry);
    }

    // Absteigend nach KD sortieren
    Entries.Sort([](const FLeaderboardEntry& A, const FLeaderboardEntry& B)
    {
        return A.KDRatio > B.KDRatio;
    });

    GS->Leaderboard = Entries;

    // RepNotify manuell auf Server
    GS->OnLeaderboardUpdated();
}

// ============================================================
//  Damage / Kill Tracking
// ============================================================

void AShooterGameMode::OnActorSpawned(AActor* SpawnedActor)
{
    ACharacter* Char = Cast<ACharacter>(SpawnedActor);
    if (!Char) return;

    if (!Char->OnTakeAnyDamage.IsAlreadyBound(this, &AShooterGameMode::OnPawnTakeAnyDamage))
        Char->OnTakeAnyDamage.AddDynamic(this, &AShooterGameMode::OnPawnTakeAnyDamage);

    // Spieler der während PostGame spawnt direkt einfrieren
    AShooterGameState* GS = GetShooterGameState();
    if (GS && GS->GamePhase == EShooterGamePhase::PostGame)
    {
        if (APlayerController* PC = Cast<APlayerController>(Char->GetController()))
        {
            PC->SetIgnoreMoveInput(true);
            PC->SetIgnoreLookInput(true);
        }
    }
}

void AShooterGameMode::OnPawnTakeAnyDamage(AActor* DamagedActor, float Damage,
                                            const UDamageType* DamageType,
                                            AController* InstigatedBy, AActor* DamageCauser)
{
    if (!InstigatedBy || Damage <= 0.f) return;

    APawn* InstigatorPawn = InstigatedBy->GetPawn();
    if (!InstigatorPawn || InstigatorPawn == DamagedActor) return;

    // Nur im laufenden Match tracken
    AShooterGameState* GS = GetShooterGameState();
    if (!GS || GS->GamePhase != EShooterGamePhase::InProgress) return;

    // RecordHit wird automatisch via UTelemetryCollector::OnOwnerTakeAnyDamage gezählt
    // — kein manueller Aufruf hier nötig (würde doppelt zählen)

    ACharacter* DamagedChar = Cast<ACharacter>(DamagedActor);
    if (DamagedChar)
    {
        KillerMap.Add(DamagedChar, InstigatorPawn);
        if (!DamagedChar->OnDestroyed.IsAlreadyBound(this, &AShooterGameMode::OnCharacterDestroyed))
            DamagedChar->OnDestroyed.AddDynamic(this, &AShooterGameMode::OnCharacterDestroyed);
    }
}

void AShooterGameMode::OnCharacterDestroyed(AActor* DestroyedActor)
{
    ACharacter* DestroyedChar = Cast<ACharacter>(DestroyedActor);
    if (!DestroyedChar) return;

    APawn** KillerPawnPtr = KillerMap.Find(DestroyedChar);
    if (!KillerPawnPtr) return;

    APawn* KillerPawn = *KillerPawnPtr;
    KillerMap.Remove(DestroyedChar);

    if (!KillerPawn) return;

    UTelemetryCollector* KillerCollector =
        KillerPawn->FindComponentByClass<UTelemetryCollector>();
    if (KillerCollector)
        KillerCollector->RecordKill();
}

// ============================================================
//  Helper
// ============================================================

AShooterGameState* AShooterGameMode::GetShooterGameState() const
{
    return GetGameState<AShooterGameState>();
}
