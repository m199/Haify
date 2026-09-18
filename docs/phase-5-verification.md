# Phase 5: Playback und App entkoppeln

Stand: 2026-09-18. Auf ausdrücklichen Nutzerwunsch begonnen, nach dessen Abnahme
von Phase 4. Baseline: `1bbd00daa0245a671e1c622512bf3dd2d63761fc`.
**Alle geplanten Phase-5-Implementierungspakete einschließlich Account-/Capability-
Policy sind umgesetzt. Die technische und manuelle Haiku-Abnahme steht aus.**
Der Runner enthält **36 vorbereitete Programme**. Es wurde kein Build oder
C++-Testlauf gestartet. Aktuelle gemeinsame [Abnahmeliste](phase-5-manual-tests.md).
Die folgenden Teilstände dokumentieren die Entwicklung; der Abschlussstand
und die verbleibenden Prüfgates stehen im Abschnitt 5d.

## 5a: Playback-Start

- `playback/PlaybackCommand.h`: frameworkfreies Kommando einschließlich
  Geräte-ID, Position, Folge-URIs und Hörbuch-Provenienz; gemeinsame Validierung.
- `playback/PlaybackStartPolicy.h`: reine Regeln für Hörbuch-Queue, Shuffle und
  maximal 100 Start-URIs, einschließlich gewähltem Titel und Duplikaten.
- `playback/PlaybackStartController`: wählt Kontext/Einzeltitel/Batch und führt
  die bisherige Shuffle-Folge aus. Nutzt direkt die injizierbare `PlaybackApi`;
  kein zusätzlicher Transport und keine Fenster-/Prozessabhängigkeit.
- `PlayerWindow` liest das Kommando, hält Anzeige und Kapitel-Folge, delegiert
  den Start und plant den bestehenden Verifikationspoll. Auch Kapitel-Weiter
  delegiert. Geräteauswahl und Librespot-Lebenszyklus bleiben im nächsten Paket.

Die Anwendung besitzt die API und muss sie bis zum Ende ihrer Callbacks am
Leben halten. Asynchrone Starts besitzen Kopien von Kommando und Ergebnissen;
sie speichern keine Fenster-/Message-Zeiger. Das Ergebnis erreicht genau einmal
den optionalen Abschluss-Callback, nachdem Play und eine erforderliche Shuffle-
Wiederherstellung geantwortet haben (unter dem bestehenden einmaligen API-
Callback-Vertrag). Ein ungültiges Kommando liefert `false` ohne Request oder
Callback. `PlayerWindow` nutzt das Ergebnis ausschließlich für Debug-Ausgabe.

Die neue Grenze führt weder Cache noch Persistenz ein. Die bestehenden Live-
Reads und ihre Cache-Invalidierung bleiben in `PlaybackApi`; der bisherige
Verifikationspoll läuft weiterhin nach dem Dispatch. Kaltstart-/Pausen-Anzeige
und Metadaten-Vorschau behalten ihre bestätigten Freigaberegeln aus Phase 4.
Ein angenommenes Start-Kommando erzeugt keinen künstlichen Playing-State.

## Verhalten und Kompatibilität

| Ausgangslage | Folge |
| --- | --- |
| Kontext statt spielbarem Item | Kontext-Play mit gewählter Geräte-ID |
| Item ohne Folge-Queue | Item-Play mit bisherigem Kontext-/Positionsverhalten |
| Item mit normaler Folge-Queue | Gewähltes Item + höchstens 99 Folge-URIs; Reihenfolge/Duplikate bleiben, Kontext und Startposition entfallen wie bisher |
| `parent_kind=audiobook` oder `primary_open_uri` mit Hörbuch-Präfix | Einzelkapitel mit Startposition; Folge-Kapitel bleiben in der UI verfügbar. Kontext-URI allein genügt nicht als Provenienz |
| Shuffle an, Track in Playlist-Kontext, kein Hörbuch | Shuffle aus → Play → Shuffle an; dieselbe Geräte-ID in jedem Schritt |
| Play nach erfolgreichem Shuffle-Aus fehlgeschlagen | Shuffle trotzdem wiederherstellen; Play-Fehler bleibt sichtbar im Ergebnis |
| Shuffle-Aus fehlgeschlagen | Bisheriger Einzeltitel-Fallback; keine Wiederherstellung |

**Dokumentierte Kompatibilitätsausnahme:** Der alte Shuffle-Aus-Fehlerpfad versucht
bei jeder Fehlerklasse einen einzelnen Titel ohne Kontext, Folge-Queue oder
Startposition. Diese Extraktion ändert diese bestehende Semantik nicht.
`usedSingleItemFallback` kennzeichnet den eingeschränkten Ersatz ausdrücklich;
es gibt kein zusammenfassendes Erfolgssignal, das ihn dem gewünschten Start
gleichsetzt. Die drei Schritte tragen getrennt `attempted`, `accepted`,
`status` und `retryAfter`. So bleiben Transportstatus 0, HTTP 401/403/404/429/5xx
und der Play-/Restore-Ausgang unterscheidbar. Fehlende, falsch typisierte oder
nicht darstellbare Status-/Retry-Werte sind -1 (unbekannt), niemals Erfolg.
`accepted` stammt allein vom API-Callback. Eine künftig engere Fallback-Policy
braucht einen eigenen Verhaltensentscheid und Runtime-Nachweis.

