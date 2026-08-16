#include "TelemetryCollector.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"
#include "Algo/MaxElement.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ShooterGameState.h"
#include "ShooterPlayerState.h"

UTelemetryCollector::UTelemetryCollector()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;

    MaxLegalSpeed       = 600.f;
    SamplingInterval    = 0.1f;
    EnemyCheckInterval  = 0.2f;
    EnemyCheckDistance  = 5000.f;
    AimErrorMaxAngle    = 45.f;

    // Default headshot bone names — covers most UE5 humanoid skeletons
    HeadshotBoneNames = { TEXT("head"), TEXT("Head"), TEXT("HEAD"), TEXT("neck_01"), TEXT("neck_02") };

    PlayerID          = TEXT("Unknown");
    Label             = 0;
    bRecordingEnabled  = false;  // Erst aktiv wenn GameMode InProgress meldet
    bSessionFinalized  = false;

    SessionStartTime        = 0.f;
    AccumulatedDuration     = 0.f;
    LastSampleTime          = 0.f;
    EnemyVisibleTimestamp   = 0.f;
    bWaitingForReactionShot = false;
    LastShotTimestamp       = -1.f;
    LastEnemyCheckTime      = 0.f;
    AimFlipCount            = 0;
    AimErrorSampleCount     = 0;
    DirectionChangeCount    = 0;
    SpeedViolationCount     = 0;
    bHasAimReference        = false;
    bHasMoveReference       = false;

    TotalShots     = 0;
    TotalHits      = 0;
    TotalHeadshots = 0;
    TotalKills     = 0;
    TotalDeaths    = 0;

    LastHitTime = -999.f;
    HitCooldown = 0.30f; // ignore repeated hits within 300ms from same bullet
}

void UTelemetryCollector::BeginPlay()
{
    Super::BeginPlay();

    if (GetOwner() && GetOwner()->HasAuthority())
    {
        SessionStartTime   = UGameplayStatics::GetTimeSeconds(GetWorld());
        LastSampleTime     = SessionStartTime;
        LastEnemyCheckTime = SessionStartTime;

        if (ACharacter* Owner = Cast<ACharacter>(GetOwner()))
        {
            LastViewDirection = Owner->GetActorForwardVector();
            LastMoveDirection = Owner->GetActorForwardVector();

            // AnyDamage Delegate direkt hier binden — Pawn ist garantiert gespawnt
            // Wird aufgerufen wenn dieser Charakter Schaden bekommt
            // Wir nutzen es um RecordHit auf dem Schützen (Instigator) aufzurufen
            if (!Owner->OnTakeAnyDamage.IsAlreadyBound(this, &UTelemetryCollector::OnOwnerTakeAnyDamage))
            {
                Owner->OnTakeAnyDamage.AddDynamic(this, &UTelemetryCollector::OnOwnerTakeAnyDamage);
            }
        }

        UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] Session started for %s"), *PlayerID);
    }
    else
    {
        PrimaryComponentTick.bCanEverTick = false;
    }
}

