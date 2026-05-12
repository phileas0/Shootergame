// Copyright (c) 2026 Patrik Milakovic — Bachelorarbeit RL-Bots
#include "AI/RL/RLAgentInterface.h"

// Hinweis: In UE 5.5+ generiert UnrealHeaderTool automatisch leere
// _Implementation-Bodies für BlueprintNativeEvent-Methoden in Interfaces.
// Daher KEINE manuellen Default-Implementierungen hier — das würde zu
// "Funktion hat bereits einen Funktionsrumpf"-Fehlern führen.
//
// Blueprints, die URLAgentInterface implementieren, überschreiben die
// einzelnen Methoden. Aufrufer (z.B. der RL-Interactor) prüfen via
// Pawn->Implements<URLAgentInterface>() vor jedem Execute_RL_xxx-Call.