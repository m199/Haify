# Message-Verträge: Drag-and-Drop und Playback

Stand: 2026-09-17. Codes und Feldnamen stehen in `Messages.h`, Builder und
Reader in `MessageContracts.h`. `DragItem.h` enthält das frameworkfreie
Drag-Modell; Playlist-Zielentscheidungen stehen in `UiLogic.h`, Discover-Tab-
und Hover-Entscheidungen in `discover/DiscoverTabPolicy.h`.
Die Discover-Tab-Verträge und ihre Abnahme sind in
[phase-3-verification.md](phase-3-verification.md) dokumentiert.
Die ergänzten Zeilen-, Paging-, Library- und Cache-Nachrichten aus Phase 3
stehen in [discover-state-contracts.md](discover-state-contracts.md).

## Gemeinsame Regeln

Die Wire-Codes und bisherigen Feldnamen bleiben unverändert. Neue Sender nutzen
Builder; bestehende Metadaten-Ergänzungen verwenden Feldkonstanten oder die
vorhandenen Helfer `NowPlayingItem`/`NowPlayingFields.h`. Reader liefern `false`
bei ungültigen Pflichtfeldern, falschen Typen oder mehrfachen Singleton-Feldern.
Die öffentlichen Reader veröffentlichen ihr Resultat erst nach erfolgreicher
Prüfung; bei Fehlern bleibt das übergebene Resultat unverändert.

Fehlende optionale Felder erhalten die unten dokumentierten Defaults. Unbekannte
Zusatzfelder werden toleriert und beim Weiterleiten erhalten. Ein `PlayCommand`
ist eine Sicht auf die Steuerfelder, kein Ersatz für die vollständige Nachricht.
Insbesondere dürfen Forwarder die Nachricht nicht nur aus der URI neu erzeugen.
Die Suche leitet deshalb jetzt auch Titel, Kontext und weitere Metadaten weiter.

Builder erzeugen eine neue Nachricht. Zusätzliche Singleton-Felder nur einmal
hinzufügen oder mit `Replace*` aktualisieren. Ein misslungener Reader führt zu
keinem Playback-/Queue-/Playlist-Kommando; das Ende einer Drag-Geste darf weiterhin
Marker und den aktiven Drag-State aufräumen. Transport-/Spotify-Fehler werden
durch diese Serialisierung weder in Erfolg umgewandelt noch mit Ersatzdaten verdeckt.

## Drag und Drop

| Code | Produzenten | Konsumenten |
| --- | --- | --- |
| `MSG_DRAG_ITEM` (`drag`) | Discover-, Playlist-, Queue- und Artist-Listen | Listenfilter, PlaylistWindow, Discovery-Forwarder |
| `MSG_PLAYLIST_DROP` (`drpT`) | PlaylistWindow-DropFilter, vollständige Kopie des Drag | PlaylistWindow zur Reorder-/Add-Entscheidung |
| `MSG_DISCOVER_DRAG_HOVER` (`dDhv`) | Discovery-Listen/TabView, Kopie des Drag | DiscoverWindow zur Zielanzeige |
| `MSG_DISCOVER_DROP` (`dDrp`) | Discovery-Forwarder, Kopie des Drag | DiscoverWindow |