void UTelemetryCollector::TickComponent(float DeltaTime, ELevelTick TickType,
                                        FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    ACharacter* Owner = Cast<ACharacter>(GetOwner());
    if (!Owner || !Owner->HasAuthority()) return;

    // Auto-resolve PlayerID and auto-enable recording if game is InProgress
    AController* Ctrl = Owner->GetController();
    AShooterPlayerState* PS = Ctrl ? Cast<AShooterPlayerState>(Ctrl->PlayerState) : nullptr;
    if (!PS)
    {
        PS = Cast<AShooterPlayerState>(Owner->GetPlayerState());
    }

    if (PS)
    {
        FString CurrentName = PS->GetPlayerName();
        // If we don't have a valid ID yet, or it was the default engine name, update it when a better name is available.
        if (PlayerID == TEXT("Unknown") || PlayerID.StartsWith(TEXT("ShooterPlayerState")))
        {
            if (!CurrentName.IsEmpty() && !CurrentName.StartsWith(TEXT("ShooterPlayerState")))
            {
                PlayerID = CurrentName;
            }
            else if (PlayerID == TEXT("Unknown") && !CurrentName.IsEmpty())
            {
                PlayerID = CurrentName;
            }
        }

        if (!bRecordingEnabled)
        {
            UWorld* World = GetWorld();
            AShooterGameState* GS = World ? Cast<AShooterGameState>(World->GetGameState()) : nullptr;
            if (GS && GS->GamePhase == EShooterGamePhase::InProgress)
            {
                SetRecordingEnabled(true);
            }
        }
    }

    if (!bRecordingEnabled) return;

    float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

    // ---- Aim sampling ----
    FVector CurrentViewDir = Owner->GetBaseAimRotation().Vector();
    CurrentViewDir.Normalize();

    if (!bHasAimReference)
    {
        // Erster Tick dieses Pawns: nur die Referenz setzen, keine Differenz werten.
        // Andernfalls würde der Sprung vom Default-Vorwärtsvektor auf die echte
        // Blickrichtung nach jedem Respawn als Aim-Flip gezählt.
        LastViewDirection = CurrentViewDir;
        bHasAimReference  = true;
    }
    else
    {
        float AngleDelta = FMath::Acos(
            FMath::Clamp(FVector::DotProduct(LastViewDirection, CurrentViewDir), -1.f, 1.f)
        );
        float AngleDeltaDeg = FMath::RadiansToDegrees(AngleDelta);
        float AngularSpeed  = AngleDeltaDeg / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);
        AimAngularSpeeds.Add(AngularSpeed);

        if (AngleDeltaDeg > 90.f)
        {
            AimFlipCount++;
        }
        LastViewDirection = CurrentViewDir;
    }

    // ---- Movement sampling ----
    UCharacterMovementComponent* Movement = Owner->GetCharacterMovement();
    if (Movement)
    {
        // Nur die horizontale Geschwindigkeit auswerten.
        // Velocity.Size() würde die Z-Komponente einschließen: bei ~980 cm/s^2
        // Gravitation überschreitet jeder Sprung/Fall nach knapp 0,6s das
        // Bodenlimit von MaxLegalSpeed und erzeugt einen Scheinverstoß.
        // Speedhacks wirken auf die Bodenbewegung — die Fallgeschwindigkeit ist
        // für alle Spieler identisch und darf nicht als Verstoß zählen.
        const FVector HorizontalVelocity(Movement->Velocity.X, Movement->Velocity.Y, 0.f);
        const float Speed = HorizontalVelocity.Size();
        MovementSpeeds.Add(Speed);

        if (Speed > MaxLegalSpeed + 10.f)
        {
            SpeedViolationCount++;
        }

        // Richtungswechsel ebenfalls in der Bodenebene messen: der Vorzeichen-
        // wechsel von Velocity.Z am Sprungscheitel würde sonst als starke
        // Richtungsänderung gewertet, obwohl die Laufrichtung gleich bleibt.
        FVector CurrentMoveDir = HorizontalVelocity;
        if (CurrentMoveDir.SizeSquared() > 1.f)
        {
            CurrentMoveDir.Normalize();

            // Wie beim Aim: erste Bewegung nach einem (Re-)Spawn nur als Referenz
            // setzen, sonst zählt der Vergleich gegen den Default-Vorwärtsvektor
            // als Richtungswechsel.
            if (!bHasMoveReference)
            {
                bHasMoveReference = true;
            }
            else
            {
                float MoveAngleDelta = FMath::Acos(
                    FMath::Clamp(FVector::DotProduct(LastMoveDirection, CurrentMoveDir), -1.f, 1.f)
                );
                if (FMath::RadiansToDegrees(MoveAngleDelta) > 45.f)
                {
                    DirectionChangeCount++;
                }
            }

            float HeadingAngle = FMath::Atan2(CurrentMoveDir.Y, CurrentMoveDir.X);
            MovementAngles.Add(FMath::RadiansToDegrees(HeadingAngle) + 180.f);
            LastMoveDirection = CurrentMoveDir;
        }
    }

    // ---- Enemy Line of Sight check (every EnemyCheckInterval seconds) ----
    if (Now - LastEnemyCheckTime >= EnemyCheckInterval)
    {
        CheckEnemyLineOfSight();
        LastEnemyCheckTime = Now;
    }

    LastSampleTime = Now;
}

