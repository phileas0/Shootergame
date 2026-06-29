// TelemetryCollector.cpp
// ════════════════════════════════════════════════════════════════════════
//  BEZUG ZUR BACHELORARBEIT
//  ────────────────────────
//  Diese Datei ist der "Sensor" des Systems. Sie tastet während des Spiels
//  laufend das Verhalten ab (Aim, Movement, Timing) und verdichtet es am Ende
//  zum Feature-Vektor x ∈ R^n (Kap. 2.1). Die zentrale Idee der Arbeit steckt
//  in den StdDev-Features: menschliches Verhalten streut, maschinelles (Bot)
//  ist fast konstant (StdDev ≈ 0) → das trennbare Signal (Kap. 7).
//  Alles läuft serverseitig (HasAuthority), daher manipulationsresistent (Kap. 1.1).
// ════════════════════════════════════════════════════════════════════════

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
    // Wir wollen, dass diese Komponente jeden Frame (bzw. im vorgegebenen Intervall) mitläuft,
    // da wir kontinuierlich Maus- und Tastatureingaben (Aim & Movement) abgreifen müssen.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.1f; // 10 Mal pro Sekunde sammeln wir einen Datenpunkt

    // Grundlegende Schwellenwerte für die Cheat-Erkennung
    MaxLegalSpeed       = 600.f; // Alles über 600 cm/s im Spiel deutet potenziell auf Speedhacks hin
    SamplingInterval    = 0.1f;
    EnemyCheckInterval  = 0.2f;  // Alle 0.2s werfen wir einen Raycast, um zu prüfen, ob ein Gegner im Bild ist
    EnemyCheckDistance  = 5000.f; // Maximale Sichtweite für die Erkennung in cm (50 Meter)

    // Standard-Knochen, die als Kopfschuss zählen. Deckt die meisten Standard-UE5-Skelette ab.
    HeadshotBoneNames = { TEXT("head"), TEXT("Head"), TEXT("HEAD"), TEXT("neck_01"), TEXT("neck_02") };

    // Standardwerte für eine frische Session
    PlayerID  = TEXT("Unknown");
    Label     = 0; // 0 = Legitim, 1 = Cheater (wichtig für die ML-Trainingsdaten; = y, Kap. 2.2)

    // Initialisierung aller Timer und Zustände
    SessionStartTime        = 0.f;
    LastSampleTime          = 0.f;
    EnemyVisibleTimestamp   = 0.f;
    bWaitingForReactionShot = false;
    LastShotTimestamp       = -1.f;  // -1 = "noch kein Schuss" → erstes ShotInterval wird nicht verfälscht
    LastEnemyCheckTime      = 0.f;

    // Zähler auf 0 setzen
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

    // ⚑ Kap. 1.1 (Systemabgrenzung): Wir sammeln Daten nur auf dem Server (Authority),
    //    damit Clients die Daten nicht manipulieren können. Das ist die technische
    //    Kernbedingung der ganzen Arbeit — verhaltensbasiert UND serverseitig.
    if (GetOwner() && GetOwner()->HasAuthority())
    {
        // Startzeitpunkte festhalten
        SessionStartTime   = UGameplayStatics::GetTimeSeconds(GetWorld());
        LastSampleTime     = SessionStartTime;
        LastEnemyCheckTime = SessionStartTime;

        if (ACharacter* Owner = Cast<ACharacter>(GetOwner()))
        {
            // Initiale Blick- und Laufrichtung speichern, damit wir im nächsten Tick die Änderung (Delta) berechnen können
            LastViewDirection = Owner->GetActorForwardVector();
            LastMoveDirection = Owner->GetActorForwardVector();

            // Ganz wichtiger Trick: Wir binden das OnTakeAnyDamage Event direkt hier im Collector an den Spieler.
            // Sobald dieser Charakter Schaden ERHÄLT, feuert OnOwnerTakeAnyDamage.
            // So können wir tracken, WER auf uns geschossen hat, ohne den Code im fremden Charakter anfassen zu müssen.
            // (Redundant zur Bindung im GameMode — doppelte Sicherheit, der IsAlreadyBound-Check verhindert Doppelzählung.)
            if (!Owner->OnTakeAnyDamage.IsAlreadyBound(this, &UTelemetryCollector::OnOwnerTakeAnyDamage))
            {
                Owner->OnTakeAnyDamage.AddDynamic(this, &UTelemetryCollector::OnOwnerTakeAnyDamage);
            }
        }

        UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] Session started for %s"), *PlayerID);
    }
    else
    {
        // Auf den Clients schalten wir den Tick komplett ab, um Performance zu sparen.
        // (Auf dem Client gäbe es ohnehin keine vertrauenswürdigen Daten — siehe Kap. 1.1.)
        PrimaryComponentTick.bCanEverTick = false;
    }
}