Primärdokumentation geprüft am 2026-09-17:
[Start/Resume Playback](https://developer.spotify.com/documentation/web-api/reference/start-a-users-playback)
und [Toggle Shuffle](https://developer.spotify.com/documentation/web-api/reference/toggle-shuffle-for-users-playback).
Die Dokumentation beschreibt optionale Geräte-IDs (ohne ID: aktives Gerät),
Kontext-Offsets für Album/Playlist und 204 als erfolgreiche Request-Antwort.
Sie garantiert keine Ausführungsreihenfolge über mehrere Player-Endpunkte.
Haify wartet daher wie bisher die Antworten seiner Shuffle-Folge ab; daraus
folgt keine bestätigte Geräteausführung. Die bestehenden Episode-/Hörbuch-
Pfade und die Batch-Grenze werden als Haify-Kompatibilität übernommen, nicht
als neu durch diese Referenzen zugesicherte API-Eigenschaften.

Mehrere schnelle Starts auf bereits verfügbaren Geräten können ihre Request-
Folgen weiterhin verschachteln. Allgemeine Playback-Epochen oder zusätzliche
Play-Retries wurden nicht eingeführt; lokale Startup-Generationen sind im
nachstehenden Bugfix dokumentiert.
Der bekannte Unterschied zwischen Windows-Desktop-Client und funktionierendem
Webplayer auf BARON wird durch diese Extraktion nicht als behoben gewertet.

## Automatisierte Abdeckung und statische Nachweise

Der Runner enthielt nach 5a **28 Programme** (einschließlich des nachstehenden
lokalen Startup-Bugfixes). Neu in 5a: `PlaybackStartControllerTest`,
mit tatsächlichen Request-Traces gegen die injizierte `PlaybackApi`, ohne
Fenster, Netzwerk, Prozess oder Live-Schreibzugriff:

- Item-/Kontext-Start, Position und kodierte/fehlende Geräte-ID;
- Batch-Grenzen 99/100/101 Gesamt-Items und darüber, Reihenfolge/Duplikate;
- beide Hörbuch-Provenienzen, Kontext allein und Shuffle-Ausnahme;
- verzögerte Shuffle-/Play-Reihenfolge, eigene Command-Kopien, unabhängige Starts;
- Play-Fehler, Wiederherstellungsfehler und bisheriger Shuffle-Fehler-Fallback
  über unterschiedliche Fehlerklassen mit getrennten Ergebnissen;
- ungültige Kommandos ohne Nebenwirkungen, auch ungültige Queue-Einträge hinter
  der Batch-Grenze; fehlerhafte Antwortfelder und synchrone Callbacks.

`MessageContractsTest` ergänzt Provenienz-Roundtrip, Geräteersetzung, Defaults,
falsche Typen und wiederholte Felder mit unveränderter Ausgabe bei Fehlern.
Die bestehenden Kaltstart-Tests bleiben im unveränderten `PlaybackApiTest`.

Reproduzierbare statische Befehle ab Repository-Wurzel:

```sh
cppcheck --quiet --enable=warning,performance,portability --std=c++17 --suppress=missingIncludeSystem -igraphify-out .
lizard -w .
git -c core.safecrlf=false diff --check
sh -n tests/run-discover-tests.sh
```

Ergebnis: Cppcheck meldet ausschließlich die bekannte und erlaubte
`ArtworkReplicantView::Instantiate`-Archiving-Ausnahme. Lizard ohne Warnungen;
Whitespace einschließlich neuer Dateien und Shell-Syntax sauber. Source-,
Include-, Makefile- und Runner-Verknüpfungen bestanden.

Protokolle und zusätzliche Source-/Include-/Makefile-/Runner-Prüfungen:
`../.phase5-verification/`; reproduzierbar mit
`python ../.phase5-verification/check-phase5a.py` unter Windows.
Der Source-Abgleich mit dem Commit prüft insbesondere die unveränderten
Vorschau- und Gerätefreigabe-Funktionen; er ist kein ausgeführter GUI-Test.
Kein Repository-Formatter konfiguriert, lokaler C++-Stil übernommen.
Kein Build oder kompilierender Testlauf durch den Agenten ausgeführt, gemäß
`AGENTS.md`. Ein aktueller Clang-Tidy-Nachweis bleibt offen.

## Kurze Haiku-Abnahme für dieses Paket

1. `sh tests/run-discover-tests.sh` ab Projektwurzel: aktuell 36 Programme;
   erwartet: alle bestanden (noch kein aktueller Lauf durch den Agenten).
   Anschließend normalen App-Build ausführen.
2. Frischer Start ohne Gerät → Titel → Local Playback: während librespot startet
   keine vorzeitige Playing-Anzeige. Gerätedialog auch einmal abbrechen.
3. In einer Playlist mit Shuffle an einen bestimmten Titel starten: gewählter
   Titel startet, Shuffle bleibt anschließend an. Mit Shuffle aus wiederholen.
4. Albumtitel und normale Folge-Queue starten; gewählter Titel und folgende
   Reihenfolge stimmen. Vorhandene doppelte Titel bleiben erhalten.
5. Podcast-Episode und Hörbuch-Kapitel mit gespeicherter Position starten;
   Kapitel-Weiter bleibt innerhalb der Kapitel-Folge.
6. Gerätewechsel und Titelstart im bestätigten Windows-Webplayer auf BARON;
   zusätzlich einen Titelwechsel während laufender Wiedergabe und aus Pause.

Nutzer meldet anschließend den folgenden Startup-Fehler; die Abnahme bleibt offen.

## Nachtrag 2026-09-17: alter Titel nach Local Playback

Nutzer-Repro: Track 3 pausieren, Haify neu starten, Track 9 wählen und Local
Playback starten. Zunächst wird eine falsche Anzeige bei passendem Audio gemeldet;
anschließend präzisiert der Nutzer: Track 3 bleibt pausiert, Play setzt Track 3
fort und die Wahl von Track 9 wirkt ignoriert. Ob die Anzeige von selbst korrigiert
wird, ist nicht beobachtet. Ein Debug-Trace dieses Ablaufs steht noch aus.

Der Source zeigt zwei konkrete Fehlerpfade, die zum Repro passen:

1. Die lokale Gerätewahl erklärte einen Namensfund unabhängig von `is_active`
   als aktiv und verbrauchte das ausstehende Play-Kommando. Parallel plante App
   ihre eigene Geräteübernahme. Damit konnte Play vor dem Transfer ausgeführt
   werden. Die tatsächlich aufgetretene Reihenfolge ist ohne Trace unbestätigt.
2. Der Player las Ereignisdateien aus früheren Librespot-Prozessen ein. Ein neues
   `playing` übernahm vorgemerkte Metadaten ohne Prüfung von `track_id`. Dadurch
   konnte ein Ereignis zu Track 9 den gespeicherten Titel von Track 3 veröffentlichen.

Korrektur:

- `LocalPlaybackReadiness` besitzt die Freigabe für einen lokalen Startversuch.
  App schreibt sie auf ihrem Looper, der Player liest atomar. Ein neuer lokaler
  Start wartet auf eine **neue** erfolgreiche Transfer-Antwort und anschließend
  ein passendes, tatsächlich aktives Gerät. Ein alter Erfolg genügt auch dann
  nicht, wenn der neue Startauftrag noch in Apps Nachrichtenwarteschlange liegt.
- Transfer-Timer, Geräteantworten, Idle-Entscheidungen und Transfer-Abschlüsse
  tragen dieselbe `playback_generation`. Veraltete Antworten/Timer dürfen keinen
  weiteren Transfer starten oder das aktuelle Kommando freigeben. Stop/Exit
  verwerfen die Freigabe. Fehler geben die Auswahl nicht als Erfolg frei; nach
  der bestehenden begrenzten Wartezeit erscheint wieder die Geräteauswahl.
- Das vollständige Play-Kommando bleibt bis zur Freigabe im Player erhalten.
  Späte Antworten der ursprünglichen Gerätesuche und Antworten nach Abbruch
  dürfen es nicht vorzeitig verbrauchen. Bestehende HTTP-Parameter und die
  Transfer-Resume-Semantik bleiben unverändert; es gibt keine Play-Wiederholung.
- `LibrespotEventState` akzeptiert nur die aktuelle Prozesssitzung und ordnet
  Playing/Pause/Position anhand der unveränderten Librespot-Track-ID zu. Bei
  fehlender oder fremder Identität wird gepollt statt fremde Metadaten zu verwenden.
  Ein bestätigter Titelwechsel verwirft alte vorgemerkte Track-Metadaten.
- Der generierte Hook erhält seinen Sitzungsschlüssel als eigenes Argument und
  schreibt `session_id`. Dadurch behalten verspätete Aufrufe alter Prozesse ihren
  alten Schlüssel, auch wenn die gemeinsam genutzte Skriptdatei erneuert wurde.
  Alte Dateien ohne Sitzungsschlüssel werden ignoriert. Startfehler, Stop und
  Prozessende deaktivieren den Ereignisleser; Spotify-Polls bleiben verfügbar.

Der zusätzliche Zustand gehört zu lokalen Startversuchen bzw. Ereignissitzungen,
nicht zu einer neuen globalen Playback-Transaktion. Die vollständige Auslagerung
der Librespot-Orchestrierung bleibt das nächste Phase-5-Paket. `App` übernimmt
hier Lebenszyklus und Antwortweiterleitung; neue Freigabe-/Identitätsregeln liegen
in den kleinen frameworkfreien Helfern. API-Annahme allein bestätigt weiterhin
keine hörbare Wiedergabe. Der bisherige Kaltstart-Vorschau-Fix bleibt erhalten.

Primärquelle geprüft am 2026-09-17:
[Librespot EventHandler](https://github.com/librespot-org/librespot/blob/dev/src/player_event_handler.rs).
Track-Metadaten und Positionsereignisse enthalten Track-IDs; der Handler reicht
zusätzliche Hook-Argumente an das gestartete Programm weiter. Die IDs werden
unverändert verglichen, ohne eine bestimmte ID-Kodierung vorauszusetzen.

Neu: `LocalPlaybackStateTest` deckt alte Dateien, Prozesswechsel, fremde Track-IDs,
Kapitel/Poll-Reihenfolge, verspätete Transfer-Erfolge sowie Transfer→Track-9-
Request-Traces und Fehlerantworten ab. `MessageContractsTest` prüft zusätzlich
die native Gerätemapping-Grenze: passender Name bei `is_active=false` reicht nicht;
ein anderer aktiver Remote-Client darf bei expliziter lokaler Wahl nicht gewinnen.
Der Test linkt dafür `PlaybackDeviceResolver.cpp` und die bereits im App-Build
verwendete `localestub`-Bibliothek.

Statische Gesamtprüfungen erneut ohne neue Befunde. Der aus dem C++-Source
rekonstruierte Hook wurde ohne Build mit vier künstlichen Ereignissen ausgeführt:
Sitzung/ID und getrennte Track-/Playback-Dateien stimmen, einschließlich eines
verspäteten alten Aufrufs. Protokoll: `../.phase5-verification/local-event-script.log`;
reproduzierbar mit `python ../.phase5-verification/check-local-event-script.py`.
C++-Fixtures, App-Build und echter Haiku-Repro wurden nicht vom Agenten ausgeführt.

Gezielte Abnahme: Track 3 pausieren → Haify beenden/neustarten → Track 9 wählen
→ Local Playback. Nach der Übernahme müssen Track 9 **und dessen Audio** starten.
Auch Abbruch und erneuten lokalen Start sowie Pause/Weiter eines Hörbuchs prüfen.
Bei erneutem Fehler den vollständigen `--debug`-Trace einschließlich SEND/RECV,
`Executing pending playback` und `Playback start` sichern. Keine bestätigte
Runtime-Behebung behauptet, solange dieser Gegencheck aussteht.

## Nachtrag: kurzer alter Titel während erfolgreicher Übernahme

Nutzer bestätigt anschließend: Der gewählte neue Titel startet nun korrekt.
Nach Local Playback erscheint jedoch noch etwa eine Sekunde der vorherige Titel.
Dies bestätigt den funktionierenden Startpfad, aber noch keine vollständig saubere
Anzeige und keinen gesondert protokollierten Durchlauf aller Tests.

Der Transfer darf den vorherigen Titel kurz als realen Zwischenstand melden.
`LocalPlaybackPresentation` hält deshalb bei einem ausdrücklich gewählten Track
oder Kapitel die bisherige Anzeige vom lokalen Startauftrag bis zur ersten realen
Playing-Bestätigung des anschließend gesendeten Ziels. Fremde/leere Polls, ein
pausierter Zieltrack und optimistische Metadaten beenden diese Anzeigesperre nicht.
Auch direkte Librespot-Positionsereignisse dürfen Playing/Position dabei nicht
vorzeitig ändern; vorgemerkte Metadaten gehen durch dieselbe Freigabe wie Polls.

Der Player-Looper besitzt diesen reinen Anzeigezustand. Er enthält weder API-
Aufrufe noch Prozess- oder Cache-Logik. Der Audio-/Transfer-Ablauf bleibt erhalten.
Die Sperre ist auf 15 Sekunden begrenzt und beginnt beim tatsächlichen Dispatch
erneut, damit ein gescheiterter oder unbestätigter Start den realen Status nicht
dauerhaft verdeckt. Abbruch, erneute Geräteauswahl, Resume/Skip oder manueller
Gerätewechsel beenden sie. Kontext-Play und Resume ohne konkreten Zieltrack
erhalten keine Sperre. Ein neuer expliziter Titel während dieser Übergangsphase
ersetzt das wartende Ziel.

Das vorhandene `LocalPlaybackStateTest` ergänzt diese Ereignisfolgen, Kapitel-
Ersetzung, Abbruch und beide Timeout-Grenzen. Weiterhin 28 Programme. Cppcheck
(nur bekannte Archiving-Ausnahme), Lizard, Whitespace, Shell-Syntax und Source-
Einbindung ohne neue Befunde. Kein Build oder C++-Testlauf durch den Agenten.
Neue manuelle Abnahme: denselben Track-3/Track-9-Kaltstart wiederholen; der alte
Titel darf während der Übernahme nicht neu aufblitzen, der neue Titel erst mit
bestätigter Wiedergabe erscheinen. Abbruch bleibt ebenfalls zu prüfen.

## Fortsetzung 2026-09-18: 5b Transfer-Orchestrierung

Beim Sitzungsabbruch lagen Controller, Nachrichtenadapter, zwei Fixtures und
App-Einbindung bereits vor. Diese wurden gegen den gesicherten 5a-Stand geprüft;
die fehlende Vertrags- und Verifikationsdokumentation ist jetzt nachgeführt.

`LibrespotTransferController` besitzt Gerätesuche, Idle-Entscheidung, Retry-Zähler,
ausstehenden Schritt und Freigabe. `DispatchLibrespotTransfer` führt genau einen
Request über die bestehende `PlaybackApi` aus. App besitzt Controller und API,
führt die zurückgegebenen Effekte aus und bleibt für Prozess, Timer und OAuth-
Lebenszyklus verantwortlich. API-Callbacks besitzen Ergebnisdaten und Messenger,
keinen Controller- oder App-Zeiger. Die API muss bis Callback-Ende leben.

Erst der App-Looper akzeptiert einen passenden Request aus Generation, Sequenz,
Schritt und Ziel. Alte, doppelte und überholte Antworten bleiben wirkungslos;
vor Annahme wird ein inzwischen beendeter Prozess geerntet und der Start verworfen.
Fenster lesen den PID atomar und veranlassen Reaping auf dem App-Looper, statt
Controllerzustand von einem Fensterthread aus zu verändern. Die Menüanzeige kann
dadurch bis zur nächsten Aktualisierung den vorigen Prozessstatus zeigen.
Ein vor Dispatch verlorener Auth-/Prozesszustand wird als Fehlschlag zurückgeführt;
nach erneuter Authentifizierung kann die Suche wieder beginnen.

Beibehaltene Semantik:

- Expliziter lokaler Start sucht den ersten exakt passenden Gerätenamen und
  überträgt an dessen nichtleere ID. Der Namensfund allein gibt noch kein Play frei.
- Autostart prüft zunächst Wiedergabe: ein anderer laufender Client bleibt aktiv;
  pausierter/fehlender Zustand oder das eigene Gerät erlauben den Transfer.
- Die Suche hat höchstens fünf Versuche im Sekundenabstand, während OAuth 150
  Versuche im Abstand von zwei Sekunden. Auth-Abschluss setzt das Budget zurück,
  dupliziert aber keinen ausstehenden Request oder bereits erfolgreichen Transfer.
- Transfer verwendet weiterhin genau eine Geräte-ID und `play=true`. Es gibt
  keine zusätzlichen Play-/Transfer-Retries und keinen Ersatz für Transferfehler.
- Suchfehler verbrauchen wie bisher das Suchbudget. Das ist eine ausdrücklich
  erhaltene Kompatibilitätsausnahme, keine neue allgemeine Retry-Policy:
  401/403/429/5xx/Transportfehler behalten Status und Retry-Provenienz; die lokalen
  Suchintervalle werten `retry_after` weiterhin nicht aus. Eine Änderung gehört
  in einen separaten Policy-Schritt.

Ungültige JSON-Typen bei Suche/Idle-Prüfung werden jetzt als `responseValid=false`
ausgegeben, statt eine Ausnahme oder einen scheinbar gültigen Idle-Zustand zu
erzeugen. Ein erfolgreiches leeres GET wird vom bestehenden Request-Client als
`{}` geliefert und behält den bisherigen Idle-Default. Status/Retry -1 bedeutet
unbekannt. Kein neuer Cache: Geräte- und Playback-Reads invalidieren weiterhin
ihre bestehenden `PlaybackApi`-Keys. Die Fixtures prüfen diese Invalidierung.

Primärquellen geprüft am 2026-09-18:
[Transfer Playback](https://developer.spotify.com/documentation/web-api/reference/transfer-a-users-playback)
beschreibt eine Ziel-ID, `play=true`, 204 und die fehlende garantierte Reihenfolge
über Player-Endpunkte. [Get Available Devices](https://developer.spotify.com/documentation/web-api/reference/get-a-users-available-devices)
dokumentiert nullable IDs und die getrennte Eigenschaft `is_active`.
[Get Playback State](https://developer.spotify.com/documentation/web-api/reference/get-information-about-the-users-current-playback)
dokumentiert 200/204 und Geräte-/Playbackzustand. Die App wartet auf Antworten;
hörbare Wiedergabe bleibt durch diese API-Annahme allein unbestätigt.

`LibrespotTransferControllerTest` und `LibrespotTransferMessagesTest` sind im
Runner verdrahtet: expliziter Start, Idle-Entscheidungen, OAuth-/Retry-Grenzen,
Fehlerklassen, defekte Antworten, ungültige Kommandos, überholte Antworten,
Authentifizierung während GET/PUT sowie native Message-Roundtrips und sämtliche
Pflichtfelder. Die Nachrichtenverträge sind in [message-contracts.md](message-contracts.md)
aktualisiert; alte interne Teilergebnis-Nachrichten werden nicht mehr akzeptiert.

## Fortsetzung 2026-09-18: Librespot-Argumentbau

`playback/LibrespotArguments` besitzt jetzt die reine Abbildung eines bestehenden
`HaifySettings`-Snapshots auf Playback-/Zusatzargumente. Eine kleine öffentliche
Funktion ersetzt zwei App-Methoden. Es gibt keine Settings-Lesezugriffe,
Persistenz, Prozesse oder neuen Konfigurationsmodelle im Helfer. App komponiert
weiterhin Binary-, Cache-, OAuth- und Ereignisargumente und startet den Prozess.

Reihenfolge, SDL-/Gerätename-Defaults, zusätzliche Argumente und Deduplizierung
von `-j`/`--enable-oauth` sind unverändert. Zusätzliche Argumente werden weiterhin
an Whitespace getrennt: Anführungszeichen bleiben literal, es erfolgt keine
Shell-Auswertung. Dies ist bewusste Bestandskompatibilität, kein neuer Parser.
`LibrespotArgumentsTest` deckt Defaults, Optionen, unveränderte Präfixargumente,
Gerätenamen mit Leerzeichen, beide OAuth-Aliase und dieses Parser-Verhalten ab.

## Nachweise und Fortsetzungspunkt nach 5b

- `python ../.phase5-verification/check-phase5a.py`: Cppcheck nur bekannte
  Archiving-Ausnahme; Lizard, Whitespace (auch neue Dateien), Shell-Syntax,
  Includes, Makefile und 31 Runner-Einträge bestanden.
- `python ../.phase5-verification/check-phase5b.py`: Player und 5a-Helfer exakt
  unverändert gegenüber dem Stand vor 5b; Ereignis-Hook und Lebenszyklus-Helfer
  unverändert. Die verschobenen Argumentbau-Anweisungen stimmen nach Entfernung
  von Whitespace exakt mit dem Ausgangsstand überein; Startaufruf korrekt ersetzt.
- App umfasst nun 1974 Zeilen (2099 vor 5b, 2024 beim Wiederaufnehmen).
- Kein Formatter im Repository konfiguriert; vorhandener C++-Stil übernommen.
  Keine Builds oder C++-Fixtures ausgeführt. Aktueller Haiku-Build, 31 Tests,
  Clang-Tidy und manuelle Wiedergabe-/Anzeige-Abnahme bleiben offen.

Zusätzlich zur obigen Haiku-Abnahme prüfen: Autostart bei laufendem Remote-Client
belässt die Wiedergabe dort; expliziter Local-Start übernimmt. Während Gerätesuche
stoppen/neu starten bzw. abmelden/anmelden: kein alter Transfer darf den neuen
Start freigeben. Librespot mit Standard- und eigenen Optionen starten; bei einer
ohnehin nötigen OAuth-Registrierung auch den Registrierungsablauf prüfen.

Nächster geplanter Schnitt: fachliches URI-/Show-/Audiobook-Routing aus App lösen.
Dabei Fehler-/Fallback-Semantik des bestehenden Show/Audiobook-Probes ausdrücklich
erhalten oder gezielt korrigieren; API-Annahme bleibt getrennt von Wiedergabe.
Account-/Capability-Policy in App bleibt ebenfalls zur Prüfung vorgemerkt.
Phase 5 insgesamt ist noch nicht abgeschlossen.

## 5c: URI-/Show-/Audiobook-Navigation (2026-09-18)

`navigation/SpotifyNavigation` entscheidet fensterunabhängig über Artist,
Episode, Hörbuch, Titelstart, Sammlung und nicht unterstützte Inhalte. Es erhält
ein typisiertes Kommando mit URI, Titel, Cover und Skip-Flag sowie die aktuellen
Capability-/API-Verfügbarkeitswerte. `ResolveSpotifyShow` führt genau den bisherigen
`ContentApi::GetAudiobook`-Request aus und liefert ein typisiertes Ergebnis.

App besitzt weiterhin API, Fenster und Looper, liest Nachrichten, führt das
gewählte Navigationsziel aus und zeigt vorhandene Hinweise. URI-Auswahl,
Hörbuch-Freigabe und JSON-Abbildung liegen außerhalb der App. Der Callback besitzt
kopiertes Kommando und Messenger; er greift nicht auf Fenster-/App-Zustand zu.
Die API muss bis zu seinem Abschluss leben. Antworten werden wie bisher in ihrer
Ankunftsreihenfolge verarbeitet; beim Empfang gilt der aktuelle Capability-Stand.
Es gibt keine neue Account-Epoche, Request-Deduplizierung, Cache-Schicht oder
Invalidierung. Die bestehenden `ContentApi`-/Request-Client-Regeln bleiben erhalten.

### Bestandskompatibilität und gezielte Korrektur

- Tracks erzeugen weiterhin Play-Kommandos; Episoden öffnen ihr Detailfenster.
  Artist/Hörbuch öffnen ihre bisherigen Fenster; Album/Playlist/Show und exakt
  `spotify:collection` verwenden die Sammlung. Unbekannte Typen bleiben abgelehnt.
- Ein Show-Probeaufruf erfolgt nur mit aktivierter Hörbuch-Unterstützung,
  verfügbarer API, nichtleerer ID und ohne Skip-Flag. Eine Show ohne ID geht wie
  bisher letztlich direkt zur Sammlung; die überflüssige lokale Retry-Nachricht
  entfällt. Es wird kein HTTP-Request mit leerer ID erzeugt.
- Erfolgreiche Objektantworten behalten die alte Regel: Hörbuch-URI aus dem
  Objekt übernehmen, andernfalls URI aus der angefragten ID synthetisieren.
  Das gilt auch für `{}` oder eine falsch typisierte URI. `SynthesizedAudiobookUri`
  kennzeichnet diesen Ersatz ausdrücklich; es ist keine bestätigte API-URI.
- **Kompatibilitätsausnahme:** Jede erfolglose Probe (einschließlich Auth,
  Berechtigung, Rate-Limit, Transport und Server) öffnet weiterhin die ursprüngliche
  Show. Erfolgreiche Nicht-Objekt-Antworten tun dies ebenfalls, mit eigener
  Ergebnisart. HTTP-Status/Retry bleiben getrennt erhalten, unbekannte/defekte Werte
  werden -1. Dieser Ersatz bedeutet nicht „als Podcast bestätigt“ und löst keinen
  zusätzlichen Request oder Retry aus. Engere Fallback-Regeln sind separat zu
  entscheiden, statt im Zuge der Extraktion bestehendes Öffnen zu entfernen.
- **Cover-Bug behoben:** Die bisherige nachgelagerte `open`-Nachricht übernahm nur
  URI und Titel. Nach einer erfolglosen Hörbuch-Probe ging das mitgelieferte Cover
  verloren. Das typisierte Ergebnis trägt jetzt das vollständige Kommando weiter.
  Repro/Abnahme: Show mit Titel/Cover öffnen, Probe schlägt fehl, Sammlung muss
  dieselbe URI, denselben Titel und dasselbe Cover erhalten. Fixture:
  `TestFailuresAndCoverRegression`, zusätzlich native Message-Roundtrips.
- Gültige alte `open`-Sender bleiben kompatibel; falsch typisierte oder doppelte
  bekannte Felder werden nun konsequent verworfen, statt zufällig den ersten Wert
  zu verwenden. Beide Cover-Schreibweisen und deren Vorrang bleiben unterstützt.

Primärquelle am 2026-09-18 geprüft:
[Get an Audiobook](https://developer.spotify.com/documentation/web-api/reference/get-an-audiobook)
beschreibt `/audiobooks/{id}`, die Hörbuch-ID/URI sowie 200/400/401/403/404/429.
Sie garantiert weder gemeinsame Show-/Hörbuch-IDs noch Podcast-Klassifikation
bei Fehlern. Probe, breite Fallback-Regel und URI-Synthese sind daher ausdrücklich
Haify-Bestandsverhalten. Es wird keine neue API-Zusage daraus abgeleitet.

### Nachweise und nächste Schritte

Zwei neue Fixtures im Runner, jetzt **33 Programme**:
`SpotifyNavigationTest` prüft Ziel-/Capability-Matrix, Probe-Mapping,
Fehler-/Malformed-Provenienz, Cover-Erhalt, kopierte Kommandos, vertauschte
Antwortreihenfolge, synchrone Callbacks und ungültige Kommandos ohne Requests.
`SpotifyNavigationMessagesTest` prüft alte Öffnungsnachrichten, Defaults/Aliase,
alle Ergebnisarten, Pflichtfelder, doppelte/falsche Felder und Zielwidersprüche
an echter Haiku-BMessage-Serialisierung. Kein Fixture wurde hier kompiliert oder
ausgeführt; ein aktueller Haiku-Lauf bleibt erforderlich.

`python ../.phase5-verification/check-phase5a.py`: Cppcheck nur bekannte
Archiving-Ausnahme; Lizard, Whitespace einschließlich neuer Dateien, Shell-Syntax
und Source-/Include-/Runner-Verknüpfungen bestanden. `check-phase5b.py` bestätigt
weiterhin unveränderte Playback-Helfer. `check-phase5c.py` bestätigt zusätzlich
83 unveränderte App-Funktionen und die neue Navigationsverdrahtung.
App umfasst 1979 Zeilen; die Trennung ersetzt Policy durch explizites Routing,
nicht durch einen breiten Umbau. Kein Repository-Formatter; lokaler Stil erhalten.
Build, C++-Tests, Clang-Tidy und manuelle Haiku-Prüfung stehen weiterhin aus.

Manuelle Abnahme: Artist, Episode, Track, Album, Playlist, Liked Songs und
Hörbuch öffnen; erneut öffnen aktiviert das vorhandene Fenster. Hörbücher in
Settings deaktivieren und den vorhandenen Hinweis prüfen. Normale Podcast-Show
bei aktivierter Hörbuch-Unterstützung öffnen und Cover-Erhalt kontrollieren.
Mehrere Shows rasch nacheinander öffnen; jede Antwort muss ihre eigenen Metadaten
behalten. Die bisherigen Playback-/Kaltstart-Regressionen bleiben erforderlich.

Der damalige nächste Schnitt Account-/Capability-Policy ist nachstehend umgesetzt.

## 5d: Account, Credentials und Capability-Lebenszyklus (2026-09-18)

Die restlichen fachlichen Regeln sind aus App ausgelagert:

| Verantwortung | Eigentümer |
| --- | --- |
| Scope-Prüfung, Account-Aliasvergleich, Auth-Start und Refresh-Zeitregel | Reine Funktionen in `SpotifySessionPolicy.h` |
| Token validieren, speichern/löschen und API-Token setzen | `SpotifyCredentialStore`; Persistenz weiter über `SettingsController` |
| Profil abbilden, Identität anwenden und beschreibbare Playlists neu laden | `SpotifyAccountSession`; API und Cache weiter in den vorhandenen Diensten |
| Capability-Probe, Cache und Generationen | `SpotifyCapabilities` |
| Native Ergebnisnachrichten | `SpotifySessionMessages` |
| Looper, Request-Annahme, Timer, OAuth-/Prozesslebenszyklus und Fenster | App; keine neue fachliche Policy |

App besitzt die Dienste bis zum Callback-Abschluss. Account-Requests besitzen
ihre kopierten Ergebnisse; nur der App-Looper verändert die Request-ID-Sperre.
Ein neuer Request, Abmelden oder neuer interaktiver Auth-Versuch entwertet alte
Profilantworten. Ein Ergebnis wird höchstens einmal angenommen. Fehler und
ungültige Profile bleiben typisiert mit Status/Retry erhalten.

### Identität, Cache und Persistenz

- Profil-Refresh verwendet ausdrücklich `RefreshCurrentUserProfile`: Der
  Request-Client entwertet den accountgebundenen `/me`-Cache und trennt laufende
  Lesergruppen vor dem neuen GET. Sonst könnte ein neuer Auth-Versuch noch die
  Antwort eines alten Tokens übernehmen. Alte Antworten erreichen nur ihre
  alten Leser, dürfen den neuen Cache nicht füllen und werden am App-Gate verworfen.
  Normale `GetCurrentUserProfile`-Aufrufe behalten ihre bisherige Cache-Nutzung.
- Die bestehende ID-Regel bleibt: `id` bevorzugen; nur bei fehlendem Feld auf
  `account_id` ausweichen. Vorhandene leere/falsch typisierte IDs sind ungültig.
  Der Aliasvergleich vermeidet einen falschen Kontowechsel, wenn die gespeicherte
  ID einem der beiden Antwortfelder entspricht. Der vollständige Profilkontext
  wird bis zu dieser Entscheidung mitgeführt.
- Erst nach erfolgreicher Speicherung wird die Identität angewendet. Ein echter
  Kontowechsel löscht die API-Sitzung und Capability-Zustand, setzt Identität/Token
  und lässt App die Fenster neu laden. Eine erste Identifizierung oder Alias-
  Anpassung verwendet weiterhin `SetAccountId` mit dessen Cache-Invalidierung.
- Die abgeleitete Liste beschreibbarer Playlists wird nach Identifizierung über
  die bestehende `PlaylistApi` neu aufgebaut. Unveränderte Accounts dürfen die
  aktuelle Rohantwort wiederverwenden. Benachrichtigungen tragen die Account-ID;
  Fehler werden protokolliert und erzeugen keine Erfolgsnachricht. Cache-Schlüssel,
  Dateiformate und bestehende Sitzungsprüfung bleiben unverändert.
- Fehlender Access-Token wird jetzt **vor** dem Schreiben abgelehnt; zuvor konnten
  Ablaufzeit und Refresh-Token trotzdem verändert werden. Nicht zurückgelieferte
  Refresh-Tokens/Scopes behalten ihre bisherigen Werte. Speicherfehler verändern
  den API-Token nicht. Abmelden leert den Speicherzustand auch bei Dateifehlern;
  die Rückmeldung benennt dann ausdrücklich die nicht gelöschten Zugangsdaten.

Primärquellen geprüft am 2026-09-18:
[Current User's Profile](https://developer.spotify.com/documentation/web-api/reference/get-current-users-profile)
empfiehlt das unveränderliche `account_id` für Account-Verknüpfungen.
**Dokumentierte Bestandsausnahme:** Haify verwendet bislang dieselbe gespeicherte
ID auch für Playlist-Owner-Vergleiche und Cache-Zuordnung. Diese Extraktion erhält
deshalb den bisherigen `id`-Vorrang samt Aliasabgleich; eine Datenmigration zu
getrennten Identitäten wird hier weder vorgenommen noch als erledigt behauptet.
[Refreshing tokens](https://developer.spotify.com/documentation/web-api/tutorials/refreshing-tokens)
bestätigt das Beibehalten eines nicht erneuerten Refresh-Tokens sowie erneute
Autorisierung statt Refresh-Wiederholung bei `invalid_grant`. Die bestehende
Haify-Retry-Regel für andere Fehler bleibt erhalten.

### Capability- und Auth-Antworten

Capability-Zustand ist ein sitzungsgebundener In-Memory-Cache, ohne neue Datei
oder Schemaänderung. `Reset`, Moduswechsel und API-Wechsel entwerten laufende
Probes über eine Generation und schließen ihre Waiter mit `Unknown` ab.
Alte Saved-/Search-Antworten dürfen weder aktuellen Zustand noch neue Waiter
verändern. Die bestehenden Endpunkte, Market-Regeln und Fehlerklassen bleiben:
403 verboten; temporärer Fehler darf zuvor bestätigte Verfügbarkeit erhalten;
explizit deaktiviert sperrt, explizit aktiviert gibt frei. `force` umgeht einen
abgeschlossenen Cachewert, bündelt aber wie bisher eine bereits laufende Probe.
App veröffentlicht bei einer Callback-Nachricht stets den aktuellen Snapshot.

Zusätzlicher Auth-Repro: Refresh A läuft, Abmelden/neue Anmeldung beendet seine
Waiter, Refresh B beginnt, erst danach antwortet A. A wird nun vollständig
ignoriert und darf B nicht mehr als fehlgeschlagen abschließen. App besitzt
weiterhin die bestehenden Token-Generationen und deren Lifecycle-Abschluss.

### Abschlussnachweis und verbleibende Gates

Drei neue Fixtures ergeben **36 Programme** im gemeinsamen Runner:

- `SpotifyAccountSessionTest`: echte API-/Cache-Implementierung mit Speicher-
  Settings und verbotenem Netzwerk; ID-/Aliaswechsel, Cache-Neuaufbau,
  vertauschte alte/neue Profilantworten, Abmelden während Playlist-Read,
  Persistenzfehler, fehlende Tokens und Auth-Grenzwerte.
- `SpotifyCapabilitiesTest`: gebündelte Requests, Force/Cache, Market-/HTTP-
  Fehler, Reset während Saved-/Search-Request, Modus-/API-Wechsel und Abmelden.
- `SpotifySessionMessagesTest`: echte Haiku-BMessage-Roundtrips, Pflichtfelder,
  doppelte/falsche Typen, widersprüchliche Ergebnisse und Token-Defaults.

`check-phase5d.py` prüft die Verdrahtung und unveränderte App-Funktionen gegenüber
dem gesicherten 5c-Stand; `check-phase5b.py`/`check-phase5c.py` prüfen die vorherigen
Grenzen weiter. `check-phase5a.py` führt Cppcheck, Lizard, Whitespace einschließlich
neuer Quellen, Shell-Syntax und Source-/Include-/Makefile-/Runner-Prüfungen aus.
Protokolle: `../.phase5-verification/`. Statischer Abschluss: keine neuen
Cppcheck-Befunde (nur bekannte Archiving-Ausnahme), keine Lizard-Warnungen,
Whitespace/Shell-Syntax/Source-Prüfungen bestanden. Kein Formatter konfiguriert.

Alle geplanten Extraktionen sind implementiert: Playback-Start, lokale Übergabe,
Librespot-Argumentbau, globale URI-Navigation und Account-/Capability-Policy.
Der optionale weitere App-Metadata-Service ist nicht erforderlich: Der bestehende
Helfer liest bereits aus der laufenden Binary, zusätzliche Metadaten werden nicht
benötigt. Prozessstart/-ende, Timer und Fensterverwaltung bleiben bewusst in App.

**Noch nicht nachgewiesen:** Kompilierung, Ausführung der 36 C++-Programme,
aktuelles Haiku-Clang-Tidy und manuelle UI-/Playback-Abnahme. Nach `AGENTS.md`
wurden keine Builds gestartet. Vor technischer Abnahme die
[gemeinsame Prüfliste](phase-5-manual-tests.md) ausführen; Phase 6 wurde nicht begonnen.