void UTelemetryCollector::SetRecordingEnabled(bool bEnabled)
{
    bRecordingEnabled = bEnabled;

    if (bEnabled)
    {
        ResetSession();
        SessionStartTime        = UGameplayStatics::GetTimeSeconds(GetWorld());
        LastSampleTime          = SessionStartTime;
        LastEnemyCheckTime      = SessionStartTime;
    }

    UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] Recording %s für: %s"),
        bEnabled ? TEXT("aktiviert") : TEXT("deaktiviert"), *PlayerID);
}

void UTelemetryCollector::ResetSession()
{
    bSessionFinalized = false;
    AccumulatedDuration     = 0.f;
    SessionStartTime        = 0.f;
    LastSampleTime          = 0.f;
    EnemyVisibleTimestamp   = 0.f;
    bWaitingForReactionShot = false;
    LastShotTimestamp       = -1.f;
    LastEnemyCheckTime      = 0.f;
    AimFlipCount            = 0;
    AimErrorSampleCount     = 0;
    DirectionChangeCount    = 0;
    SpeedViolationCount     = 0;
    bHasAimReference        = false;
    bHasMoveReference       = false;

    AimAngularSpeeds.Empty();
    AimAngularErrors.Empty();
    MovementSpeeds.Empty();
    MovementAngles.Empty();
    ReactionTimes.Empty();
    ShotIntervals.Empty();

    TotalShots     = 0;
    TotalHits      = 0;
    TotalHeadshots = 0;
    TotalKills     = 0;
    TotalDeaths    = 0;
    LastHitTime    = -999.f;

    UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] Session zurückgesetzt für: %s"), *PlayerID);
}

void UTelemetryCollector::RecordShot()
{
    UE_LOG(LogTemp, Log, TEXT("[RecordShot] Player=%s | bEnabled=%d | TotalShots_vorher=%d"),
        *PlayerID, (int32)bRecordingEnabled, TotalShots);
    if (!bRecordingEnabled) return;

    float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
    TotalShots++;

    // Winkelfehler zum Fadenkreuz-nächsten sichtbaren Ziel im Moment des Schusses.
    // Kernmerkmal für Aimbot-Erkennung: menschliches Zielen streut, ein Aimbot
    // liefert nahezu konstant ~0 Grad Abweichung.
    SampleAimError();

    if (LastShotTimestamp > 0.f)
        ShotIntervals.Add(Now - LastShotTimestamp);
    LastShotTimestamp = Now;

    if (bWaitingForReactionShot)
        RecordFirstShotAfterVisible();
}

void UTelemetryCollector::RecordHit(bool bIsHeadshot)
{
    if (!bRecordingEnabled) return;
    TotalHits++;
    if (bIsHeadshot) TotalHeadshots++;
}

void UTelemetryCollector::RecordHitWithBone(FName HitBoneName)
{
    if (!bRecordingEnabled) return;
    TotalHits++;
    if (IsHeadshotBone(HitBoneName))
    {
        TotalHeadshots++;
        UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] Headshot! Bone: %s"), *HitBoneName.ToString());
    }
}

void UTelemetryCollector::RecordKill()
{
    UE_LOG(LogTemp, Warning, TEXT("[RecordKill] Player=%s | bFinalized=%d | TotalKills_vorher=%d"),
        *PlayerID, (int32)bSessionFinalized, TotalKills);
    if (bSessionFinalized)  return;
    TotalKills++;
    UE_LOG(LogTemp, Warning, TEXT("[RecordKill] => TotalKills jetzt: %d"), TotalKills);
}

void UTelemetryCollector::RecordDeath()
{
    if (bSessionFinalized)  return;
    TotalDeaths++;
    UE_LOG(LogTemp, Warning, TEXT("[RecordDeath] Player=%s TotalDeaths=%d"), *PlayerID, TotalDeaths);
}

