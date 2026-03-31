#include "TelemetryCollector.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"
#include "Algo/MaxElement.h"

UTelemetryCollector::UTelemetryCollector()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f; // Sample every 100ms by default

    MaxLegalSpeed    = 600.f;   // cm/s — adjust to match BP_ShooterCharacter max walk speed
    SamplingInterval = 0.1f;

    PlayerID  = TEXT("Unknown");
    Label     = 0;

    SessionStartTime       = 0.f;
    LastSampleTime         = 0.f;
    EnemyVisibleTimestamp  = 0.f;
    bWaitingForReactionShot = false;
    LastShotTimestamp      = -1.f;
    AimFlipCount           = 0;
    DirectionChangeCount   = 0;
    SpeedViolationCount    = 0;

    TotalShots     = 0;
    TotalHits      = 0;
    TotalHeadshots = 0;
    TotalKills     = 0;
    TotalDeaths    = 0;
}

void UTelemetryCollector::BeginPlay()
{
    Super::BeginPlay();

    // Only collect data on the server (authoritative)
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        SessionStartTime = UGameplayStatics::GetTimeSeconds(GetWorld());
        LastSampleTime   = SessionStartTime;

        // Initialize view direction
        if (ACharacter* Owner = Cast<ACharacter>(GetOwner()))
        {
            LastViewDirection = Owner->GetActorForwardVector();
            LastMoveDirection = Owner->GetActorForwardVector();
        }

        UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] Session started for %s"), *PlayerID);
    }
    else
    {
        // Disable ticking on clients — server only
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

    float AngularSpeed = AngleDeltaDeg / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);
    AimAngularSpeeds.Add(AngularSpeed);

    // Detect abrupt flips (>90 degrees in one tick)
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

        // Speed violation check
        if (Speed > MaxLegalSpeed + 10.f) // 10 cm/s tolerance
        {
            SpeedViolationCount++;
        }

        // Direction change detection
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

            // Record heading angle for path entropy
            float HeadingAngle = FMath::Atan2(CurrentMoveDir.Y, CurrentMoveDir.X);
            MovementAngles.Add(FMath::RadiansToDegrees(HeadingAngle) + 180.f); // shift to 0-360

            LastMoveDirection = CurrentMoveDir;
        }
    }

    LastSampleTime = Now;
}

void UTelemetryCollector::RecordShot()
{
    float Now = UGameplayStatics::GetTimeSeconds(GetWorld());
    TotalShots++;

    // Shot interval
    if (LastShotTimestamp > 0.f)
    {
        ShotIntervals.Add(Now - LastShotTimestamp);
    }
    LastShotTimestamp = Now;

    // Reaction time: was this the first shot after an enemy became visible?
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
    EnemyVisibleTimestamp  = UGameplayStatics::GetTimeSeconds(GetWorld());
    bWaitingForReactionShot = true;
}

void UTelemetryCollector::RecordFirstShotAfterVisible()
{
    if (!bWaitingForReactionShot) return;

    float ReactionTime = UGameplayStatics::GetTimeSeconds(GetWorld()) - EnemyVisibleTimestamp;
    // Cap at 5 seconds — anything longer is not a reaction to that enemy sighting
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

    float Now            = UGameplayStatics::GetTimeSeconds(GetWorld());
    float SessionDuration = Now - SessionStartTime;

    if (SessionDuration < 1.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TelemetryCollector] Session too short (<1s), skipping: %s"), *PlayerID);
        return;
    }

    FPlayerSessionData Data;
    Data.PlayerID              = PlayerID;
    Data.Label                 = Label;
    Data.SessionDurationSeconds = SessionDuration;

    // ---- Aim Features ----
    float AimSpeedMean = ComputeMean(AimAngularSpeeds);
    Data.AimAngularSpeedMean   = AimSpeedMean;
    Data.AimAngularSpeedStdDev = ComputeStdDev(AimAngularSpeeds, AimSpeedMean);

    float AimErrMean = ComputeMean(AimAngularErrors);
    Data.AimAngularErrorMean   = AimErrMean;
    Data.AimAngularErrorStdDev = ComputeStdDev(AimAngularErrors, AimErrMean);

    int32 TotalSamples = FMath::Max(AimAngularSpeeds.Num(), 1);
    Data.AimFlipRatio  = (float)AimFlipCount / (float)TotalSamples;

    // ---- Movement Features ----
    float SpeedMean = ComputeMean(MovementSpeeds);
    Data.MovementSpeedMean = SpeedMean;
    Data.MovementSpeedMax  = MovementSpeeds.Num() > 0
                             ? *Algo::MaxElement(MovementSpeeds)
                             : 0.f;

    Data.DirectionChangesPerSecond = SessionDuration > 0.f
                                     ? (float)DirectionChangeCount / SessionDuration
                                     : 0.f;

    int32 MovSamples = FMath::Max(MovementSpeeds.Num(), 1);
    Data.SpeedViolationRatio  = (float)SpeedViolationCount / (float)MovSamples;
    Data.MovementPathEntropy  = ComputeEntropy(MovementAngles, 8);

    // ---- Timing Features ----
    float RTMean = ComputeMean(ReactionTimes);
    Data.ReactionTimeMean   = RTMean;
    Data.ReactionTimeStdDev = ComputeStdDev(ReactionTimes, RTMean);

    float SIMean = ComputeMean(ShotIntervals);
    Data.ShotIntervalMean   = SIMean;
    Data.ShotIntervalStdDev = ComputeStdDev(ShotIntervals, SIMean);

    Data.ShotsPerSecond = SessionDuration > 0.f
                          ? (float)TotalShots / SessionDuration
                          : 0.f;

    // ---- Rate Features ----
    Data.HitRate       = TotalShots > 0 ? (float)TotalHits / (float)TotalShots : 0.f;
    Data.HeadshotRate  = TotalHits > 0  ? (float)TotalHeadshots / (float)TotalHits : 0.f;
    Data.KillsPerMinute = SessionDuration > 0.f
                          ? (float)TotalKills / (SessionDuration / 60.f)
                          : 0.f;
    Data.KillDeathRatio = TotalDeaths > 0
                          ? (float)TotalKills / (float)TotalDeaths
                          : (float)TotalKills;

    Data.TotalShots  = TotalShots;
    Data.TotalHits   = TotalHits;
    Data.TotalKills  = TotalKills;
    Data.TotalDeaths = TotalDeaths;

    Logger->LogSessionData(Data);

    UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] Session finalized for %s: %.1fs, %d shots, %d kills"),
           *PlayerID, SessionDuration, TotalShots, TotalKills);
}

// ---- Private Helpers ----

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

    // Find range
    float MinVal = Values[0], MaxVal = Values[0];
    for (float V : Values)
    {
        MinVal = FMath::Min(MinVal, V);
        MaxVal = FMath::Max(MaxVal, V);
    }

    float Range = MaxVal - MinVal;
    if (Range < KINDA_SMALL_NUMBER) return 0.f; // all same value = zero entropy

    // Bin the values
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

    // Compute Shannon entropy: H = -sum(p * log2(p))
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
