// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "RLAgentInterface.generated.h"

/**
 * URLAgentInterface
 *
 * Blueprint-Interface, das BP_RLShooterNPC implementieren wird, um vom
 * C++-Interactor adressierbar zu sein. Der Interactor weiß nichts vom
 * konkreten Pawn-Blueprint und ruft alle Aktions- und Lese-Operationen
 * über dieses Interface auf.
 *
 * Aktionen entsprechen den 8 Action-Slots aus
 * Docs/MDP_EffizienterBot.md §5; die Lese-Funktionen liefern die
 * Self-Beobachtungen aus §4.1.
 *
 * Alle Methoden sind als BlueprintNativeEvent deklariert und haben
 * leere Default-Implementierungen — Blueprints müssen sie überschreiben.
 */
UINTERFACE(BlueprintType, Blueprintable, MinimalAPI)
class URLAgentInterface : public UInterface
{
    GENERATED_BODY()
};

class SHOOTERGAME_API IRLAgentInterface
{
    GENERATED_BODY()

public:
    // ── Aktionen (vom Interactor pro Tick aufgerufen) ────────────────────

    UFUNCTION(BlueprintNativeEvent, Category="RL|Action")
    void RL_ApplyMovementInput(float Forward, float Right);

    UFUNCTION(BlueprintNativeEvent, Category="RL|Action")
    void RL_ApplyLookInput(float YawDelta, float PitchDelta);

    UFUNCTION(BlueprintNativeEvent, Category="RL|Action")
    void RL_Jump();

    UFUNCTION(BlueprintNativeEvent, Category="RL|Action")
    void RL_SetCrouch(bool bWantsCrouch);

    UFUNCTION(BlueprintNativeEvent, Category="RL|Action")
    void RL_StartFire();

    UFUNCTION(BlueprintNativeEvent, Category="RL|Action")
    void RL_StopFire();

    UFUNCTION(BlueprintNativeEvent, Category="RL|Action")
    void RL_Reload();

    // ── Episodensteuerung ────────────────────────────────────────────────

    /** Health, Ammo, Recoil, Reload-State zurücksetzen. Vom EpisodeManager
     *  nach dem Teleport an einen neuen Spawnpoint zu rufen. */
    UFUNCTION(BlueprintNativeEvent, Category="RL|Episode")
    void RL_ResetForEpisode();

    // ── Lese-Funktionen für Beobachtungen ────────────────────────────────

    UFUNCTION(BlueprintNativeEvent, Category="RL|Query")
    float RL_GetHealth() const;

    UFUNCTION(BlueprintNativeEvent, Category="RL|Query")
    int32 RL_GetAmmoLoaded() const;

    UFUNCTION(BlueprintNativeEvent, Category="RL|Query")
    int32 RL_GetAmmoReserve() const;

    UFUNCTION(BlueprintNativeEvent, Category="RL|Query")
    bool RL_IsReloading() const;

    /** [0..1]: 1 = Schuss gerade erst abgegeben, 0 = bereit. */
    UFUNCTION(BlueprintNativeEvent, Category="RL|Query")
    float RL_GetWeaponCooldown() const;

    /** [0..1]: aktueller Streukegel-Aufbau (Recoil-Offset). */
    UFUNCTION(BlueprintNativeEvent, Category="RL|Query")
    float RL_GetRecoilOffset() const;
};