| Feld | Typ | Vertrag |
| --- | --- | --- |
| `uri` | string | Nichtleere Spotify-Item-URI mit ID. Legacy: ersatzweise `trackUri`, dann `albumUri`. Vorhandene nichtleere Aliase müssen übereinstimmen. |
| `itemType` | string | Optional; fehlt/leer: aus URI ableiten. Sonst muss der Typ zur URI passen. |
| `sourcePlaylist` | string | Optional, Default leer; URI des Quellfensters, auch Album/Show/Collection möglich. Nur dieselbe Zielplaylist erlaubt Reorder. |
| `sourceIndex` | int32 | Optional, Default -1. Ab 0 muss eine Quelle vorhanden sein; bezeichnet die sichtbare Quellzeile wie bisher. Werte unter -1 sind ungültig. |
| `sourceIndices`, `sourceUris` | wiederholte int32, string | Optionales Paar für Gruppen-Reorder: gleiche positive Anzahl, Indizes streng aufsteigend und ab 0, spielbare URIs mit ID. Der gegriffene `sourceIndex` muss mit seiner `uri` enthalten sein. Gleiche URI an verschiedenen Positionen bleibt erlaubt. |
| `sourceSnapshot` | string | Für Gruppen-Reorder Pflicht; Version der Quellplaylist beim Greifen. Das Ziel prüft Snapshot und aktuelle URIs an allen Quellpositionen. |
| `dropIntent` | int32 | Optional, Default 0 (`Automatic`). 1 (`Add`) und 2 (`Reorder`) schränken die vorhandene Zielentscheidung ein. Andere Werte sind ungültig. |
| `title`, `artist`, `album`, `duration` | string | Optionale Anzeige-Metadaten; keine Entscheidungsgrundlage des Drag-Readers. |
| `tab` | int32 | Für den Discovery-Drop Pflicht, ab 0. Das Fenster prüft zusätzlich den erwarteten logischen Tab. |
| `targetUri`, `targetWritable` | string, bool | Optional, Defaults leer/false. Für Playlist-Ziele entscheidet weiterhin die bestehende Zielpolicy über Schreibbarkeit. |
| `targetTitle` | string | Optionale Anzeige-Metadaten. |

Neue Drag-Builder schreiben `Automatic` und weiterhin die passenden Legacy-Aliase.
`Add` erzwingt kein Kopieren innerhalb derselben Playlist; es akzeptiert nur einen
von der bestehenden Policy ohnehin erlaubten Add. `Reorder` darf nicht als
Library-/Playlist-Add umgedeutet werden. Schreibrechte und laufende Mutationen
bleiben unabhängig vom Intent maßgeblich.

Die Playlist-Liste setzt für mehrere markierte Zeilen explizit `Reorder` und
überträgt ihre Positionen in Listenreihenfolge, nicht in Klickreihenfolge.
Diese Geste verschiebt die Gruppe innerhalb derselben Playlist; andere
Playlist-/Discover-Ziele dürfen sie nicht als einzelnen Titel kopieren.
Legacy-Einzeltitel ohne die zusätzlichen Gruppenfelder bleiben kompatibel.

Die Liste startet die Geste und registriert die Originalnachricht im vorhandenen
`HaifyDragState`. Forwarder kopieren sie inklusive Haiku-Drop-Koordinaten; das
Zielfenster besitzt die Mutation. Die bestehende Drag-Generation verhindert
veraltete Tab-Wechsel. Es wurde kein neuer Cache oder Persistenzpfad eingeführt.
Der Playlist-Filter verwendet beim erneuten Posten `MSG_PLAYLIST_DROP`, damit
die Nachricht nicht erneut als äußerer Drag abgefangen wird. `ReadDragItem`
akzeptiert diesen internen Code mit denselben Payload-Prüfungen wie `drag`.

## Playback

| Code | Pflicht | Optional / Semantik | Produzent → Konsument |
| --- | --- | --- | --- |
| `MSG_PLAY_URI` (`play`) | `uri` (string), ersatzweise `trackUri` | `context_uri`, `device_id`: string, Default leer; `start_position_ms`: int32 ab 0, Default 0; `next_queue_uri`: wiederholte Track-/Episode-URIs in Reihenfolge | Fenster/Listen → App → PlayerWindow |
| `MSG_QUEUE_ITEM` (`addQ`) | `trackUri` (string), auch `uri` lesbar | Nur Track oder Episode mit ID | TrackContextMenu → dessen Auswahlhandler → Playback-API |
| `MSG_SEARCH_QUEUE_ITEM` (`sQue`) | `uri` (string), auch `trackUri` lesbar | Derselbe Queue-Reader; bisheriger Such-Code bleibt erhalten | Such-Kontextmenü → dessen Auswahlhandler → Playback-API |
| `MSG_CURRENT_TRACK_UPDATE` (`pStU`) | `trackUri` (string) | Leer ist für den Reader ein gültiger leerer Track-State. App ignoriert leere Broadcasts weiterhin wie bisher. | PlayerWindow → App → Discover/Playlist/Queue/Artist/Search; App synchronisiert neu geöffnete Fenster |

Play akzeptiert bekannte Item-URIs mit ID sowie die bestehenden Sonderkontexte
`spotify:collection` und `spotify:saved-episodes`. Queue-Einträge dürfen nicht leer
oder Kontext-URIs sein. Ein fehlerhafter Queue-Eintrag verwirft das gesamte
Play-Kommando; es wird keine scheinbar erfolgreiche Teil-Queue abgespielt.

