#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TelemetryLogger.generated.h"

/**
 * FPlayerSessionData
 * 
 * Dies ist unser Container für eine fertige Spieler-Session. Er enthält exakt die 25 Features, 
 * die wir in Kapitel 2.1 der Bachelorarbeit definiert haben.
 * Diese Struktur wird nach Abschluss einer Runde / nach dem Tod des Spielers vom 
 * TelemetryCollector befüllt und dann an den Logger übergeben.
 */
USTRUCT(BlueprintType)
struct FPlayerSessionData
{
    GENERATED_BODY()

    // ==========================================
    // META-DATEN (Identifikation)
    // ==========================================
    
    /** Eindeutige ID des Spielers (z.B. AccountName oder Controller-ID) */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry")
    FString PlayerID;

    /** Wie lange hat diese Session (in Sekunden) gedauert? Kurze Sessions < 1s filtern wir aus. */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry")
    float SessionDurationSeconds;

    // ==========================================
    // 1. AIM-FEATURES (Zielen & Mausbewegungen)
    // ==========================================
    
    /** Durchschnittliche Winkelgeschwindigkeit der Kameradrehung in Grad pro Sekunde. */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Aim")
    float AimAngularSpeedMean;

    /** 
     * Standardabweichung der Winkelgeschwindigkeit. 
     * Extrem wichtig: Aimbots ziehen oft maschinell konstant auf Ziele, wodurch dieser Wert sehr niedrig wird. 
     * Menschen schwanken hier stark.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Aim")
    float AimAngularSpeedStdDev;

    /** 
     * Durchschnittliche Ungenauigkeit beim Schuss (Abstand vom Fadenkreuz zur Zielmitte). 
     * Hinweis: Aktuell meist 0, da wir perfekte Hitscans ohne komplexes Bullet-Spread-Tracking nutzen.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Aim")
    float AimAngularErrorMean;

    /** Standardabweichung der Aiming-Ungenauigkeit. */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Aim")
    float AimAngularErrorStdDev;

    /** 
     * Anteil der Frames, in denen der Spieler seine Blickrichtung schlagartig um über 90 Grad geändert hat.
     * Ein hoher Wert deutet auf "Aim-Snapping" (Aimbot schaltet auf Gegner hinter sich auf) hin.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Aim")
    float AimFlipRatio;

    // ==========================================
    // 2. MOVEMENT-FEATURES (Laufen & Positionierung)
    // ==========================================
    
    /** Durchschnittliche Laufgeschwindigkeit in cm/s. */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Movement")
    float MovementSpeedMean;

    /** Die absolute Höchstgeschwindigkeit, die der Spieler in dieser Session erreicht hat. */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Movement")
    float MovementSpeedMax;

    /** Wie oft pro Sekunde hat der Spieler seine Laufrichtung abrupt geändert? (Zick-Zack-Laufen) */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Movement")
    float DirectionChangesPerSecond;

    /** 
     * Anteil der Frames, in denen der Spieler schneller lief, als das Spiel es eigentlich erlaubt.
     * Unser Haupt-Indikator für Speedhacks.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Movement")
    float SpeedViolationRatio;

    /** 
     * Shannon-Entropie des Laufweges. Misst die Unvorhersehbarkeit. 
     * Bots laufen oft stur geradeaus (Entropie nahe 0), Menschen navigieren komplexer.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Movement")
    float MovementPathEntropy;

    // ==========================================
    // 3. TIMING-FEATURES (Reaktionszeiten & Klicks)
    // ==========================================
    
    /** 
     * Durchschnittliche Reaktionszeit in Sekunden. 
     * (Zeit zwischen "Gegner wird sichtbar" und "Spieler drückt ab").
     */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Timing")
    float ReactionTimeMean;

    /** 
     * Varianz der Reaktionszeit. 
     * Triggerbots schießen unmenschlich konstant, Menschen variieren immer. Ein Kern-Feature für das ML-Modell!
     */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Timing")
    float ReactionTimeStdDev;

    /** Durchschnittliche Zeitspanne zwischen zwei aufeinanderfolgenden Schüssen. */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Timing")
    float ShotIntervalMean;

    /** 
     * Varianz der Schussabstände. 
     * Genau wie bei der Reaktionszeit verrät eine Varianz nahe 0 einen Triggerbot/Makro, der Klicks perfekt simuliert.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Timing")
    float ShotIntervalStdDev;

    /** Feuerrate (Schüsse pro Sekunde). */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Timing")
    float ShotsPerSecond;

    // ==========================================
    // 4. RATE-FEATURES (Statistiken & Quoten)
    // ==========================================
    
    /** Trefferquote (Treffer geteilt durch abgefeuerte Schüsse). */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    float HitRate;