void UTelemetryCollector::TickComponent(float DeltaTime, ELevelTick TickType,
                                        FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    ACharacter* Owner = Cast<ACharacter>(GetOwner());
    if (!Owner || !Owner->HasAuthority()) return;   // erneuter Server-Check (Sicherheit)

    float Now = UGameplayStatics::GetTimeSeconds(GetWorld());

    // ==========================================
    // 1. AIM-DATEN SAMMELN (Mausbewegungen)
    // ==========================================
    FVector CurrentViewDir = Owner->GetBaseAimRotation().Vector();
    CurrentViewDir.Normalize();   // Einheitsvektor → DotProduct liefert sauberen Cosinus

    // Wir berechnen den Winkel zwischen dem letzten und dem aktuellen Frame.
    // Das DotProduct liefert uns den Cosinus des Winkels, Acos wandelt es in den Bogenmaß-Winkel um.
    // Clamp(-1,1) verhindert NaN durch Rundungsfehler (Acos ist nur auf [-1,1] definiert).
    float AngleDelta = FMath::Acos(
        FMath::Clamp(FVector::DotProduct(LastViewDirection, CurrentViewDir), -1.f, 1.f)
    );
    float AngleDeltaDeg = FMath::RadiansToDegrees(AngleDelta);

    // Winkeländerung pro Sekunde (Winkelgeschwindigkeit). Aimbots haben hier oft unrealistisch konstante oder sprunghafte Werte.
    // Max(DeltaTime, KINDA_SMALL_NUMBER) verhindert Division durch 0.
    float AngularSpeed  = AngleDeltaDeg / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);
    AimAngularSpeeds.Add(AngularSpeed);   // → wird zu AimAngularSpeedMean/StdDev (x_i, Kap. 2.1)

    // Ein "Flip" passiert, wenn der Spieler sich in einem Bruchteil einer Sekunde um mehr als 90 Grad dreht.
    // Ein Mensch macht das in der Regel nicht präzise im Gefecht, ein Aimbot, der auf ein Ziel hinter sich aufschaltet, schon.
    if (AngleDeltaDeg > 90.f)
    {
        AimFlipCount++;
    }
    LastViewDirection = CurrentViewDir;   // für das nächste Delta merken

    // ==========================================
    // 2. MOVEMENT-DATEN SAMMELN (Tastatureingaben/Laufen)
    // ==========================================
    UCharacterMovementComponent* Movement = Owner->GetCharacterMovement();
    if (Movement)
    {
        float Speed = Movement->Velocity.Size();   // Betrag des Geschwindigkeitsvektors (cm/s)
        MovementSpeeds.Add(Speed);

        // Wir tolerieren +10cm/s wegen Physik-Jitter. Wenn er schneller ist, verbuchen wir das als Speedhack-Vorfalls.
        // → SpeedViolationCount wird später zu SpeedViolationRatio (Haupt-Speedhack-Feature).
        if (Speed > MaxLegalSpeed + 10.f)
        {
            SpeedViolationCount++;
        }

        FVector CurrentMoveDir = Movement->Velocity;
        if (CurrentMoveDir.SizeSquared() > 1.f) // Nur messen, wenn wir uns auch wirklich bewegen (Stillstand würde Winkel verfälschen)
        {
            CurrentMoveDir.Normalize();
            float MoveAngleDelta = FMath::Acos(
                FMath::Clamp(FVector::DotProduct(LastMoveDirection, CurrentMoveDir), -1.f, 1.f)
            );

            // Plötzliche Richtungswechsel (>45 Grad) aufzeichnen. Zick-Zack-Muster können auf Bots/Scripts hindeuten.
            if (FMath::RadiansToDegrees(MoveAngleDelta) > 45.f)
            {
                DirectionChangeCount++;
            }

            // Gesamte Bewegungsrichtung speichern für die Entropy-Berechnung (Unvorhersehbarkeit des Pfades).
            // Atan2 liefert den Heading-Winkel; +180 verschiebt ihn nach [0,360] (positiv für die Histogramm-Bins).
            float HeadingAngle = FMath::Atan2(CurrentMoveDir.Y, CurrentMoveDir.X);
            MovementAngles.Add(FMath::RadiansToDegrees(HeadingAngle) + 180.f);
            LastMoveDirection = CurrentMoveDir;
        }
    }

    // ==========================================
    // 3. SICHTLINIE ZUM FEIND PRÜFEN (Reaction Time)
    // ==========================================
    // Um Performance zu sparen, machen wir den Raycast nicht jeden Tick, sondern nur im eingestellten Intervall (z.B. alle 0.2s).
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

    // Zeit seit dem letzten Schuss speichern. Extrem wichtig für die Erkennung von Triggerbots!
    // Triggerbots schießen exakt im selben Millisekunden-Takt, Menschen haben immer eine kleine Abweichung.
    // → ShotIntervals wird zu ShotIntervalStdDev; bei Bots nahe 0 (vgl. generate_training_data.py gen_triggerbot).
    if (LastShotTimestamp > 0.f)   // erst ab dem zweiten Schuss gibt es ein Intervall
    {
        ShotIntervals.Add(Now - LastShotTimestamp);
    }
    LastShotTimestamp = Now;

    // Falls ein Feind ins Sichtfeld gerückt ist und wir auf den ersten Schuss warten,
    // ist das genau jetzt passiert. Reaktionszeit stoppen!
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
    // Wir prüfen, ob der Name des getroffenen Knochens in unserer Headshot-Liste steht (z.B. "head", "neck_01")
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
    // Diese Funktion wird vom LineTrace (CheckEnemyLineOfSight) aufgerufen.
    // Wenn wir nicht ohnehin schon auf einen Schuss warten, fangen wir jetzt an, die Zeit zu stoppen.
    // Der bWaitingForReactionShot-Guard verhindert, dass die Stoppuhr ständig neu startet,
    // solange derselbe Gegner sichtbar bleibt (sonst würde die Reaktionszeit künstlich klein).
    if (!bWaitingForReactionShot)
    {
        EnemyVisibleTimestamp   = UGameplayStatics::GetTimeSeconds(GetWorld());
        bWaitingForReactionShot = true;
    }
}