`PlayerWindow` besitzt das ausstehende Play-Kommando und dessen vollständige
Metadaten. Die Geräteauswahl ersetzt `device_id` mit `SetPlaybackDevice`, auch
wenn das Feld bereits als leerer String vorhanden ist. Der Helfer erhält die
übrige Nachricht und liefert bei leerer Geräte-ID oder fehlgeschlagenem
`AddString` false; die Nachricht bleibt dann unverändert. Das ausstehende
Kommando wird erst nach erfolgreicher Ergänzung verbraucht. Play wird vor der Auswahl und
vor der Ausführung geprüft. `MessageContracts::PlayCommand` ist nun ein Alias
für das frameworkfreie `PlaybackCommand` in `playback/PlaybackCommand.h`.
`parent_kind` und `primary_open_uri` sind zusätzliche optionale String-Singletons
mit Default leer; falsche Typen oder Wiederholungen verwerfen das Kommando.
Builder schreiben sie nur, wenn nicht leer. Bestehende Sender dürfen sie bei
leeren Command-Defaults weiterhin einmalig als Metadaten ergänzen.

Shuffle-, Audiobook-, Batch- und Startentscheidungen gehören nun zu
`PlaybackStartPolicy`/`PlaybackStartController`. Der Controller bekommt die
aufgelöste Geräte-ID und unveränderte Hörbuch-Provenienz. Er liefert nach der
Request-Folge `PlaybackStartResult` an einen Callback; `PlayerWindow` protokolliert
das Ergebnis ohne Zugriff auf Fensterzustand vom API-Thread. Es entsteht kein
neuer Wire-Code. Annahme eines Play-Requests ist keine Wiedergabebestätigung;
Polls und Librespot-Ereignisse bleiben dafür maßgeblich. Fehler-/Fallback-Vertrag:
[Phase 5a](phase-5-verification.md).

Die vollständigen Poll-/Replicant-Snapshots
(`pbst`, `MSG_REPLICANT_STATE`) sind eigene bestehende Verträge; das hier
typisierte Current-Track-Update ist die Nachricht für die Track-Markierung.

## Spotify-Navigation (Phase 5c)

`navigation/SpotifyNavigationMessages` zentralisiert die Grenze zur App.
`MSG_OPEN_SPOTIFY_URI` verwendet unverändert den bestehenden Code `open`.
`uri` ist ein nichtleerer Pflicht-String; `title`, `coverUrl`, der bisherige Alias
`cover_url` und `skip_audiobook_resolution` (bool) sind optional. Ohne Werte gelten
leerer Titel/Cover und `false`. Ein nichtleeres `coverUrl` hat Vorrang vor dem Alias.
Alle bekannten Felder müssen bei Vorhandensein Singleton und richtig typisiert
sein; unbekannte Felder bleiben erlaubt. Ein Reader-Fehler verändert die Ausgabe
nicht. Vorhandene Sender dürfen weiterhin `open` senden; ihre unveränderten
Fenster-Forwarder gehören nicht zu dieser Extraktion.

`MSG_SPOTIFY_SHOW_RESOLVED` (`sNvR`) ist ein neuer rein interner Ergebnisvertrag.
Produzent ist der `ContentApi`-Callback, Empfänger der App-Looper. Pflichtfelder:

| Feld | Typ / Bedeutung |
| --- | --- |
| `request` | genau eine BMessage mit dem ursprünglichen `open`-Kommando einschließlich Titel und Cover |
| `resolution_kind` | int32: 0 API-Hörbuch-URI, 1 synthetisierte Hörbuch-URI, 2 bisheriger Show-Fallback nach API-Fehler, 3 bisheriger Show-Fallback nach Nicht-Objekt-Antwort |
| `resolved_uri` | string: Ergebnis-URI; Fallback muss exakt der ursprünglichen Show-URI entsprechen |
| `status`, `retry_after` | int32 ≥ -1; -1 bedeutet unbekannt, 0 bleibt Transportstatus |

