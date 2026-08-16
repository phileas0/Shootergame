"""
Lesen und Schreiben der von Hand gepflegten CSV-Dateien (labels.csv,
players.csv).

Hintergrund: Excel richtet sich beim Doppelklick auf eine .csv nach dem
Listentrennzeichen der Windows-Regionseinstellung. Auf einem deutschen System
ist das ein Semikolon — eine komma-getrennte Datei landet dort komplett in
Spalte A und ist nicht bearbeitbar.

Deshalb werden die Handdateien mit Semikolon geschrieben. Beim Lesen wird das
Trennzeichen erkannt, damit aeltere komma-getrennte Dateien weiter
funktionieren und ein in Excel veraendertes Format nicht zum Bruch fuehrt.

Die vom Spiel erzeugten session_*.csv sind davon nicht betroffen: die schreibt
der TelemetryLogger und die werden nicht von Hand bearbeitet.
"""

import os
import sys
import pandas as pd

# Trennzeichen fuer neu geschriebene Handdateien
CSV_SEP = ";"

# Excel erkennt UTF-8 nur mit BOM zuverlaessig — sonst werden Umlaute in
# Personennamen zerschossen.
CSV_ENCODING = "utf-8-sig"


def sniff_sep(path):
    """
    Trennzeichen aus der Kopfzeile bestimmen.

    Bewusst kein csv.Sniffer: der raet bei einer einzelnen Spalte oder bei
    Semikolon in einem Feld schnell falsch. Hier genuegt der Vergleich, welches
    Zeichen in der Kopfzeile haeufiger vorkommt — die Kopfzeile ist von uns
    erzeugt und enthaelt weder Kommas noch Semikolons innerhalb der Namen.
    """
    try:
        with open(path, "r", encoding=CSV_ENCODING) as f:
            header = f.readline()
    except UnicodeDecodeError:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            header = f.readline()

    return ";" if header.count(";") > header.count(",") else ","


def read_hand_csv(path, **kwargs):
    """CSV mit erkanntem Trennzeichen einlesen."""
    return pd.read_csv(path, sep=sniff_sep(path), encoding=CSV_ENCODING, **kwargs)


def write_hand_csv(df, path):
    """
    Schreiben mit klarer Meldung statt Traceback.

    Excel sperrt eine geoeffnete Datei zum Schreiben. Ohne diesen Zweig endet
    jeder Lauf mit offener Tabelle in einem PermissionError-Traceback.
    """
    try:
        df.to_csv(path, index=False, sep=CSV_SEP, encoding=CSV_ENCODING)
    except PermissionError:
        print(f"\n[FEHLER] {path} laesst sich nicht schreiben.")
        print(f"         Die Datei ist vermutlich noch in Excel geoeffnet.")
        print(f"         Schliessen und das Skript erneut starten — "
              f"bereits eingetragene Labels bleiben erhalten.")
        sys.exit(1)
