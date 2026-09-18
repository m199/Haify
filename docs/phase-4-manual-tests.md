# Phase 4: manuelle Abnahme auf Haiku

Stand: 2026-09-16. Die geplanten Extraktionen sind implementiert. Dieser Stand
ist noch **nicht** durch einen Haiku-Testlauf/App-Build bestätigt.

## Vorbereitung

Im Projektverzeichnis:

```sh
sh tests/run-discover-tests.sh
make
```

Erwartet: **26 Testprogramme bestanden**, anschließend erfolgreicher App-Build.
Der Runner funktioniert auch aus `tests/` mit `sh run-discover-tests.sh`.
Die automatisierten Tests verwenden keine echten Spotify-Schreibzugriffe.

Für die folgenden Änderungen eine eigene **Testplaylist** verwenden, mit etwa
zehn Titeln einschließlich eines doppelten Titels. Für Nachladen zusätzlich eine
große eigene Playlist verwenden, deren Ende noch nicht geladen ist.

## Manuelle Prüfliste

| Nr. | Aktion | Erwartung |
| --- | --- | --- |
| 1 | Einzelnen Titel nach oben/unten und ans Ende ziehen; anschließend Mehrfachauswahl mit Alt, z. B. 1/3/6/9, gemeinsam ans Ende ziehen. | Reihenfolge innerhalb der Auswahl bleibt erhalten. Nicht ausgewählte Titel behalten ihre Reihenfolge. Doppelte Titel werden als einzelne Vorkommen behandelt. Nach erneutem Öffnen stimmt die Reihenfolge weiterhin. |
| 2 | Mehrfachauswahl an einem markierten Titel ohne Alt greifen; einmal ziehen, einmal nur klicken; danach Alt/Shift und Doppelklick testen. | Kein kurzes Verschwinden der Markierung beim Drag. Reiner Klick wählt anschließend einen einzelnen Titel; Doppelklick startet ihn. Auswahl mit Zusatztasten bleibt nutzbar. |
| 3 | Einen Track und eine Episode von außerhalb in eine vollständig geladene Testplaylist ziehen. Danach einen Track in eine teilweise geladene große Playlist ziehen und bis ans tatsächliche Ende laden. | Vollständig geladene Liste zeigt den neuen Eintrag am Ende. In der teilweise geladenen Liste erscheint er erst beim Nachladen seines tatsächlichen Platzes. Keine doppelten oder übersprungenen Zeilen; Zähler und erneut geöffnetes Fenster stimmen. |
| 4 | Aus der Testplaylist einzelne/mehrere Titel entfernen, darunter genau eines der doppelten Vorkommen. Danach „Clear Playlist“ einmal abbrechen und einmal bestätigen. | Entfernen betrifft die gewählten Vorkommen. Abbrechen verändert nichts. Bestätigen leert die gesamte Playlist, auch noch nicht geladene Einträge. Erneutes Öffnen zeigt denselben Stand. |
| 5 | Eine Mutation auslösen und unmittelbar neu laden, weiterziehen oder das Fenster schließen. Mehrfach-Reorder zusätzlich während laufender Schritte schließen. | Keine konkurrierende Änderung oder falsche Cache-Wiederherstellung. Gruppen-Reorder beendet seine Folge vor dem Schließen des Fensters. Andere geschlossene Fenster erzeugen keine verspäteten Zeilen oder Abstürze. |
| 6 | Cover einer eigenen Testplaylist auf eine kleine JPEG-Datei ändern; anschließend PNG und zu große JPEG-Datei wählen. | Gültiges Cover wird geladen. PNG und JPEG mit mehr als 192 KiB Rohdaten werden abgewiesen; das bisherige Cover bleibt erhalten. Ein Dateizugriffsfehler wird angezeigt. Schlägt nur das Laden der Vorschau fehl, nennt die Meldung den erfolgreichen Upload. |
| 7 | Menüleiste und Header-Kontextmenü bei eigener, fremder und leerer Playlist vergleichen. Album, Liked Songs und Podcast öffnen. | Gleiche Freigaben in beiden Playlist-Menüs. Fremde Playlist bietet keine Bearbeitung/Leeren/Cover-Änderung, aber Unfollow. Leere Playlist lässt sich nicht erneut leeren. Die übrigen Inhaltstypen behalten ihre passenden Bedienelemente. |
| 8 | Große Liste nachladen und erneut öffnen. Im Podcast schnell Suchtext wechseln/löschen, aktualisieren, Links und E-Mail-Adressen anklicken. Fenster verkleinern und mit größerer Schrift prüfen. | Keine veralteten Suchseiten/Zeilen. Titel, Cover, Zähler und Layout bleiben brauchbar. Hand-Cursor über Links; E-Mail-Ziel enthält ein echtes `@` und geht an das Mailprogramm. |
| 9 | Wenn möglich: nach Laden der Testplaylist Haiku kurz offline nehmen und Add/Remove/Reorder/Clear versuchen; danach Verbindung wiederherstellen und neu laden. | Nach dem Fehler endet der Pending-Zustand; Zeilen/Auswahl werden wiederhergestellt. Ein teilweise ausgeführter Gruppen-Reorder meldet den Teilfehler und lädt den Serverstand. Kein bestätigter Erfolg wird vorgetäuscht. |
| 10 | Falls vorhanden: Playlist mit nicht verfügbaren/ausgeblendeten Einträgen benutzen und sichtbare Einzel-/Mehrfachauswahl verschieben. | Richtige Spotify-Vorkommen verschieben sich; Nummern dürfen Lücken haben. Ergebnis nach erneutem Öffnen gegenprüfen. Fehlt eine passende Playlist, dies vermerken; der neue Positionstest deckt solche Lücken automatisiert ab. |
| 11 | Haify ohne laufende Wiedergabe und ohne gewähltes Gerät starten. Ersten Titel wählen und im Gerätedialog „Local Playback“ anklicken. Danach während laufender Musik einen anderen Titel wählen; zusätzlich pausieren und einen neuen Titel starten. | Während librespot startet, wird der gewählte Titel nicht vorzeitig als spielend angezeigt. Titel/Fortschritt folgen der Wiedergabemeldung. Wechsel bei laufender Musik bleiben unmittelbar; beim Start aus Pause wird ebenfalls auf die Wiedergabemeldung gewartet. |

Für die Rückmeldung genügen Test-/Build-Ergebnis und auffällige Nummern dieser
Liste. Nicht durchführbare Szenarien bitte als solche nennen. Den Windows-Webplayer
für einen kurzen Remote-Playback-Test verwenden; der bekannte Windows-Desktopplayer-
Befund wird separat verfolgt.

Native Mauszustände, tatsächliche Fensterdarstellung, Dateiauswahl und Spotify-
Synchronisierung werden manuell geprüft: Die isolierten Controller-Tests besitzen
absichtlich weder echte Fenster noch ein angemeldetes Spotify-Konto.

Technische Nachweise und reproduzierbare Analysebefehle:
[Phase-4-Protokoll](phase-4-verification.md).