Der ursprüngliche Request muss eine Show mit nichtleerer ID ohne Skip-Flag sein.
Hörbuch-Ergebnisse müssen das Hörbuch-Präfix tragen; eine synthetisierte URI muss
zusätzlich aus genau der ursprünglichen ID bestehen. Unbekannte Ergebnisarten,
fehlende/doppelte/falsch typisierte Felder und widersprüchliche Ziele werden
verworfen. Ein Ergebnis enthält keine rohen JSON-Daten oder Fensterzeiger.

Nach dem Lesen protokolliert App Ergebnisart, Status und Retry und routet auf dem
eigenen Looper mit dem dann aktuellen Capability-Stand. Der vollständige Request
bleibt bis dahin im Ergebnis erhalten. Bei der Weiterleitung verhindert ein
gesetztes Skip-Flag eine erneute Probe; Titel und Cover bleiben erhalten.
API-Fehler werden ausdrücklich nicht als bestätigte Podcast-Klassifikation
modelliert. Die weiterhin breite Fallback-Regel und das Syntheseverhalten sind
dokumentierte Bestandsausnahmen: [Phase 5c](phase-5-verification.md).

Die bisherige Reihenfolge bleibt erhalten: unabhängige Öffnungswünsche werden
bei Eingang ihrer Antworten verarbeitet, ohne globale Abbruch-/Account-Epoche.
Ein späterer Account-Wechsel verwirft solche Navigationsergebnisse derzeit nicht.
Einmaliger Callback und Lebensdauer der API sind Voraussetzungen wie bisher.
Der Vertrag ist weder persistiert noch ein öffentliches Replicant-Protokoll.

## Ergebnis der Geräteauswahl

Stand 2026-09-18: `LibrespotTransferController` besitzt die lokale Startfolge
und deren `LocalPlaybackReadiness`. App besitzt Controller, Prozess und Timer;
nur der App-Looper verändert den Controller. Der Player liest die Freigabe atomar.
Der Player verbraucht sein vollständiges ausstehendes Kommando erst
nach dem neuen erfolgreichen Transfer und einem aktiven lokalen Gerät. Ein
Namensfund allein ist keine Aktivierung. Fehler/Timeout behalten den bestehenden
Geräteauswahlpfad; Abbruch verwirft Kommando und lokalen Retry-Timer.

Die typisierten Builder/Reader liegen in `playback/LibrespotTransferMessages`:

| Code | Pflichtfelder | Produzent → Empfänger |
| --- | --- | --- |
| `MSG_LIBRESPOT_TRANSFER_POLL` (`tlbp`) | `playback_generation`: int64 > 0 | App/Transfer-Timer → App |
| `MSG_LIBRESPOT_TRANSFER_RESULT` (`lbpt`) | `playback_generation`: int64 > 0; `sequence`: int32 > 0; `step`: int32 (1 Gerätesuche, 2 Wiedergabeprüfung, 3 Transfer); `device_name`, `device_id`, `found_device_id`: string; `ok`, `response_valid`, `should_transfer`: bool; `status`, `retry_after`: int32 ≥ -1 | API-Callback bzw. abgewiesener Dispatch → App |
| `MSG_LIBRESPOT_REAP` (`rlbp`) | keine | Prozessstatus-Abfrage eines Fensters → App |

Alle Pflichtfelder sind Singletons; fehlende, doppelte oder falsch typisierte
Werte verwerfen die Nachricht und lassen die Reader-Ausgabe unverändert.
Gerätesuche verlangt Namen und leere Ziel-ID, die anderen Schritte Ziel-ID und
leeren Namen. Nur Gerätesuche darf `found_device_id` liefern; nur die
Wiedergabeprüfung darf `should_transfer=true` liefern. Fehlerhafte/erfolglose
Antworten dürfen keines von beidem behaupten. Status/Retry -1 bedeutet unbekannt.

Ein API-Callback sendet ausschließlich kopierte Ergebnisdaten. App akzeptiert
Generation, Sequenz, Schritt und Ziel des aktuell ausstehenden Requests genau
einmal, bevor sie den nächsten Schritt ausführt. Alte oder doppelte Antworten
sowie Ergebnisse nach Prozessende lösen weder Transfer noch Freigabe aus.
Die neuen `lbpt`-Ergebnisse melden auch Fehler. Die alten internen Codes `ldpr`
und `lpbd` entfallen; alte reine Erfolgsnachrichten `lbpt` werden abgelehnt.
Dies ist ein prozessinterner Vertrag, kein persistiertes oder öffentliches
Replicant-Protokoll. Ein Transfer-Erfolg bestätigt weiterhin nur API-Annahme.