void UTelemetryCollector::RecordEnemyVisible()
{
    if (!bRecordingEnabled) return;
    if (!bWaitingForReactionShot)
    {
        EnemyVisibleTimestamp   = UGameplayStatics::GetTimeSeconds(GetWorld());
        bWaitingForReactionShot = true;
    }
}

void UTelemetryCollector::RecordFirstShotAfterVisible()
{
    if (!bRecordingEnabled || !bWaitingForReactionShot) return;

    float ReactionTime = UGameplayStatics::GetTimeSeconds(GetWorld()) - EnemyVisibleTimestamp;
    if (ReactionTime <= 5.f && ReactionTime >= 0.f)
        ReactionTimes.Add(ReactionTime);
    bWaitingForReactionShot = false;
}

void UTelemetryCollector::SetPlayerID(const FString& InPlayerID)
{
    PlayerID = InPlayerID;
}

void UTelemetryCollector::SetLabel(int32 InLabel)
{
    Label = InLabel;
}

void UTelemetryCollector::MergeTelemetry(UTelemetryCollector* SourceCollector)
{
    if (!SourceCollector) return;

    // Merge raw arrays
    AimAngularSpeeds.Append(SourceCollector->AimAngularSpeeds);
    AimAngularErrors.Append(SourceCollector->AimAngularErrors);
    MovementSpeeds.Append(SourceCollector->MovementSpeeds);
    MovementAngles.Append(SourceCollector->MovementAngles);
    ReactionTimes.Append(SourceCollector->ReactionTimes);
    ShotIntervals.Append(SourceCollector->ShotIntervals);

    // Merge counters
    AimFlipCount         += SourceCollector->AimFlipCount;
    AimErrorSampleCount  += SourceCollector->AimErrorSampleCount;
    DirectionChangeCount += SourceCollector->DirectionChangeCount;
    SpeedViolationCount  += SourceCollector->SpeedViolationCount;
    TotalShots           += SourceCollector->TotalShots;
    TotalHits            += SourceCollector->TotalHits;
    TotalHeadshots       += SourceCollector->TotalHeadshots;
    TotalKills           += SourceCollector->TotalKills;
    TotalDeaths          += SourceCollector->TotalDeaths;

    float SourceDuration = SourceCollector->LastSampleTime - SourceCollector->SessionStartTime;
    if (SourceDuration > 0.f)
    {
        AccumulatedDuration += SourceDuration;
    }
}

