#include "TelemetryCollector.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"
#include "Algo/MaxElement.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

UTelemetryCollector::UTelemetryCollector()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f;

    MaxLegalSpeed       = 600.f;
    SamplingInterval    = 0.1f;
    EnemyCheckInterval  = 0.2f;
    EnemyCheckDistance  = 5000.f;

    // Default headshot bone names — covers most UE5 humanoid skeletons
    HeadshotBoneNames = { TEXT("head"), TEXT("Head"), TEXT("HEAD"), TEXT("neck_01"), TEXT("neck_02") };

    PlayerID  = TEXT("Unknown");
    Label     = 0;

    SessionStartTime        = 0.f;
    LastSampleTime          = 0.f;
    EnemyVisibleTimestamp   = 0.f;
    bWaitingForReactionShot = false;
    LastShotTimestamp       = -1.f;
    LastEnemyCheckTime      = 0.f;
    AimFlipCount            = 0;
    DirectionChangeCount    = 0;
    SpeedViolationCount     = 0;

    TotalShots     = 0;
    TotalHits      = 0;
    TotalHeadshots = 0;
    TotalKills     = 0;
    TotalDeaths    = 0;
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

    float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

    // ---- Aim sampling ----
    FVector CurrentViewDir = Owner->GetBaseAimRotation().Vector();
    CurrentViewDir.Normalize();

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

    // ---- Movement sampling ----
    UCharacterMovementComponent* Movement = Owner->GetCharacterMovement();
    if (Movement)
    {
        float Speed = Movement->Velocity.Size();
        MovementSpeeds.Add(Speed);

        if (Speed > MaxLegalSpeed + 10.f)
        {
            SpeedViolationCount++;
        }

        FVector CurrentMoveDir = Movement->Velocity;
        if (CurrentMoveDir.SizeSquared() > 1.f)
        {
            CurrentMoveDir.Normalize();
            float MoveAngleDelta = FMath::Acos(
                FMath::Clamp(FVector::DotProduct(LastMoveDirection, CurrentMoveDir), -1.f, 1.f)
            );
            if (FMath::RadiansToDegrees(MoveAngleDelta) > 45.f)
            {
                DirectionChangeCount++;
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

void UTelemetryCollector::RecordShot()
{
    float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
    TotalShots++;

    if (LastShotTimestamp > 0.f)
    {
        ShotIntervals.Add(Now - LastShotTimestamp);
    }
    LastShotTimestamp = Now;

    if (bWaitingForReactionShot)
    {
        RecordFirstShotAfterVisible();
    }
}

void UTelemetryCollector::RecordHit(bool bIsHeadshot)
{
    TotalHits++;
    if (bIsHeadshot) TotalHeadshots++;
}

void UTelemetryCollector::RecordHitWithBone(FName HitBoneName)
{
    TotalHits++;
    if (IsHeadshotBone(HitBoneName))
    {
        TotalHeadshots++;
        UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] Headshot! Bone: %s"), *HitBoneName.ToString());
    }
}

void UTelemetryCollector::RecordKill()
{
    TotalKills++;
}

void UTelemetryCollector::RecordDeath()
{
    TotalDeaths++;
}

void UTelemetryCollector::RecordEnemyVisible()
{
    // Only start a new reaction timer if we're not already waiting for a shot
    if (!bWaitingForReactionShot)
    {
        EnemyVisibleTimestamp   = UGameplayStatics::GetTimeSeconds(GetWorld());
        bWaitingForReactionShot = true;
    }
}

void UTelemetryCollector::RecordFirstShotAfterVisible()
{
    if (!bWaitingForReactionShot) return;

    float ReactionTime = UGameplayStatics::GetTimeSeconds(GetWorld()) - EnemyVisibleTimestamp;
    if (ReactionTime <= 5.f && ReactionTime >= 0.f)
    {
        ReactionTimes.Add(ReactionTime);
    }
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

void UTelemetryCollector::FinalizeSession(UTelemetryLogger* Logger)
{
    if (!Logger) return;

    float Now             = UGameplayStatics::GetTimeSeconds(GetWorld());
    float SessionDuration = Now - SessionStartTime;

    if (SessionDuration < 1.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TelemetryCollector] Session too short (<1s), skipping: %s"), *PlayerID);
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
        TEXT("[TelemetryCollector] Session finalized for %s: %.1fs, %d shots, %d kills, %d headshots, %d reaction samples"),
        *PlayerID, SessionDuration, TotalShots, TotalKills, TotalHeadshots, ReactionTimes.Num());
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
        ShooterCollector->RecordHit(false);
        UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] RecordHit für Schütze: %s → Opfer: %s"),
            *InstigatorPawn->GetName(), *DamagedActor->GetName());
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