void UTelemetryCollector::RecordFirstShotAfterVisible()
{
    if (!bWaitingForReactionShot) return;

    // Der Schuss fiel! Die Reaktionszeit ist die Differenz zwischen "Gegner taucht auf" und "Schuss gelöst".
    // → ReactionTimes wird zu ReactionTimeMean/StdDev. Aimbots: sehr kurz UND sehr konstant (StdDev≈0).
    float ReactionTime = UGameplayStatics::GetTimeSeconds(GetWorld()) - EnemyVisibleTimestamp;

    // Sanity-Check: Ignoriere offensichtlichen Quatsch (z.B. über 5 Sekunden).
    // Solche Ausreißer würden Mean/StdDev verzerren → bewusst herausfiltern (saubere Feature-Definition).
    if (ReactionTime <= 5.f && ReactionTime >= 0.f)
    {
        ReactionTimes.Add(ReactionTime);
    }
    bWaitingForReactionShot = false; // Zurücksetzen für den nächsten Gegner
}

void UTelemetryCollector::SetPlayerID(const FString& InPlayerID)
{
    PlayerID = InPlayerID;
}

void UTelemetryCollector::SetLabel(int32 InLabel)
{
    // Dient nur für die Trainingsdatengenerierung: 0 = Normaler Spieler, 1 = Cheater (= y, Kap. 2.2)
    Label = InLabel;
}