void UTelemetryCollector::FinalizeSession(UTelemetryLogger* Logger)
{
    if (!Logger) return;
    if (bSessionFinalized) return;
    bSessionFinalized = true;

    float Now             = UGameplayStatics::GetTimeSeconds(GetWorld());
    float SessionDuration = AccumulatedDuration;
    if (bRecordingEnabled)
    {
        SessionDuration += (Now - SessionStartTime);
    }

    if (SessionDuration < 0.1f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TelemetryCollector] Session too short (<0.1s), skipping: %s"), *PlayerID);
        return;
    }

    FPlayerSessionData Data;
    Data.PlayerID               = PlayerID;
    Data.Label                  = Label;
    Data.SessionDurationSeconds = SessionDuration;

    // ---- Aim Features ----
    float AimSpeedMean          = ComputeMean(AimAngularSpeeds);
    Data.AimAngularSpeedMean    = AimSpeedMean;
    Data.AimAngularSpeedStdDev  = ComputeStdDev(AimAngularSpeeds, AimSpeedMean);

    float AimErrMean            = ComputeMean(AimAngularErrors);
    Data.AimAngularErrorMean    = AimErrMean;
    Data.AimAngularErrorStdDev  = ComputeStdDev(AimAngularErrors, AimErrMean);
    Data.AimErrorSampleCount    = AimErrorSampleCount;

    int32 TotalSamples          = FMath::Max(AimAngularSpeeds.Num(), 1);
    Data.AimFlipRatio           = (float)AimFlipCount / (float)TotalSamples;

    // ---- Movement Features ----
    float SpeedMean             = ComputeMean(MovementSpeeds);
    Data.MovementSpeedMean      = SpeedMean;
    Data.MovementSpeedMax       = MovementSpeeds.Num() > 0
                                  ? *Algo::MaxElement(MovementSpeeds) : 0.f;

    Data.DirectionChangesPerSecond = SessionDuration > 0.f
                                     ? (float)DirectionChangeCount / SessionDuration : 0.f;

    int32 MovSamples            = FMath::Max(MovementSpeeds.Num(), 1);
    Data.SpeedViolationRatio    = (float)SpeedViolationCount / (float)MovSamples;
    Data.MovementPathEntropy    = ComputeEntropy(MovementAngles, 8);

    // ---- Timing Features ----
    float RTMean                = ComputeMean(ReactionTimes);
    Data.ReactionTimeMean       = RTMean;
    Data.ReactionTimeStdDev     = ComputeStdDev(ReactionTimes, RTMean);

    float SIMean                = ComputeMean(ShotIntervals);
    Data.ShotIntervalMean       = SIMean;
    Data.ShotIntervalStdDev     = ComputeStdDev(ShotIntervals, SIMean);

    Data.ShotsPerSecond         = SessionDuration > 0.f
                                  ? (float)TotalShots / SessionDuration : 0.f;

    // ---- Rate Features ----
    Data.HitRate        = TotalShots > 0 ? (float)TotalHits / (float)TotalShots : 0.f;
    Data.HeadshotRate   = TotalHits  > 0 ? (float)TotalHeadshots / (float)TotalHits : 0.f;
    Data.KillsPerMinute = SessionDuration > 0.f
                          ? (float)TotalKills / (SessionDuration / 60.f) : 0.f;
    Data.KillDeathRatio = TotalDeaths > 0
                          ? (float)TotalKills / (float)TotalDeaths : (float)TotalKills;

    Data.TotalShots  = TotalShots;
    Data.TotalHits   = TotalHits;
    Data.TotalKills  = TotalKills;
    Data.TotalDeaths = TotalDeaths;

    Logger->LogSessionData(Data);

    UE_LOG(LogTemp, Log,
        TEXT("[TelemetryCollector] Session finalized for %s: %.1fs, %d shots, %d kills, %d headshots, "
             "%d reaction samples, %d aim-error samples (mean %.2f Grad, std %.2f)"),
        *PlayerID, SessionDuration, TotalShots, TotalKills, TotalHeadshots, ReactionTimes.Num(),
        AimErrorSampleCount, Data.AimAngularErrorMean, Data.AimAngularErrorStdDev);
}

// ---- Private Helpers ----

void UTelemetryCollector::OnOwnerTakeAnyDamage(AActor* DamagedActor, float Damage,
                                                const UDamageType* DamageType,
                                                AController* InstigatedBy, AActor* DamageCauser)
{
    // Nur tracken wenn jemand anderes den Schaden verursacht hat
    if (!InstigatedBy || Damage <= 0.f) return;

    APawn* InstigatorPawn = InstigatedBy->GetPawn();
    if (!InstigatorPawn) return;

    // Nicht sich selbst treffen
    if (InstigatorPawn == GetOwner()) return;

    // RecordHit direkt auf diesem Collector aufrufen —
    // dieser Collector gehört dem Opfer, aber wir wollen den Schützen tracken.
    // Deshalb: den TelemetryCollector des Schützen (Instigator) suchen und dort RecordHit aufrufen.
    // Falls der Schütze ein Spieler ist → hat TelemetryCollector
    // Falls der Schütze ein NPC ist → hat keinen TelemetryCollector → ignorieren
    UTelemetryCollector* ShooterCollector =
        InstigatorPawn->FindComponentByClass<UTelemetryCollector>();
    if (ShooterCollector)
    {
        // Dedup: ignore repeated damage events within HitCooldown seconds
        // (projectile overlaps can fire multiple frames for a single hit)
        float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
        if (Now - ShooterCollector->LastHitTime >= ShooterCollector->HitCooldown)
        {
            ShooterCollector->LastHitTime = Now;
            ShooterCollector->RecordHit(false);
            UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] RecordHit für Schütze: %s → Opfer: %s"),
                *InstigatorPawn->GetName(), *DamagedActor->GetName());
        }
    }
}

