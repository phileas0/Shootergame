#include "ShooterGameMode.h"
#include "TelemetryCollector.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "Misc/DateTime.h"
#include "EngineUtils.h"

AShooterGameMode::AShooterGameMode()
{
    TelemetryLogger = nullptr;
    CSVFilename     = TEXT("session_01");
}

void AShooterGameMode::BeginPlay()
{
    Super::BeginPlay();
    TelemetryLogger = NewObject<UTelemetryLogger>(this, UTelemetryLogger::StaticClass());

    // Alle bereits gespawnten Actors auf AnyDamage binden
    // Neue Actors werden über OnActorSpawned erfasst
    if (UWorld* World = GetWorld())
    {
        World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &AShooterGameMode::OnActorSpawned));

        // Bereits vorhandene Charaktere binden
        for (TActorIterator<ACharacter> It(World); It; ++It)
        {
            ACharacter* Char = *It;
            if (!Char->OnTakeAnyDamage.IsAlreadyBound(this, &AShooterGameMode::OnPawnTakeAnyDamage))
            {
                Char->OnTakeAnyDamage.AddDynamic(this, &AShooterGameMode::OnPawnTakeAnyDamage);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[ShooterGameMode] TelemetryLogger erstellt."));
}

void AShooterGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    FlushAllSessionsToCSV();
    Super::EndPlay(EndPlayReason);
}

void AShooterGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    // Pawn ist hier noch nicht gespawnt — Delegate-Bind in HandleStartingNewPlayer
}

void AShooterGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    Super::HandleStartingNewPlayer_Implementation(NewPlayer);

    if (!NewPlayer) return;

    // Kurz warten bis Pawn wirklich da ist
    APawn* Pawn = NewPlayer->GetPawn();
    if (!Pawn)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ShooterGameMode] HandleStartingNewPlayer: Pawn noch null für %s"),
            *NewPlayer->GetName());
        return;
    }

    // AnyDamage Delegate binden — wird aufgerufen wenn dieser Pawn Schaden bekommt
    if (!Pawn->OnTakeAnyDamage.IsAlreadyBound(this, &AShooterGameMode::OnPawnTakeAnyDamage))
    {
        Pawn->OnTakeAnyDamage.AddDynamic(this, &AShooterGameMode::OnPawnTakeAnyDamage);
        UE_LOG(LogTemp, Log, TEXT("[ShooterGameMode] AnyDamage-Delegate gebunden für: %s"),
            *Pawn->GetName());
    }
}

void AShooterGameMode::OnActorSpawned(AActor* SpawnedActor)
{
    // Nur Charaktere (Spieler + NPCs) binden
    ACharacter* Char = Cast<ACharacter>(SpawnedActor);
    if (!Char) return;

    if (!Char->OnTakeAnyDamage.IsAlreadyBound(this, &AShooterGameMode::OnPawnTakeAnyDamage))
    {
        Char->OnTakeAnyDamage.AddDynamic(this, &AShooterGameMode::OnPawnTakeAnyDamage);
        UE_LOG(LogTemp, Log, TEXT("[ShooterGameMode] Damage-Delegate gebunden für: %s"),
            *Char->GetName());
    }
}

void AShooterGameMode::OnPawnTakeAnyDamage(AActor* DamagedActor, float Damage,
                                            const UDamageType* DamageType,
                                            AController* InstigatedBy, AActor* DamageCauser)
{
    if (!InstigatedBy || Damage <= 0.f) return;

    APawn* InstigatorPawn = InstigatedBy->GetPawn();
    if (!InstigatorPawn) return;

    // Nicht sich selbst treffen
    if (InstigatorPawn == DamagedActor) return;

    // TelemetryCollector des Schützen holen
    UTelemetryCollector* ShooterCollector =
        InstigatorPawn->FindComponentByClass<UTelemetryCollector>();
    if (!ShooterCollector) return;

    // RecordHit auf dem Schützen
    ShooterCollector->RecordHit(false);
    UE_LOG(LogTemp, Log, TEXT("[ShooterGameMode] RecordHit für Schütze: %s → Opfer: %s"),
        *InstigatorPawn->GetName(), *DamagedActor->GetName());

    // Kill prüfen: ist das Opfer ein Charakter und hat er 0 HP?
    ACharacter* DamagedChar = Cast<ACharacter>(DamagedActor);
    if (DamagedChar)
    {
        // Prüfe ob der Charakter nach diesem Schaden tot ist
        // UE5 ruft AnyDamage VOR dem Tod auf — wir prüfen über ein Timer-Delay
        // Einfacherer Weg: prüfen ob DamagedActor pending kill oder health <= 0
        // Da wir keinen direkten HP-Zugriff haben, nutzen wir IsPendingKillPending
        // Das geht nicht zuverlässig — stattdessen binden wir OnDestroyed
        if (!DamagedChar->OnDestroyed.IsAlreadyBound(this, &AShooterGameMode::OnCharacterDestroyed))
        {
            // Speichere Instigator für den Destroyed-Callback
            KillerMap.Add(DamagedChar, InstigatorPawn);
            DamagedChar->OnDestroyed.AddDynamic(this, &AShooterGameMode::OnCharacterDestroyed);
        }
    }
}