void UTelemetryCollector::FinalizeSession(UTelemetryLogger* Logger)
{
    // ⚑ KERNFUNKTION: Hier wird aus den Roh-Arrays der fertige Feature-Vektor x ∈ R^n
    //    gebaut (Kap. 2.1). Das Ergebnis ist genau eine Zeile des Trainings-/Test-Datensatzes.
    if (!Logger) return;

    float Now             = UGameplayStatics::GetTimeSeconds(GetWorld());
    float SessionDuration = Now - SessionStartTime;

    // Kurze Sessions (unter 1 Sekunde) ignorieren wir, da sie keine aussagekräftigen ML-Features liefern.
    // (Zu wenige Samples → Mean/StdDev statistisch wertlos.)
    if (SessionDuration < 1.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("[TelemetryCollector] Session too short (<1s), skipping: %s"), *PlayerID);
        return;
    }

    // Wir füllen jetzt unsere finale Struktur, die später in die CSV-Datei geschrieben wird.
    // FPlayerSessionData ist die formale Repräsentation von x (definiert in TelemetryLogger.h).
    FPlayerSessionData Data;
    Data.PlayerID               = PlayerID;
    Data.Label                  = Label;
    Data.SessionDurationSeconds = SessionDuration;

    // ==========================================
    // 1. AIM FEATURES BERECHNEN
    // ==========================================
    // Wir berechnen den Durchschnitt (Mean) und die Standardabweichung (StdDev) unserer Samples.
    // Die Standardabweichung ist für uns entscheidend, da Maschinen konstant zielen, während Menschen zittern/schwanken.
    float AimSpeedMean          = ComputeMean(AimAngularSpeeds);
    Data.AimAngularSpeedMean    = AimSpeedMean;
    Data.AimAngularSpeedStdDev  = ComputeStdDev(AimAngularSpeeds, AimSpeedMean);

    float AimErrMean            = ComputeMean(AimAngularErrors);
    Data.AimAngularErrorMean    = AimErrMean;
    Data.AimAngularErrorStdDev  = ComputeStdDev(AimAngularErrors, AimErrMean);

    int32 TotalSamples          = FMath::Max(AimAngularSpeeds.Num(), 1);  // Max(...,1) verhindert Division durch 0
    Data.AimFlipRatio           = (float)AimFlipCount / (float)TotalSamples; // Wie oft wurden harte >90-Grad-Drehungen gemacht?

    // ==========================================
    // 2. MOVEMENT FEATURES BERECHNEN
    // ==========================================
    float SpeedMean             = ComputeMean(MovementSpeeds);
    Data.MovementSpeedMean      = SpeedMean;
    Data.MovementSpeedMax       = MovementSpeeds.Num() > 0
                                  ? *Algo::MaxElement(MovementSpeeds) : 0.f; // Max-Wert der Geschwindigkeit raussuchen

    // Wie oft pro Sekunde ändert der Spieler abrupt die Richtung?  (normiert auf die Sessiondauer → vergleichbar)
    Data.DirectionChangesPerSecond = SessionDuration > 0.f
                                     ? (float)DirectionChangeCount / SessionDuration : 0.f;

    int32 MovSamples            = FMath::Max(MovementSpeeds.Num(), 1);
    Data.SpeedViolationRatio    = (float)SpeedViolationCount / (float)MovSamples; // Anteil an "illegal schnellen" Frames
    Data.MovementPathEntropy    = ComputeEntropy(MovementAngles, 8); // Entropie misst die Unvorhersehbarkeit des Laufwegs

    // ==========================================
    // 3. TIMING FEATURES BERECHNEN
    // ==========================================
    float RTMean                = ComputeMean(ReactionTimes);
    Data.ReactionTimeMean       = RTMean;
    Data.ReactionTimeStdDev     = ComputeStdDev(ReactionTimes, RTMean); // Triggerbots haben hier eine StdDev nahe 0

    float SIMean                = ComputeMean(ShotIntervals);
    Data.ShotIntervalMean       = SIMean;
    Data.ShotIntervalStdDev     = ComputeStdDev(ShotIntervals, SIMean);

    Data.ShotsPerSecond         = SessionDuration > 0.f
                                  ? (float)TotalShots / SessionDuration : 0.f;

    // ==========================================
    // 4. RATE FEATURES BERECHNEN
    // ==========================================
    // Quoten statt Rohzahlen → unabhängig von der Sessiondauer, fair vergleichbar zwischen Spielern.
    Data.HitRate        = TotalShots > 0 ? (float)TotalHits / (float)TotalShots : 0.f;
    Data.HeadshotRate   = TotalHits  > 0 ? (float)TotalHeadshots / (float)TotalHits : 0.f;
    Data.KillsPerMinute = SessionDuration > 0.f
                          ? (float)TotalKills / (SessionDuration / 60.f) : 0.f;

    // K/D Ratio berechnen. Wir lassen Fallback auf TotalKills, um Division durch 0 bei perfekten Runden zu vermeiden.
    Data.KillDeathRatio = TotalDeaths > 0
                          ? (float)TotalKills / (float)TotalDeaths : (float)TotalKills;

    Data.TotalShots  = TotalShots;
    Data.TotalHits   = TotalHits;
    Data.TotalKills  = TotalKills;
    Data.TotalDeaths = TotalDeaths;

    // Session an den Logger übergeben, der sie dann später in die CSV pumpt (→ ein Datenpunkt für das ML-Modell).
    Logger->LogSessionData(Data);

    UE_LOG(LogTemp, Log,
        TEXT("[TelemetryCollector] Session finalized for %s: %.1fs, %d shots, %d kills, %d headshots, %d reaction samples"),
        *PlayerID, SessionDuration, TotalShots, TotalKills, TotalHeadshots, ReactionTimes.Num());
}