Librespot-Dateiereignisse tragen zusätzlich `session_id` (dezimale Prozesssitzung
als String). Der Hook bekommt sie als erstes Argument. App veröffentlicht die
aktuelle Sitzung atomar; Player verwirft fremde/fehlende Sitzungen und doppelte
Ereignis-IDs. `track_changed` verknüpft `uri` und die opake `track_id`;
Playing/Pause/Position müssen zu dieser Identität passen. Fehlende Zuordnung
löst einen Spotify-Poll aus. Weitere Details und Grenzen: [Startup-Fix](phase-5-verification.md).

| Code | Payload | Bedeutung |
| --- | --- | --- |
| `MSG_PLAYBACK_DEVICE_SELECTED` (`pbDs`) | `device_id`: nichtleerer string, Pflicht | Gewähltes Gerät verwenden. |
| `MSG_PLAYBACK_DEVICE_START_LOCAL` (`pbDl`) | Keine | Bestehenden lokalen Startablauf anstoßen. |
| `MSG_PLAYBACK_DEVICE_PROMPT_CLOSED` (`pbDx`) | `cancelled`: bool, Pflicht | true verwirft das ausstehende Kommando; false meldet nur das Schließen nach einer Auswahl. |

Produzent ist `PlaybackDevicePromptWindow`, Empfänger und Eigentümer des
ausstehenden Kommandos ist `PlayerWindow`. Das Prompt sendet zuerst Auswahl oder
lokalen Start, danach beim Schließen `cancelled=false`. Ein Abbruch sendet nur
`cancelled=true`. Falsche oder fehlende Payloads werden ignoriert; ein defektes
Close-Ergebnis wird nicht als bestätigte Auswahl interpretiert. Die internen
Button-Nachrichten bleiben UI-Ereignisse und werden erst beim Senden an den
Player zu einem Ergebnis mit Payload.

## Verifikation

`tests/MessageContractsTest.cpp` prüft Builder/Reader-Roundtrips, Legacy-Aliase,
fehlende und falsch typisierte Felder, widersprüchliche Typen/URIs, Singleton-
Duplikate, Drop-Intent, Queue-Reihenfolge, Metadaten-Erhalt beim Kopieren,
Geräteersetzung und Geräteabbruch. Ein Build mit `NDEBUG` wird abgewiesen,
damit abgeschaltete Assertions keinen falschen Testerfolg melden.
Die Tests nutzen die echte Haiku-`BMessage`-Implementierung, keine nachgebaute
Windows-Attrappe. Daher ist ihre Ausführung auf dieser Windows-Arbeitsumgebung
ohne Haiku-Build nicht möglich.

Nach ausdrücklicher Build-Freigabe im Haiku-Checkout (ohne `-DNDEBUG`):

```sh
c++ -std=c++17 -I. tests/MessageContractsTest.cpp -lbe -o /tmp/haify-message-contract-tests
/tmp/haify-message-contract-tests
```

Manuelle Haiku-Abnahme nach dem App-Build:

- Track innerhalb einer eigenen Playlist umsortieren; Reihenfolge bleibt nach Reload erhalten.
- Track/Episode aus Playlist, Queue und Suche sowie Artist-Track in eine andere
  eigene Playlist ziehen; genau ein Item wird hinzugefügt. Fremde Playlist lehnt ab.
- Album aus Artist/Discovery auf Saved Albums ziehen; falscher Tab bleibt ohne Mutation.
- Bei Drag-Abbruch verschwinden Marker; Hover-Tabwechsel und Drop-Koordinaten stimmen.
- Play aus Suche, Playlist und Album starten, auch mit Shuffle. Gewählter Starttitel
  und folgende Titel bleiben korrekt; Such-Titel/Artist gehen beim Forwarding nicht verloren.
- Podcast und Audiobook abspielen: Parent-Metadaten, Resume-Position und Kapitelreihenfolge prüfen.
- Track und Episode aus beiden Kontextmenüs zur Queue hinzufügen.
- Ohne aktives Gerät: Gerät wählen, lokal starten und abbrechen separat prüfen.
  Nach Auswahl wird das ausstehende Kommando ausgeführt, nach Abbruch nicht.
