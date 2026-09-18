# Remote-Titelstart: Diagnose und Abnahme

Stand: 2026-09-15. Fehler im Transport-Abschluss korrigiert;
Remote-Titelstart im Windows-Webplayer vom Nutzer bestätigt; die Windows-
Desktop-App scheitert weiterhin trotz echtem HTTP 204.

## Gemeldeter Fall

Der Nutzer startet in Haify einen Titel auf einem Windows-Zielgerät. Dort beginnt
keine Wiedergabe; Haify zeigt kurz danach wieder „Nothing is playing“.
Die bisherigen Debug-Zeilen melden sowohl für `PUT /me/player` als auch für
`PUT /me/player/play?device_id=…` HTTP 204. Lokal mit Librespot funktioniert
die Wiedergabe laut Nutzer. Der Fehler tritt laut anschließendem Gegencheck
auch bei bereits laufender Remote-Wiedergabe ohne Gerätewechsel auf.

Diese Antworten zeigen keinen HTTP-Fehler. Sie enthalten aber keinen
nachfolgenden Wiedergabestatus. Ob Transfer und Play dasselbe Gerät adressierten,
welche URI bzw. welcher Kontext gesendet wurde und welcher Zustand danach
zurückkam, lässt sich aus den bisherigen Zeilen nicht ablesen.

Primärquellen, geprüft am 2026-09-14:

- [Start/Resume Playback](https://developer.spotify.com/documentation/web-api/reference/start-a-users-playback):
  Zielgerät über `device_id`; Spotify garantiert die Ausführungsreihenfolge mit
  anderen Player-Endpunkten nicht. Das allein beweist hier kein Reihenfolgeproblem.
- [Get Playback State](https://developer.spotify.com/documentation/web-api/reference/get-information-about-the-users-current-playback):
  enthält aktives Gerät, `is_playing` und aktuellen Titel; HTTP 200 und 204 sind
  dokumentiert, `item` und `context` können null sein.

## Ergänzter Trace und Transportkorrektur

Im neuen Nutzermitschnitt ist `BARON` in Trace 13 mit derselben ID wie im
Play-Aufruf aufgeführt: `is_active=false`, `is_restricted=false`.
Trace 16 sendet 49 Track-URIs und meldet nach etwa 1 ms **status=0**, nicht 204.
Die anschließenden GETs liefern weiterhin 204 ohne Body. Dieser Mitschnitt
enthält keinen `PUT /me/player` für einen Transfer. Er belegt daher einen anderen
Fehlerverlauf als die vorherigen zwei 204-Zeilen.

`network/HttpClient.cpp` ignorierte bisher das Ergebnis von `Run()`,
`wait_for_thread()` und `BUrlRequest::Status()`. Ein unterbrochener Join wurde
wie ein abgeschlossener Request behandelt; anschließend konnte der noch laufende
Request beim Löschen gestoppt werden. Native Netzwerkfehler erschienen nur als
HTTP-Status 0. Welcher dieser Fälle im Mitschnitt eintrat, ist daraus noch nicht
ablesbar.

Korrektur: Ein `B_INTERRUPTED` wiederholt ausschließlich das Warten auf denselben
Worker. Request und Output-Puffer bleiben bis zum Abschluss erhalten. Fehler
beim Start, beim Warten und im nativen Request liefern Status -1 mit Operation,
Haiku-Fehlertext und numerischem `native_status`. Auch ein nominal erfolgreicher
Request ohne gültigen HTTP-Status liefert -1. Die Debug-Trace enthält diesen Text
als `transport_error`. Der HTTP-Befehl wird nicht automatisch erneut gesendet.
Das Kopieren des Request-Bodys wird auf Schreib-/Seek-Fehler geprüft.

Echte HTTP-Antworten behalten Status, Body und Retry-After. Eine notwendige
Kompatibilitätsregel ist explizit: Haikus `B_RESOURCE_NOT_FOUND` zusammen mit
HTTP 404 bleibt eine HTTP-404-Antwort. Andere native Fehler, auch nach bereits
empfangenen HTTP-200-Headern, bleiben Transportfehler. So werden bestehende
404-Fallbacks erhalten, ohne einen unvollständigen Transfer als Erfolg zu melden.

Die kleinen Abschlussregeln stehen in `network/HttpRequestCompletion.h`.
Der HTTP-Worker besitzt Request und Puffer; sein Aufrufer erhält genau einen
Callback mit kopierter Antwort nach Freigabe des Requests. UI und Playback-Policy
wurden für diese Korrektur nicht erweitert.

Haiku-Primärquellen, geprüft am 2026-09-14 am Commit
`c1aa9a58f5ba87068563a04c67ca507fc6de4d85` und gegen die lokalen Header:

- [UrlRequest.cpp](https://github.com/haiku/haiku/blob/c1aa9a58f5ba87068563a04c67ca507fc6de4d85/src/kits/network/libnetservices/UrlRequest.cpp):
  `Run()` kann fehlschlagen; `_ThreadEntry` gibt B_OK zurück, während `Status()`
  das Ergebnis des Protokolls enthält.
- [thread.cpp](https://github.com/haiku/haiku/blob/c1aa9a58f5ba87068563a04c67ca507fc6de4d85/src/system/kernel/thread.cpp):
  Das Warten im Userspace kann unterbrochen werden.
- [HttpRequest.cpp](https://github.com/haiku/haiku/blob/c1aa9a58f5ba87068563a04c67ca507fc6de4d85/src/kits/network/libnetservices/HttpRequest.cpp):
  HTTP 404 wird zusätzlich mit B_RESOURCE_NOT_FOUND gemeldet.

## Gegencheck 2026-09-15: Start scheitert auch auf aktivem Ziel

Der zweite Nutzermitschnitt nach der Transportkorrektur zeigt in Trace 9 einen
Play-Request mit acht Track-URIs an BARON. Nach 119 ms folgt HTTP 204 ohne
nativen Fehler. Alle anschließenden Statusabfragen bleiben etwa neun Sekunden
lang bei HTTP 204 ohne Body. Der Mitschnitt enthält keinen Transfer-Aufruf.

Zusätzliche Nutzerbeobachtungen, ohne neuen Mitschnitt dieses Gegenchecks:

- Direkt auf BARON gestartete Musik erscheint in Haify korrekt, mit zeitlichem
  Versatz. Die Anzeige und das Lesen des Remote-Status funktionieren somit
  grundsätzlich; der Versatz passt zum bestehenden Polling.
- Auch aus diesem laufenden Zustand startet Haify den neu gewählten Titel nicht.
  Ein lediglich inaktives Ziel erklärt den Fehler daher nicht ausreichend.

Die gesendete Form `{"uris":["spotify:track:…",…]}` mit `device_id` entspricht
der erneut am 2026-09-15 geprüften Start/Resume-Dokumentation. Es gibt bisher
keinen Beleg, dass ein zusätzlicher Transfer oder automatisches Wiederholen
dieses Problem behebt.

Ein [Erstbericht im Spotify-Entwicklerforum](https://community.spotify.com/t5/Spotify-for-Developers/Web-API-me-player-play-returns-204-but-does-not-start-playback/td-p/7552922),
abgerufen am 2026-09-15, beschreibt denselben Ablauf auf Windows: HTTP 204,
kein Titelstart, teils Ende der bisherigen Wiedergabe. Der Autor reproduziert
ihn auch in Spotifys API-Konsole. Gegen denselben Account funktioniert laut
Bericht der Webplayer als Ziel; ein weiterer Nutzer bestätigt den Unterschied.
Dies sind direkte Nutzerberichte, keine bestätigte Spotify-Ursachenanalyse.

Ergebnis des Vergleichs, vom Nutzer am 2026-09-15 bestätigt: Der Titelstart aus
Haify im Spotify-Webplayer auf demselben Windows-Rechner BARON funktioniert
problemlos. Damit ist der Unterschied zwischen den Zielclients auch im eigenen
Repro bestätigt. Zusammen mit dem Erstbericht spricht dies stark für ein Problem
im Zusammenspiel von Spotify-Web-API und Windows-Desktop-App. Die genaue interne
Ursache bleibt unbestätigt; der Desktop-Fehler wird nicht als behoben geführt.

Der Webplayer ist ein bestätigter Ausweichweg für Remote-Wiedergabe. Dieser
Desktop-spezifische Befund wird als voraussichtlich externe Einschränkung separat
weitergeführt und verlangt vor Phase 4 keinen spekulativen Haify-Umbau. Die
übrige Phase-2/3-Abnahme einschließlich der neuen Transport-Fixture bleibt offen.
Es wurden weder Geräteaktivierung noch Playback-Retries geändert. Am 2026-09-15
wurden nur Diagnoseunterlagen aktualisiert; kein Build oder ausführbarer Test
wurde gestartet und kein zusätzlicher App-Code geändert.

## Ergänzte Debug-Ausgabe

`SpotifyRequestClient.cpp` protokolliert mit `--debug` tatsächlichen Dispatch
und Antwort für Playback-State, Currently-Playing, Devices, Transfer, Play und
Shuffle. `trace` verbindet SEND/RECV; `t` ist die monotone Zeit in Millisekunden.
Die Zeitpunkte zeigen die clientseitige Reihenfolge, nicht Spotifys interne
Ausführung. Ein Auth-Retry erhält eine eigene Trace-ID.

SEND enthält Methode, Pfad, Body-Länge und bis zu 4096 Zeichen Request-Body.
RECV enthält HTTP-Status und eine kompakte JSON-Auswahl: Gerät samt Aktiv- und
Restricted-Flags, Item-/Kontext-URI, Fortschritt, Playing-/Shuffle-Zustand und
Fehlerdaten. Leerer Body, ungültiges JSON, fehlende Felder und explizites null
bleiben unterscheidbar; bei Status <= 0 steht der Fehlertext in `transport_error`.
Header und Zugriffstoken werden nicht hinzugefügt.
Es werden keine zusätzlichen Netzwerkanfragen oder Playback-Retries ausgelöst.

## Reproduzierbarer manueller Check

Nach einem freigegebenen Build den aktualisierten Stand so starten:

```sh
./objects.x86_64-cc13-release/Haify --debug
```

1. Windows-Ziel auswählen und genau einen Titel starten. Logs vom Gerätewechsel
   bis mindestens zur ersten Statusantwort nach „Nothing is playing“ erfassen.
2. Denselben Titel bei bereits laufender Wiedergabe auf dem Windows-Gerät starten,
   ohne zuvor das Gerät zu wechseln. Erfolg/Fehler und Trace festhalten.
3. Als Vergleich denselben Titel lokal über Librespot starten.
4. Auf dem Windows-Rechner den Spotify-Webplayer als Ziel verwenden. Dieser
   Remote-Titelstart ist am 2026-09-15 vom Nutzer als erfolgreich bestätigt.

Prüfen: Transfer-Ziel und Play-Ziel, gesendete URI/Offset/Kontext, zeitliche
Überlappung sowie `item`, `device` und `is_playing` in den nachfolgenden GETs.
Ein HTTP 204 allein schließt diesen Abnahmepunkt nicht ab.

Der Runner `sh tests/run-discover-tests.sh` enthält zusätzlich
`HttpRequestCompletionTest.cpp`: mehrfach unterbrochenes Warten bis zum fertigen
HTTP-204-Ergebnis, terminale Wartefehler, echte HTTP-Fehler einschließlich der
Haiku-404-Sonderregel, Status 0 und native Fehler trotz empfangener HTTP-Header.
Dieser Test simuliert den Wait-Rückgabewert und benötigt weder Netzwerk noch UI.

Lokale Prüfung: Cppcheck mit den Projektoptionen meldet nur die bekannte
`Instantiate`-Ausnahme; `lizard -w .` und `git diff --check` sind ohne Befund.
Kein Build und kein ausführbarer Test wurden gestartet. Die neue Fixture und
der manuelle Remote-Repro müssen auf Haiku noch bestätigt werden. Zusätzlich
lokale Librespot-Wiedergabe und das Laden von Artwork/Bibliothek prüfen, weil
dieselben HTTP-Worker auch diese Requests ausführen.
