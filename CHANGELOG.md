# Changelog – Trucker Chat

**English** · [Deutsch](#changelog-deutsch)

## 0.1.1 Alpha – Runs together with Realistic Radio

### New
- **Works together with Realistic Radio without a second Direct3D hook.** If `realistic-radio.dll` (1.4.3 or newer) is in the plugins folder, Trucker Chat waits at start until the game has loaded it and then does not hook Direct3D itself: it hands its pictures to Realistic Radio, which draws the radio first and then the chat. Two plugins hooking the same Direct3D functions could crash the game; with one hook that is gone. The log says `Realistic Radio: linked`.
- **Same navigation area as the radio.** Linked with Realistic Radio, the chat uses the radio's truck presets (drawing surface, area, flip) and changes with the truck – the screen is set up only once. Own values: `"screen": { "followRadio": false }`.
- **The chat waits for the radio.** While Realistic Radio shows a card (station change, volume, start logo), a new message appears afterwards (`screen.radioCoexist: "yield"`, default). If the radio display is pinned on the screen, the chat is drawn on top of it.

### Changed
- **Older Realistic Radio (up to 1.4.2):** Trucker Chat shows the side chat only and does not draw on the navigation screen, so two hooks never touch the screen render targets. The log asks you to update Realistic Radio.
- The former automatic radio detection (watching `Draw`) was removed.
- Without Realistic Radio nothing changes: Trucker Chat hooks Direct3D itself as before.

### Upgrade notes
- Replace `trucker-chat.dll`. Your settings in `trucker-chat\trucker-chat.json` are kept; `screen.followRadio` is on by default.
- With Realistic Radio: update it to 1.4.3 as well.

## 0.1.0 Alpha – First release

- Live chat of your stream on the in-cab navigation screen: new **Twitch** and **YouTube** messages appear for a few seconds as a card, then the navigation comes back.
- Twitch without login; YouTube Live without API key (or with your own key from the Google Console).
- In-game menu **Ctrl + 9**, used with the mouse: chat on/off, stream link, menu key and more.
- Filters, display settings and keys in `trucker-chat.json`; only `trucker-chat.dll` is installed, everything else is created next to it.

---

<a id="changelog-deutsch"></a>
# Changelog (Deutsch)

## 0.1.1 Alpha – Läuft zusammen mit Realistic Radio

### Neu
- **Läuft zusammen mit Realistic Radio ohne zweiten Direct3D-Hook.** Liegt `realistic-radio.dll` (1.4.3 oder neuer) im Plugin-Ordner, wartet Trucker Chat beim Start, bis das Spiel sie geladen hat, und hängt sich dann nicht selbst an Direct3D: Es gibt seine Bilder an Realistic Radio weiter, das erst das Radio und dann den Chat zeichnet. Zwei Plugins an denselben Direct3D-Funktionen konnten das Spiel abstürzen lassen; mit einem Hook fällt das weg. Im Log steht `Realistic Radio: linked`.
- **Gleicher Navi-Bereich wie das Radio.** Mit Realistic Radio verbunden, übernimmt der Chat dessen LKW-Presets (Zeichenfläche, Bereich, Drehung) und wechselt mit dem LKW mit – der Bildschirm wird nur einmal eingestellt. Eigene Werte: `"screen": { "followRadio": false }`.
- **Der Chat wartet auf das Radio.** Zeigt Realistic Radio gerade eine Karte (Senderwechsel, Lautstärke, Start-Logo), erscheint eine neue Nachricht danach (`screen.radioCoexist: "yield"`, Standard). Ist die Radioanzeige dauerhaft eingeblendet, liegt der Chat darüber.

### Geändert
- **Ältere Realistic-Radio-Versionen (bis 1.4.2):** Trucker Chat zeigt nur den Seitenchat und zeichnet nicht aufs Navi, damit nie zwei Hooks in die Navi-Zeichenflächen greifen. Im Log steht ein Hinweis, Realistic Radio zu aktualisieren.
- Die frühere automatische Radio-Erkennung (Beobachtung von `Draw`) wurde entfernt.
- Ohne Realistic Radio ändert sich nichts: Trucker Chat hängt sich wie bisher selbst an Direct3D.

### Hinweise zum Update
- `trucker-chat.dll` ersetzen. Deine Einstellungen in `trucker-chat\trucker-chat.json` bleiben; `screen.followRadio` ist standardmäßig an.
- Mit Realistic Radio: dieses ebenfalls auf 1.4.3 aktualisieren.

## 0.1.0 Alpha – Erste Veröffentlichung

- Livechat deines Streams auf dem Navi im LKW: neue **Twitch**- und **YouTube**-Nachrichten erscheinen ein paar Sekunden als Karte, danach läuft das Navi weiter.
- Twitch ohne Login; YouTube Live ohne API-Key (oder mit eigenem Key aus der Google Console).
- Menü im Spiel mit **Strg + 9**, mit der Maus bedienbar: Chat an/aus, Stream-Link, Menü-Taste und mehr.
- Filter, Anzeige und Tasten in `trucker-chat.json`; installiert wird nur `trucker-chat.dll`, alles andere legt das Plugin daneben an.
