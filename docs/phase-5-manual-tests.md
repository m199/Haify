# Phase 5: gemeinsame Haiku-Abnahme

Stand 2026-09-18. Implementierung und statische Checks abgeschlossen; die folgenden
Punkte sind **noch nicht ausgeführt/bestätigt**. Builds erst nach Nutzerfreigabe.
Details und Bestandsausnahmen: [Phase-5-Protokoll](phase-5-verification.md).

1. Ab Projektwurzel `sh tests/run-discover-tests.sh`: alle **36 Programme** müssen
   bestehen. Danach normaler App-Build und aktuelles Clang-Tidy mit passender
   Compilation Database. Fehler und genaue Befehle protokollieren.
2. **Track-3/Track-9-Repro:** Track 3 pausieren, Haify beenden/neustarten, Track 9
   wählen, Local Playback starten. Erst nach Übernahme spielt Track 9; alter Titel
   blitzt nicht auf. Abbrechen und erneutes lokales Starten separat prüfen.
3. Lokalen Start während Gerätesuche stoppen/neu starten. Alte Transferantworten
   geben kein neues Kommando frei. Autostart bei laufendem Remote-Client lässt
   diesen spielen; expliziter Local-Start übernimmt. Standard-/Zusatzoptionen
   prüfen, OAuth-Registrierung nur falls ohnehin erforderlich.
4. Gewählten Playlist-Titel mit Shuffle an/aus starten. Album und Folge-Queue
   einschließlich Duplikaten prüfen. Podcast-Episode und Hörbuch mit Resume-
   Position starten; Kapitel-Weiter behält die Kapitelreihenfolge.
5. Gerätewechsel, Pause/Fortsetzen und Titelwechsel im bestätigten Webplayer auf
   BARON. Bekannter Desktop-Client-Befund bleibt separat dokumentiert; HTTP 204
   allein gilt nicht als Nachweis hörbarer Wiedergabe.
6. Artist, Episode, Track, Album, Playlist, Liked Songs und Hörbuch öffnen.
   Erneutes Öffnen aktiviert vorhandene Fenster. Normale Show bei aktivierter
   Hörbuch-Unterstützung öffnen; Titel/Cover bleiben beim Probe-Fallback erhalten.
   Mehrere Shows rasch öffnen: Metadaten dürfen sich nicht vermischen.
7. Mit gespeichertem Token starten, abmelden und wieder anmelden. Eigene
   Playlists bleiben beschreibbar, fremde nicht. Falls ein zweites Konto verfügbar
   ist, Kontowechsel prüfen: keine alten Library-/Playlist-Daten oder Rechte.
8. Während Profil-/Capability-Requests abmelden und neu anmelden. Verspätete
   Antworten dürfen alte Identität oder Hörbuch-Freigabe nicht zurückbringen.
   Settings Auto/Aktiviert/Deaktiviert wechseln und Tabs/Hinweise kontrollieren.
9. Bei erreichbarem Refresh-Abbruch-Szenario: Refresh A läuft, Abmelden/neue
   Anmeldung, Refresh B startet, A antwortet zuletzt. B bleibt unabhängig und
   beendet seine eigenen wartenden Requests. Source-Gate ist statisch geprüft;
   eine echte App-/Looper-Ausführung ist noch nicht nachgewiesen.

Speicherfehler, ungültige Antworten, Cache-Invalidierung und vertauschte
Request-Antworten sind in den neuen Fixtures vorbereitet; dafür keine echten
Zugangsdaten beschädigen. Nicht erreichbare manuelle Szenarien als offen markieren.
Ergebnisse mit Haiku-Version, Commit/Arbeitsstand und Datum ergänzen; bisherige
Teilabnahmen ersetzen keinen Nachweis für diesen Gesamtstand.