// ==========================================
// PRIVATE HILFSFUNKTIONEN UND DELEGATES
// ==========================================

void UTelemetryCollector::OnOwnerTakeAnyDamage(AActor* DamagedActor, float Damage,
                                                const UDamageType* DamageType,
                                                AController* InstigatedBy, AActor* DamageCauser)
{
    // Zur Erinnerung: Diese Funktion läuft auf dem OPFER, wenn es getroffen wird.
    // Wir wollen aber Hit-Stats für den SCHÜTZEN aufzeichnen.

    // Nur tracken, wenn jemand anderes den Schaden verursacht hat und der Schaden > 0 ist.
    if (!InstigatedBy || Damage <= 0.f) return;

    APawn* InstigatorPawn = InstigatedBy->GetPawn();
    if (!InstigatorPawn) return;

    // Suizid oder Eigenschaden ausklammern (würde HitRate künstlich aufblähen).
    if (InstigatorPawn == GetOwner()) return;

    // Wir suchen uns nun den TelemetryCollector des SCHÜTZEN (Instigator).
    // - Wenn der Schütze ein NPC ist, findet er keinen Collector und bricht ab (Korrekt so!).
    // - Wenn der Schütze ein Spieler ist, rufen wir auf SEINEM Collector RecordHit auf.
    UTelemetryCollector* ShooterCollector =
        InstigatorPawn->FindComponentByClass<UTelemetryCollector>();

    if (ShooterCollector)
    {
        ShooterCollector->RecordHit(false);
        UE_LOG(LogTemp, Log, TEXT("[TelemetryCollector] RecordHit für Schütze: %s → Opfer: %s"),
            *InstigatorPawn->GetName(), *DamagedActor->GetName());
    }
}

// HINWEIS: Da NPCs keinen Collector haben, fließen PVE-Treffer nicht fälschlicherweise in die Hit-Rate
// des NPCs ein. Die Spieler-Treffer auf NPCs werden wiederum über deren Damage-Empfang korrekt dem Spieler-Collector zugeordnet.

void UTelemetryCollector::CheckEnemyLineOfSight()
{
    ACharacter* Owner = Cast<ACharacter>(GetOwner());
    if (!Owner) return;

    UWorld* World = GetWorld();
    if (!World) return;

    // Wir schießen einen Raycast (LineTrace) aus der Sicht des Spielers in den Raum.
    FVector StartLocation = Owner->GetPawnViewLocation(); // Startpunkt = Augenhöhe
    FVector EndLocation   = StartLocation + Owner->GetBaseAimRotation().Vector() * EnemyCheckDistance;

    FHitResult HitResult;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(Owner); // Uns selbst ignorieren, sonst treffen wir den eigenen Kopf

    // Strahl abfeuern. ECC_Pawn = wir reagieren nur auf Spielfiguren, nicht auf Wände/Deko.
    bool bHit = World->LineTraceSingleByChannel(
        HitResult,
        StartLocation,
        EndLocation,
        ECC_Pawn, // Wir scannen nur nach anderen Spielfiguren (Pawns)
        Params
    );

    if (bHit && HitResult.GetActor())
    {
        // Wir haben etwas getroffen. Ist es ein anderer Charakter?
        ACharacter* HitCharacter = Cast<ACharacter>(HitResult.GetActor());
        if (HitCharacter && HitCharacter != Owner)
        {
            // Ein Gegner ist im Fadenkreuz! Wir triggern die Aufzeichnung der Reaktionszeit.
            RecordEnemyVisible();
        }
    }
}