- Regression Geräteergänzung: Ein gültiges Play mit vorhandenem leerem
  `device_id` und bekanntem aktivem Gerät bleibt nach der Ergänzung lesbar;
  es enthält genau eine Geräte-ID und behält Titel und Kontext. Automatisierter
  Repro: `TestPlaybackDeviceReplacement`; der frühere `AddString`-Pfad erzeugte
  zwei Werte und der Reader lehnte das Kommando ab.
- Track-Markierung in offenen und neu geöffneten Fenstern nach einem Wechsel prüfen.

Prüfstand 2026-09-07: Cppcheck 2.21.0 ausschließlich mit der dokumentierten
`ArtworkReplicantView::Instantiate`-Ausnahme; Lizard 1.24.0 ohne Warnungen.
Whitespace-Prüfung sauber. Nach ausdrücklicher Nutzerfreigabe sind Haiku-Build
und Vertrags-Test bestanden. Bear erfasst alle 51 App-Quelldateien. Clang-Tidy
prüft alle 20 betroffenen Übersetzungseinheiten ohne neue Befunde; sechs
LayoutBuilder-Warnungen sind im unveränderten Git-Ausgangsstand identisch
reproduziert. App-Start geprüft; der Nutzer meldet einen bisher unauffälligen
Smoke-Test, dessen vollständige Bestätigung noch aussteht. Details und
reproduzierbare Kommandos: [Phase-2-Abnahmeprotokoll](phase-2-verification.md).


## Phase 4: Playlist-Mutationen und Cover

Producer sind `PlaylistWriteRequests`, `PlaylistCoverRequests` bzw.
`PlaylistMetadataRequests`; Consumer ist der Looper des zugehörigen PlaylistWindow.
Zentrale Codes behalten ihre bisherigen FourCC-Werte. Die neuen Reader erwarten
die vollständigen typisierten Payloads; alte untypisierte In-Process-Ergebnisse
werden verworfen. Es gibt keine persistierte oder externe Wire-Kompatibilität.

| Code | Pflichtfelder | Abschlussverhalten |
| --- | --- | --- |
| `MSG_PLAYLIST_CLEAR_RESULT` (`pClR`), `MSG_PLAYLIST_ADD_RESULT` (`pAdR`) | `request_id`: int64 > 0; `playlist_id`: nichtleerer String; `ok`: bool; `status`: int32; `snapshot_id`: String, darf leer sein | Code identifiziert Clear/Add. Controller akzeptiert nur seinen aktuellen Auftrag und seine Playlist. Erfolg ohne Snapshot löst Nachlesen aus; 409 Reload, sonst Rollback. Fehler-Snapshot wird nicht übernommen. |
| `MSG_PLAYLIST_COVER_RESULT` (`pCvR`) | `request_id`, `playlist_id` wie oben; `error`: int32 aus `PlaylistCoverError`; `status`: int32; `cover_url`: String | `None` verlangt eine nichtleere URL. `RefreshFailed` bedeutet erfolgreichen Upload mit fehlgeschlagener Vorschau. Weitere Fehler: FileRead, NotJpeg, TooLarge, UploadFailed. Nur die aktive Identität beendet Pending. |
| `MSG_PLAYLIST_SNAPSHOT_RESULT` (`pRmM`) | Vollständiger Metadatenvertrag aus `PlaylistMetadataMessages`, Kind Playlist und nichtleere ID | Ergebnis des expliziten Nachlesens nach Mutation. Fenster ignoriert fremde Playlists, Fehler und Antworten während laufender Zeilenmutation. |

Commands/Callbacks besitzen ihre Daten, BMessenger wird kopiert. BRow-/Window-
Zeiger sind kein Bestandteil dieser Payloads. Native Zeilen verbleiben im Fenster;
Clear-Rollback hängt sie mit ihrer Auswahl wieder ein, Add-Rollback entfernt nur
die zugehörige optimistische Zeile. Fehlschlag vor Dispatch verwendet denselben
typisierten Abschlussweg. Pending-Sperren gelten bis zum passenden Ergebnis.
Das Transportmodul garantiert einen Abschluss pro API-Request; wiederholte
gelieferte UI-Ergebnisse werden über die Controller-Identität ignoriert.