// HINWEIS: Dieser Callback läuft auf dem OPFER wenn es Schaden bekommt.
// Der Schütze (Instigator) ist der Spieler — wir suchen seinen TelemetryCollector.
// NPCs haben keinen TelemetryCollector → ihre Treffer werden hier nicht gezählt.
// Für NPC-Hits auf Spieler: der Spieler ist das Opfer, NPC ist Instigator → kein Collector → korrekt ignoriert.

void UTelemetryCollector::CheckEnemyLineOfSight()
{
    ACharacter* Owner = Cast<ACharacter>(GetOwner());
    if (!Owner) return;

    UWorld* World = GetWorld();
    if (!World) return;

    // Start from camera / eye location
    FVector StartLocation = Owner->GetPawnViewLocation();
    FVector EndLocation   = StartLocation + Owner->GetBaseAimRotation().Vector() * EnemyCheckDistance;

    FHitResult HitResult;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Owner);

    bool bHit = World->LineTraceSingleByChannel(
        HitResult,
        StartLocation,
        EndLocation,
        ECC_Pawn,
        Params
    );

    if (bHit && HitResult.GetActor())
    {
        // Check if the hit actor is an NPC/enemy (not the player themselves)
        // We check for a Character that is NOT the owner
        ACharacter* HitCharacter = Cast<ACharacter>(HitResult.GetActor());
        if (HitCharacter && HitCharacter != Owner)
        {
            RecordEnemyVisible();
        }
    }
}

void UTelemetryCollector::SampleAimError()
{
    ACharacter* Owner = Cast<ACharacter>(GetOwner());
    if (!Owner) return;

    UWorld* World = GetWorld();
    if (!World) return;

    const FVector EyeLocation = Owner->GetPawnViewLocation();

    // HINWEIS (Messgenauigkeit): GetBaseAimRotation() nutzt für entfernte Clients
    // die replizierte RemoteViewPitch, die auf 1 Byte komprimiert ist (~1.4 Grad
    // Auflösung im Pitch). Der Winkelfehler ist dadurch bei Remote-Clients
    // grobkörniger als beim Listen-Server-Host. Für die Trennung
    // Aimbot (~0 Grad) vs. Mensch (mehrere Grad) reicht die Auflösung aus.
    FVector ViewDir = Owner->GetBaseAimRotation().Vector();
    ViewDir.Normalize();

    float BestAngleDeg = TNumericLimits<float>::Max();
    bool  bFoundTarget = false;

    for (TActorIterator<ACharacter> It(World); It; ++It)
    {
        ACharacter* Target = *It;
        if (!IsValid(Target) || Target == Owner) continue;

        // Ziele ohne Controller sind bereits tot / nicht mehr steuerbar
        if (!Target->GetController()) continue;

        const float Distance = FVector::Dist(Target->GetActorLocation(), EyeLocation);
        if (Distance < KINDA_SMALL_NUMBER || Distance > EnemyCheckDistance) continue;

        // Kleinsten Winkel über mehrere Körperpunkte bestimmen (Torso + Kopf).
        // Würde nur gegen die Kapselmitte gemessen, erschiene ein auf den Kopf
        // rastender Aimbot mit einem systematischen Versatz als "ungenau"
        // (bei ~3,4 m Distanz rund 10 Grad — genau der Wert, der im Aimbot-Test
        // gemessen wurde, während der Cheat nachweislich aktiv war).
        TArray<FVector> AimPoints;
        GetTargetAimPoints(Target, AimPoints);

        float AngleDeg = TNumericLimits<float>::Max();
        for (const FVector& AimPoint : AimPoints)
        {
            FVector ToPoint = AimPoint - EyeLocation;
            if (!ToPoint.Normalize()) continue;

            const float PointAngle = FMath::RadiansToDegrees(
                FMath::Acos(FMath::Clamp(FVector::DotProduct(ViewDir, ToPoint), -1.f, 1.f))
            );
            AngleDeg = FMath::Min(AngleDeg, PointAngle);
        }

        // Außerhalb des Kegels, oder bereits ein näheres Ziel gefunden →
        // teuren Sichtlinien-Trace sparen
        if (AngleDeg > AimErrorMaxAngle || AngleDeg >= BestAngleDeg) continue;

        if (!HasLineOfSightTo(Target, EyeLocation)) continue;

        BestAngleDeg = AngleDeg;
        bFoundTarget = true;
    }

    if (bFoundTarget)
    {
        AimAngularErrors.Add(BestAngleDeg);
        AimErrorSampleCount++;
        UE_LOG(LogTemp, Log, TEXT("[AimError] Player=%s | Winkelfehler=%.2f Grad | Stichproben=%d"),
            *PlayerID, BestAngleDeg, AimErrorSampleCount);
    }
    else
    {
        UE_LOG(LogTemp, Verbose, TEXT("[AimError] Player=%s | kein sichtbares Ziel im %.0f-Grad-Kegel"),
            *PlayerID, AimErrorMaxAngle);
    }
}

