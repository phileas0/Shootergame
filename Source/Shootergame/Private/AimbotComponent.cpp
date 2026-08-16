#include "AimbotComponent.h"

#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

UAimbotComponent::UAimbotComponent()
{
    PrimaryComponentTick.bCanEverTick = true;

    bAimbotEnabled      = false;
    bLoggedNoTarget     = false;
    MaxTargetDistance   = 5000.f;

    // Standardmaessig aus: der Aimbot zielt auch durch Waende und haelt den
    // Lock ununterbrochen. Deckung unterbricht damit weder das Zielen noch
    // das Durchschalten. Auf true gesetzt zielt er nur bei freier Sicht.
    bRequireLineOfSight = false;
    bAimAtHead          = true;
    bPlayersOnly        = true;
    AimInterpSpeed      = 0.f;   // 0 = hartes Einrasten

    HeadBoneNames = { TEXT("head"), TEXT("Head"), TEXT("HEAD"), TEXT("neck_01") };
}

void UAimbotComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                     FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bAimbotEnabled) return;

    ACharacter* Owner = Cast<ACharacter>(GetOwner());
    if (!Owner) return;

    // Nur auf der Instanz laufen lassen, die den Pawn tatsächlich steuert.
    // Die gedrehte Blickrichtung repliziert dann über den normalen Weg —
    // dadurch sieht der TelemetryCollector auf dem Server exakt das, was
    // er auch bei einem echten Cheat sehen würde.
    if (!Owner->IsLocallyControlled()) return;

    // Ziel verloren (tot, disconnected, ausser Reichweite) → automatisch
    // weiterschalten, damit der Aimbot nicht stumm ins Leere zeigt.
    if (!IsValidTarget(LockedTarget.Get()))
    {
        LockedTarget.Reset();
        CycleTarget(+1);
    }

    ApplyAim(DeltaTime);
}

void UAimbotComponent::SetAimbotEnabled(bool bEnabled)
{
    bAimbotEnabled = bEnabled;

    if (!bAimbotEnabled)
    {
        LockedTarget.Reset();
    }

    UE_LOG(LogTemp, Warning, TEXT("[Aimbot] %s"),
           bAimbotEnabled ? TEXT("AKTIV") : TEXT("aus"));
}

bool UAimbotComponent::ToggleAimbot()
{
    SetAimbotEnabled(!bAimbotEnabled);
    return bAimbotEnabled;
}

void UAimbotComponent::ClearTarget()
{
    LockedTarget.Reset();
}

bool UAimbotComponent::NextTarget()
{
    return CycleTarget(+1);
}

bool UAimbotComponent::PreviousTarget()
{
    return CycleTarget(-1);
}

bool UAimbotComponent::CycleTarget(int32 Direction)
{
    TArray<ACharacter*> Targets;
    BuildTargetList(Targets);

    if (Targets.Num() == 0)
    {
        LockedTarget.Reset();

        // Nur beim Wechsel in den Zustand loggen, nicht bei jedem Tick
        if (!bLoggedNoTarget)
        {
            UE_LOG(LogTemp, Warning,
                   TEXT("[Aimbot] Kein gueltiges Ziel gefunden "
                        "(Distanz <= %.0f, Sichtlinie %s, nur Spieler %s)."),
                   MaxTargetDistance,
                   bRequireLineOfSight ? TEXT("noetig") : TEXT("egal"),
                   bPlayersOnly ? TEXT("ja") : TEXT("nein"));
            bLoggedNoTarget = true;
        }
        return false;
    }

    bLoggedNoTarget = false;

    // Position des aktuellen Ziels suchen. INDEX_NONE = noch kein Ziel oder
    // das bisherige ist aus der Liste gefallen → bei 0 beginnen.
    const int32 CurrentIndex = Targets.IndexOfByKey(LockedTarget.Get());

    int32 NewIndex;
    if (CurrentIndex == INDEX_NONE)
    {
        NewIndex = (Direction >= 0) ? 0 : Targets.Num() - 1;
    }
    else
    {
        // Umlauf in beide Richtungen. Der zusaetzliche Summand haelt das
        // Ergebnis bei negativer Richtung positiv — in C++ ist -1 % n = -1.
        NewIndex = (CurrentIndex + Direction + Targets.Num()) % Targets.Num();
    }

    LockedTarget = Targets[NewIndex];

    UE_LOG(LogTemp, Warning, TEXT("[Aimbot] Ziel %d/%d: %s"),
           NewIndex + 1, Targets.Num(), *GetLockedTargetName());

    return true;
}

void UAimbotComponent::BuildTargetList(TArray<ACharacter*>& OutTargets) const
{
    OutTargets.Reset();

    const ACharacter* Owner = Cast<ACharacter>(GetOwner());
    UWorld* World = GetWorld();
    if (!Owner || !World) return;

    for (TActorIterator<ACharacter> It(World); It; ++It)
    {
        ACharacter* Candidate = *It;
        if (Candidate == Owner) continue;
        if (!IsValidTarget(Candidate)) continue;

        OutTargets.Add(Candidate);
    }

    // Stabile Reihenfolge (siehe Kommentar in der Header-Datei): nach
    // Entfernung zu sortieren wuerde das Durchschalten unbrauchbar machen.
    OutTargets.Sort([](const ACharacter& A, const ACharacter& B)
    {
        return GetStableSortKey(&A) < GetStableSortKey(&B);
    });
}