void AShooterGameMode::OnCharacterDestroyed(AActor* DestroyedActor)
{
    ACharacter* DestroyedChar = Cast<ACharacter>(DestroyedActor);
    if (!DestroyedChar) return;

    // Killer aus der Map holen
    APawn** KillerPawnPtr = KillerMap.Find(DestroyedChar);
    if (!KillerPawnPtr) return;

    APawn* KillerPawn = *KillerPawnPtr;
    KillerMap.Remove(DestroyedChar);

    if (!KillerPawn) return;

    // RecordKill auf dem Killer
    UTelemetryCollector* KillerCollector =
        KillerPawn->FindComponentByClass<UTelemetryCollector>();
    if (KillerCollector)
    {
        KillerCollector->RecordKill();
        UE_LOG(LogTemp, Log, TEXT("[ShooterGameMode] RecordKill für: %s → hat getötet: %s"),
            *KillerPawn->GetName(), *DestroyedActor->GetName());
    }
}

void AShooterGameMode::OnPlayerSessionEnd(AActor* DyingCharacter, AActor* KillerActor)
{
    if (!DyingCharacter || !TelemetryLogger) return;

    // RecordDeath auf dem Sterbenden
    UTelemetryCollector* VictimCollector =
        DyingCharacter->FindComponentByClass<UTelemetryCollector>();
    if (VictimCollector)
    {
        VictimCollector->RecordDeath();
        VictimCollector->FinalizeSession(TelemetryLogger);
        UE_LOG(LogTemp, Log,
            TEXT("[ShooterGameMode] RecordDeath + Session finalisiert für: %s"),
            *DyingCharacter->GetName());
    }

    // RecordKill auf dem Killer (falls vorhanden und ein anderer Charakter)
    if (KillerActor && KillerActor != DyingCharacter)
    {
        UTelemetryCollector* KillerCollector =
            KillerActor->FindComponentByClass<UTelemetryCollector>();
        if (KillerCollector)
        {
            KillerCollector->RecordKill();
            UE_LOG(LogTemp, Log,
                TEXT("[ShooterGameMode] RecordKill für Killer: %s"),
                *KillerActor->GetName());
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[ShooterGameMode] Buffer: %d Session(en)."),
        TelemetryLogger->GetBufferedSessionCount());
}

UTelemetryLogger* AShooterGameMode::GetTelemetryLogger() const
{
    return TelemetryLogger;
}

void AShooterGameMode::FlushAllSessionsToCSV()
{
    if (!TelemetryLogger) return;

    // Noch lebende Spieler finalisieren
    UWorld* World = GetWorld();
    if (World)
    {
        for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = It->Get();
            if (!PC) continue;
            APawn* Pawn = PC->GetPawn();
            if (!Pawn) continue;
            UTelemetryCollector* Collector =
                Pawn->FindComponentByClass<UTelemetryCollector>();
            if (Collector)
            {
                Collector->FinalizeSession(TelemetryLogger);
            }
        }
    }

    if (TelemetryLogger->GetBufferedSessionCount() > 0)
    {
        FString Timestamp     = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S"));
        FString FinalFilename = FString::Printf(TEXT("session_%s"), *Timestamp);
        TelemetryLogger->FlushToCSV(FinalFilename);
        UE_LOG(LogTemp, Log,
            TEXT("[ShooterGameMode] CSV gespeichert: Saved/Telemetry/%s.csv"),
            *FinalFilename);
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ShooterGameMode] Buffer leer — keine CSV erstellt."));
    }
}