void UTelemetryCollector::GetTargetAimPoints(const ACharacter* Target, TArray<FVector>& OutPoints) const
{
    if (!Target) return;

    // Kapselmitte (Torso) — immer verfügbar
    OutPoints.Add(Target->GetActorLocation());

    // Kopfposition, falls das Skelett einen der bekannten Kopfknochen besitzt.
    // Dieselbe Namensliste wie für die Headshot-Erkennung.
    if (const USkeletalMeshComponent* Mesh = Target->GetMesh())
    {
        for (const FName& HeadBone : HeadshotBoneNames)
        {
            if (Mesh->GetBoneIndex(HeadBone) != INDEX_NONE)
            {
                OutPoints.Add(Mesh->GetBoneLocation(HeadBone));
                break;
            }
        }
    }
}

bool UTelemetryCollector::HasLineOfSightTo(const AActor* Target, const FVector& EyeLocation) const
{
    UWorld* World = GetWorld();
    if (!World || !Target) return false;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(GetOwner());
    Params.AddIgnoredActor(Target);

    // Blockiert Level-Geometrie die Sicht zwischen Auge und Zielmittelpunkt?
    FHitResult Blocker;
    const bool bBlocked = World->LineTraceSingleByChannel(
        Blocker,
        EyeLocation,
        Target->GetActorLocation(),
        ECC_Visibility,
        Params
    );

    return !bBlocked;
}

bool UTelemetryCollector::IsHeadshotBone(FName BoneName) const
{
    for (const FName& HeadBone : HeadshotBoneNames)
    {
        if (BoneName == HeadBone) return true;
    }
    return false;
}

float UTelemetryCollector::ComputeMean(const TArray<float>& Values) const
{
    if (Values.Num() == 0) return 0.f;
    float Sum = 0.f;
    for (float V : Values) Sum += V;
    return Sum / Values.Num();
}

float UTelemetryCollector::ComputeStdDev(const TArray<float>& Values, float Mean) const
{
    if (Values.Num() < 2) return 0.f;
    float Variance = 0.f;
    for (float V : Values) Variance += (V - Mean) * (V - Mean);
    return FMath::Sqrt(Variance / Values.Num());
}

float UTelemetryCollector::ComputeEntropy(const TArray<float>& Values, int32 NumBins) const
{
    if (Values.Num() == 0) return 0.f;

    float MinVal = Values[0], MaxVal = Values[0];
    for (float V : Values)
    {
        MinVal = FMath::Min(MinVal, V);
        MaxVal = FMath::Max(MaxVal, V);
    }

    float Range = MaxVal - MinVal;
    if (Range < KINDA_SMALL_NUMBER) return 0.f;

    TArray<int32> Bins;
    Bins.SetNumZeroed(NumBins);

    for (float V : Values)
    {
        int32 BinIdx = FMath::Clamp(
            (int32)(((V - MinVal) / Range) * NumBins),
            0, NumBins - 1
        );
        Bins[BinIdx]++;
    }

    float Entropy = 0.f;
    float N = (float)Values.Num();
    for (int32 Count : Bins)
    {
        if (Count > 0)
        {
            float P = (float)Count / N;
            Entropy -= P * FMath::Log2(P);
        }
    }

    return Entropy;
}