int32 UAimbotComponent::GetStableSortKey(const ACharacter* Target)
{
    if (!Target) return MAX_int32;

    if (const APlayerState* PS = Target->GetPlayerState())
    {
        return PS->GetPlayerId();
    }

    // Rueckfallebene fuer Charaktere ohne PlayerState (KI): die Objekt-ID
    // ist innerhalb einer Sitzung konstant.
    return static_cast<int32>(Target->GetUniqueID());
}

bool UAimbotComponent::IsValidTarget(const ACharacter* Target) const
{
    if (!IsValid(Target)) return false;

    const ACharacter* Owner = Cast<ACharacter>(GetOwner());
    if (!Owner || Target == Owner) return false;

    // Der Controller ist NUR auf dem Server gesetzt — auf einem Client sind
    // die Controller fremder Pawns nicht repliziert und immer nullptr.
    // Die Pruefung darf deshalb ausschliesslich mit Authority laufen, sonst
    // findet ein cheatender Client grundsaetzlich kein einziges Ziel.
    if (Target->HasAuthority() && !Target->GetController()) return false;

    // Der PlayerState wird dagegen zu allen Clients repliziert und ist damit
    // die verlaessliche Pruefung auf "aktiver Spieler" auf beiden Seiten.
    if (bPlayersOnly && !Target->GetPlayerState()) return false;

    const FVector EyeLocation = Owner->GetPawnViewLocation();

    if (MaxTargetDistance > 0.f)
    {
        const float Distance = FVector::Dist(Target->GetActorLocation(), EyeLocation);
        if (Distance > MaxTargetDistance) return false;
    }

    // BEWUSST KEINE Sichtlinien-Pruefung an dieser Stelle.
    //
    // IsValidTarget beantwortet "ist das ueberhaupt ein waehlbares Ziel"
    // (lebt, ist ein Spieler, in Reichweite) — nicht "sehe ich es gerade".
    // Die Sichtlinie flackert bei jeder Deckung im Frame-Takt. Stuende sie
    // hier, fiele das Ziel staendig aus der Liste, der Lock wuerde verworfen
    // und neu gesetzt: die Zielanzahl springt (2/2 -> 1/1 -> 1/2) und die
    // stabile Reihenfolge waere wertlos.
    //
    // Ob tatsaechlich gezielt wird, entscheidet ApplyAim().
    return true;
}

FVector UAimbotComponent::GetAimPoint(const ACharacter* Target) const
{
    if (!Target) return FVector::ZeroVector;

    if (bAimAtHead)
    {
        if (const USkeletalMeshComponent* Mesh = Target->GetMesh())
        {
            for (const FName& BoneName : HeadBoneNames)
            {
                if (Mesh->GetBoneIndex(BoneName) != INDEX_NONE)
                {
                    return Mesh->GetBoneLocation(BoneName);
                }
            }
        }
    }

    return Target->GetActorLocation();
}

void UAimbotComponent::ApplyAim(float DeltaTime)
{
    ACharacter* Owner = Cast<ACharacter>(GetOwner());
    ACharacter* Target = LockedTarget.Get();
    if (!Owner || !Target) return;

    AController* OwnerController = Owner->GetController();
    if (!OwnerController) return;

    const FVector EyeLocation = Owner->GetPawnViewLocation();
    const FVector AimPoint    = GetAimPoint(Target);

    // Sichtlinie hier pruefen, nicht bei der Zielauswahl: verschwindet das
    // Ziel hinter Deckung, hoert der Aimbot einfach auf zu drehen und
    // uebernimmt wieder, sobald es auftaucht. Der Lock bleibt bestehen.
    if (bRequireLineOfSight)
    {
        UWorld* World = GetWorld();
        if (!World) return;

        FCollisionQueryParams Params;
        Params.AddIgnoredActor(Owner);
        Params.AddIgnoredActor(Target);

        FHitResult Hit;
        if (World->LineTraceSingleByChannel(Hit, EyeLocation, AimPoint,
                                            ECC_Visibility, Params))
        {
            return;
        }
    }

    FVector ToTarget = AimPoint - EyeLocation;
    if (!ToTarget.Normalize()) return;

    const FRotator DesiredRotation = ToTarget.Rotation();

    if (AimInterpSpeed <= 0.f)
    {
        // Hartes Einrasten
        OwnerController->SetControlRotation(DesiredRotation);
    }
    else
    {
        // Geglättete Variante: erzeugt eine sichtbare Drehbewegung statt
        // eines Sprungs und ist damit deutlich schwerer zu erkennen.
        OwnerController->SetControlRotation(FMath::RInterpTo(
            OwnerController->GetControlRotation(), DesiredRotation,
            DeltaTime, AimInterpSpeed));
    }
}

FString UAimbotComponent::GetLockedTargetName() const
{
    const ACharacter* Target = LockedTarget.Get();
    if (!Target) return TEXT("-");

    if (const APlayerState* PS = Target->GetPlayerState())
    {
        return PS->GetPlayerName();
    }
    return Target->GetName();
}

int32 UAimbotComponent::GetTargetCount() const
{
    TArray<ACharacter*> Targets;
    BuildTargetList(Targets);
    return Targets.Num();
}

int32 UAimbotComponent::GetLockedTargetIndex() const
{
    if (!LockedTarget.IsValid()) return 0;

    TArray<ACharacter*> Targets;
    BuildTargetList(Targets);

    const int32 Index = Targets.IndexOfByKey(LockedTarget.Get());
    return (Index == INDEX_NONE) ? 0 : Index + 1;
}
