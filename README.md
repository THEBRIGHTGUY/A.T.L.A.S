# A.T.L.A.S. Field OS

A small, installable field app for the A.T.L.A.S. agri-IoT stack. It is the
companion to the Heltec Wireless Tracker brain and the 7-in-1 soil nodes: soil
readings, tank level, local weather and careful AI suggestions in one screen.

The marketing site stays at the repository root. This folder is the app.

## Run it

From the repository root:

```powershell
python -m http.server 8080
```

Then open <http://localhost:8080/app/>.

A local server is required for the camera QR scanner, geolocation and the
service worker. Opening `index.html` directly still renders, but those browser
features stay off.

## What is inside

| File | Purpose |
| --- | --- |
| `index.html` | App shell and icon sprite |
| `assets/app.css` | Dark green-blue theme, light theme, responsive layout |
| `assets/i18n.js` | English, हिन्दी and తెలుగు strings |
| `assets/app.js` | State, storage, demo seed data, action helpers |
| `assets/app-ui.js` | Views, chat, QR pairing, Settings, weather + EdgeHub adapters |
| `assets/config.js` | Deployment defaults (weather key, EdgeHub URL/token) |
| `assets/qr.js` | Vendored MIT QR generator (pairing labels) |
| `manifest.webmanifest`, `sw.js`, `icons/` | PWA install + offline shell |

There is no build step and no framework. The app is plain HTML, CSS and
JavaScript, and keeps a single state object in `localStorage` under
`atlas.fieldos.v1`.

## Data flow

```
7-in-1 soil node ──LoRa──▶ Heltec Wireless Tracker ──▶ EdgeHub ──▶ this app
tank level sensor ─LoRa──▶                           │
                                                     └──▶ AI insights / actions
```

The app never wakes a sensor and never polls in the background. Readings are
fetched when the app opens and when you tap **Refetch readings**. There are no
intervals anywhere in the code.

### EdgeHub contract

Set the base URL and optional bearer token in **Settings → Data & sync**.

```
GET  {base}/api/v1/health
GET  {base}/api/v1/telemetry/latest?nodeId=ATL-N-01
POST {base}/api/v1/assistant
```

`telemetry/latest` may return the telemetry object directly, or wrap it in
`data`, `telemetry`, `payload`, `nodes[]` or `data[]`. Field names are matched
loosely (camelCase, snake_case and common aliases), so a payload like this works:

```json
{
  "nodeId": "ATL-N-01",
  "soil": {
    "moisture": 38,
    "temperature": 27.4,
    "ph": 6.5,
    "ec": 412,
    "nitrogen": 42,
    "phosphorus": 28,
    "potassium": 165
  },
  "battery": 87,
  "rssi": -87,
  "timestamp": 1761400000
}
```

Tank telemetry accepts `level` / `levelPct` / `percent`, `capacity`,
`distance` and `temperature`.

`POST /api/v1/assistant` takes `{ message, nodeId, lang, context }` and may
return `{ "reply": "…", "action": { "type": "irrigation", "kind": "deferred" } }`.
When no endpoint is configured, or it cannot be reached, the assistant answers
locally from the last readings and labels the reply as on-device AI.

## Weather

Live weather uses the Google Weather API. Add a key and a location in
**Settings → Location**, or tap **Use my location**. Requests go to:

```
https://weather.googleapis.com/v1/currentConditions:lookup
https://weather.googleapis.com/v1/forecast/days:lookup
```

If the key, coordinates or network are missing, the app shows clearly labelled
sample weather. The key is stored only on the device.

Restrict the key to the Weather API and to your app's HTTP referrer before
deploying. For a private field deployment, put the calls behind your own proxy
and change the two URLs in `refreshWeather()` in `assets/app-ui.js`.

## Pairing labels

Each node and tank label carries one of these payloads:

```
ATLAS:NODE:ATL-N-01:7F3K2A
ATLAS:TANK:ATL-T-01:M4Q9PD
```

JSON (`{"type":"node","id":"…","code":"…"}`) and URL query
(`?type=node&id=…&code=…`) payloads are also accepted. The Setup screen can scan
with the camera (`BarcodeDetector` + `getUserMedia`), accept a pasted code, or
show a generated QR for an already linked device.

## Configure defaults

Edit `assets/config.js` before shipping:

```js
window.ATLAS_CONFIG = {
  weatherApiKey: "",
  edgeHubUrl: "",
  edgeHubToken: ""
};
```

Defaults apply on a fresh install or after **Settings → Reset**. Values entered
in the app override them and stay on the device.

## Language and theme

English, Hindi and Telugu are complete dictionaries. Dark is the default theme;
**Settings → Appearance** switches to a warm light theme. Both remember the
choice locally, and the theme is applied before first paint to avoid a flash.

## Offline and updates

`sw.js` precaches the app shell. Telemetry and weather are never cached, so a
stale reading is never shown as live. **Settings → App → Check for update**
asks the service worker for a new build and reloads when one is ready.

## Notes

- Demo mode is intentional: with no EdgeHub URL the app ships believable
  readings so the interface can be reviewed offline. Everything demo is marked
  **Demo** or **Sample weather**.
- AI suggestions never execute on their own. Tapping **Log action** records the
  plan; irrigation remains manual. Per-action switches and a master pause live
  in Settings.
- `assets/qr.js` is `qrcode-generator` 1.4.4 (MIT).