Reorder-Drag-Indizes bezeichnen sichtbare Zeilen. Der Reorder-Controller übersetzt
sie anhand ihrer gespeicherten Spotify-Positionen; API-Positionen zählen auch
nicht dargestellte Einträge. `visiblePositions` in Command/Update bleibt intern
beim Looper und wird nicht über BMessage transportiert. Details und Fehlerpfade:
[Phase-4-Abnahme](phase-4-verification.md).

## Phase 5d: Account, Token und Capability (2026-09-18)

Builder/Reader: `spotify/session/SpotifySessionMessages`. App besitzt API und
Looper; asynchrone Producer senden ausschließlich kopierte Ergebnisse.

| Code | Payload | Produzent → Empfänger |
| --- | --- | --- |
| `MSG_SPOTIFY_ACCOUNT_RESULT` (`spAc`) | `request_id`: int64 > 0; `ok`, `response_valid`: bool; `status`, `retry_after`: int32 ≥ -1; `account_id`, `provider_account_id`: string | `RequestSpotifyAccount` → App |
| `MSG_PLAYLISTS_CHANGED` | `account_id`: nichtleerer string für den Profil-/Playlist-Refresh | Erfolgreicher `RefreshSpotifyAccountPlaylists` → App → bestehende Empfänger |
| `MSG_SPOTIFY_CAPABILITIES_CHANGED` | Callback: `probe_result=true`; Broadcast: `audiobook_state`, `audiobook_mode` int32 und `audiobooks_enabled` bool | Capability-Callback → App; aktueller Snapshot → Fenster |
| `MSG_AUTH_COMPLETE` | Token-Teil: `ok` bool; `http_status`, `expires_in` int32; `access_token`, `refresh_token`, `scopes`, `error`, `error_description` string | Bestehende OAuth-/Refresh-Callbacks → App |

`spAc` verlangt alle aufgeführten Felder genau einmal und im richtigen Typ.
`response_valid=true` verlangt `ok=true` und eine nichtleere Account-ID; ungültige
Antworten dürfen keine Identitäten tragen. Der Reader lässt seine Ausgabe bei
Fehlern unverändert. Alte interne `spAc`-Ergebnisse ohne Request-ID werden abgewiesen;
es gibt kein persistiertes oder öffentliches Wire-Format hierfür. Die App nimmt
nur ihren aktuellen Auftrag genau einmal an. Abmelden/neue Auth-Generation und
ein neuer Profilauftrag entwerten ältere Aufträge. Der frische `/me`-Read trennt
außerdem alte und neue GET-Leser im Request-Client.

Die bestehende Profilzuordnung (`id` vor `account_id`) wird bis zum typisierten
Account-Ergebnis erhalten. Speicherung und Account-/Cache-Übergang erfolgen erst
nach gültiger, aktueller Antwort; Speicherfehler führen nicht zu einem angewendeten
Account. Playlist-Fehler erzeugen keine `MSG_PLAYLISTS_CHANGED`-Nachricht. Andere
bestehende Playlist-Sender behalten ihren Vertrag; die App prüft vorhandenen
Account-Kontext vor der Weiterleitung.

Capability-Callbacks tragen absichtlich keinen alten Snapshot. App liest beim
Empfang den aktuellen Dienstzustand. Der Dienst prüft selbst die Probe-Generation,
bevor er Zustand oder aktuelle Waiter verändert. Ein Reset kann zusätzliche
Benachrichtigungen erzeugen; sie veröffentlichen weiterhin den aktuellen Zustand.

Der Token-Reader akzeptiert fehlende optionale Felder mit bisherigen Defaults
(`ok=false`, HTTP -1, Ablauf 3600, Strings leer), lehnt doppelte/falsch typisierte
bekannte Felder ab. Erfolgreiche Speicherung verlangt dennoch Access-Token und
alle erforderlichen Scopes. Die bestehende App-Hülle mit `silent`,
`refresh_request`, `token_generation` und `operation` bleibt kompatibel. Ein
Ergebnis mit veralteter Token-Generation wird ignoriert und schließt keine
Waiter einer neueren Generation ab. Abmelden/neuer Auth-Versuch beendet alte
Waiter weiterhin ausdrücklich. Zugangsdaten gehören nicht in Diagnoseprotokolle.

Fixtures und Grenzen der noch ausstehenden nativen Abnahme:
[Phase-5-Protokoll](phase-5-verification.md).