    /** Kopfschuss-Quote (Kopfschüsse geteilt durch alle Treffer). */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    float HeadshotRate;

    /** Kills pro Minute (KPM). */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    float KillsPerMinute;

    /** Kill/Death Ratio (K/D). */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    float KillDeathRatio;

    /** Gesamtzahl abgefeuerter Schüsse. */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    int32 TotalShots;

    /** Gesamtzahl der Treffer auf Feinde. */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    int32 TotalHits;

    /** Gesamtzahl der Kills in dieser Session. */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    int32 TotalKills;

    /** Gesamtzahl der Tode. */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry|Rate")
    int32 TotalDeaths;

    /** 
     * Ground-Truth-Label für unsere Trainingsdaten.
     * 0 = Legitim (Normaler Spieler), 1 = Cheater.
     */
    UPROPERTY(BlueprintReadWrite, Category = "Telemetry")
    int32 Label;

    // Standard-Konstruktor initialisiert alles brav mit 0, damit wir keine Garbage-Werte im ML-Modell haben.
    FPlayerSessionData()
        : SessionDurationSeconds(0.f)
        , AimAngularSpeedMean(0.f), AimAngularSpeedStdDev(0.f)
        , AimAngularErrorMean(0.f), AimAngularErrorStdDev(0.f)
        , AimFlipRatio(0.f)
        , MovementSpeedMean(0.f), MovementSpeedMax(0.f)
        , DirectionChangesPerSecond(0.f), SpeedViolationRatio(0.f)
        , MovementPathEntropy(0.f)
        , ReactionTimeMean(0.f), ReactionTimeStdDev(0.f)
        , ShotIntervalMean(0.f), ShotIntervalStdDev(0.f)
        , ShotsPerSecond(0.f)
        , HitRate(0.f), HeadshotRate(0.f)
        , KillsPerMinute(0.f), KillDeathRatio(0.f)
        , TotalShots(0), TotalHits(0), TotalKills(0), TotalDeaths(0)
        , Label(0)
    {}
};

/**
 * UTelemetryLogger
 *
 * Das ist unser Singleton-ähnliches Objekt, das die fertigen Sessions (`FPlayerSessionData`) 
 * entgegennimmt, kurz im Arbeitsspeicher (Puffer) sammelt und dann blockweise 
 * als CSV-Datei in das `Saved/`-Verzeichnis der Engine exportiert.
 *
 * Blueprint Nutzung:
 *   - `LogSessionData()` aufrufen, wenn ein Spieler stirbt, disconnectet oder die Runde endet.
 *   - `FlushToCSV()` aufrufen (z.B. am Rundenende), um den Puffer endgültig auf die Festplatte zu schreiben.
 */
UCLASS(Blueprintable, BlueprintType)
class SHOOTERGAME_API UTelemetryLogger : public UObject
{
    GENERATED_BODY()

public:
    UTelemetryLogger();

    /**
     * Schiebt eine fertige Spieler-Session in unseren internen Puffer.
     * Dieser Aufruf ist extrem schnell und blockiert den Game-Thread nicht.
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void LogSessionData(const FPlayerSessionData& SessionData);

    /**
     * Schreibt alle im Puffer gesammelten Sessions auf einmal in die CSV-Datei.
     * Sollte am Ende einer Runde aufgerufen werden, da Festplattenzugriffe kurz Ruckler verursachen können.
     * 
     * @param Filename - Der Name der Datei (ohne .csv Endung). Standard ist "telemetry_log".
     */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void FlushToCSV(const FString& Filename = TEXT("telemetry_log"));

    /** Leert den internen Arbeitsspeicher-Puffer. Passiert nach FlushToCSV automatisch. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    void ClearBuffer();

    /** Gibt zurück, wie viele Sessions gerade im Puffer liegen und noch nicht gespeichert wurden. */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Telemetry")
    int32 GetBufferedSessionCount() const;

    /** 
     * Hilfsfunktion: Gibt den exakten Pfad zum Ordner zurück, in dem die CSV landet. 
     * Standardmäßig ist das "DeinProjekt/Saved/Telemetry/". 
     */
    UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Telemetry")
    static FString GetSaveDirectory();

private:
    /** Unser RAM-Puffer. Sammelt die Daten, bis wir FlushToCSV aufrufen. */
    TArray<FPlayerSessionData> SessionBuffer;

    /** Liefert die exakte Kopfzeile für die CSV. Spaltennamen müssen 1:1 zum Python-ML-Skript passen! */
    static FString GetCSVHeader();

    /** Konvertiert ein Session-Struct in eine kommaseparierte Textzeile für die Datei. */
    static FString SessionToCSVRow(const FPlayerSessionData& Data);
};