bool UTelemetryCollector::IsHeadshotBone(FName BoneName) const
{
    // Iteriere durch unsere vorkonfigurierte Liste von Kopf/Hals-Bones
    for (const FName& HeadBone : HeadshotBoneNames)
    {
        if (BoneName == HeadBone) return true;
    }
    return false;
}

// ------------------------------------------
// MATHEMATISCHE FUNKTIONEN FÜR ML-FEATURES
// ------------------------------------------
// Diese drei Funktionen sind die eigentliche Feature-Extraktion (Kap. 2.1):
// sie verdichten je ein Roh-Array zu einer Zahl, die ins CSV/zum Modell geht.

float UTelemetryCollector::ComputeMean(const TArray<float>& Values) const
{
    // Berechnet den simplen arithmetischen Durchschnitt:  μ = (1/N) · Σ x_i
    if (Values.Num() == 0) return 0.f;
    float Sum = 0.f;
    for (float V : Values) Sum += V;
    return Sum / Values.Num();
}

float UTelemetryCollector::ComputeStdDev(const TArray<float>& Values, float Mean) const
{
    // Berechnet die Standardabweichung (Wurzel der Varianz):  σ = sqrt( (1/N) · Σ (x_i − μ)² )
    // ⚑ Kap. 7 (Adversarial): DAS diskriminativste Feature überhaupt. Sehr wichtig für ML:
    //   Zeigt, wie stark die Werte um den Durchschnitt schwanken.
    //   Maschinelle Cheats (Aimbots, Macros) haben meist eine StdDev nahe 0 (keine menschliche Streuung).
    // Hinweis: <2 Werte → 0 (Streuung ist erst ab 2 Messpunkten definiert). Es wird die
    //   Populations-Varianz (Teiler N) verwendet, konsistent mit der Datengenerierung.
    if (Values.Num() < 2) return 0.f;
    float Variance = 0.f;
    for (float V : Values) Variance += (V - Mean) * (V - Mean);
    return FMath::Sqrt(Variance / Values.Num());
}

float UTelemetryCollector::ComputeEntropy(const TArray<float>& Values, int32 NumBins) const
{
    // Shannon-Entropie berechnet die Unvorhersehbarkeit (z.B. des Laufweges).
    // Wir werfen die Bewegungswinkel in Bins ("Körbe") und schauen, wie gleichmäßig sie verteilt sind.
    // Jemand, der nur stumpf geradeaus (W+Shift) läuft, hat eine Entropie nahe 0.
    // Jemand, der natürlich und komplex navigiert, hat eine höhere Entropie.
    if (Values.Num() == 0) return 0.f;

    // 1) Wertebereich [Min, Max] bestimmen (für die Bin-Grenzen).
    float MinVal = Values[0], MaxVal = Values[0];
    for (float V : Values)
    {
        MinVal = FMath::Min(MinVal, V);
        MaxVal = FMath::Max(MaxVal, V);
    }

    float Range = MaxVal - MinVal;
    if (Range < KINDA_SMALL_NUMBER) return 0.f;   // alle Werte gleich → keine Streuung → Entropie 0

    TArray<int32> Bins;
    Bins.SetNumZeroed(NumBins);   // NumBins leere Körbe anlegen

    // 2) Werte in das Histogramm einsortieren (Wert → Bin-Index)
    for (float V : Values)
    {
        int32 BinIdx = FMath::Clamp(
            (int32)(((V - MinVal) / Range) * NumBins),
            0, NumBins - 1   // Clamp fängt den Randfall V==MaxVal ab (sonst Index == NumBins)
        );
        Bins[BinIdx]++;
    }

    // 3) Shannon-Formel:  H = − Σ p · log2(p)   mit  p = Anteil der Werte im Bin
    float Entropy = 0.f;
    float N = (float)Values.Num();
    for (int32 Count : Bins)
    {
        if (Count > 0)   // log2(0) ist undefiniert → leere Bins überspringen
        {
            float P = (float)Count / N;
            Entropy -= P * FMath::Log2(P);
        }
    }

    return Entropy;
}
