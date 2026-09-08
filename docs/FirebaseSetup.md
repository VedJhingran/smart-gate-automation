# Firebase setup

## 1. Create a Firebase project

Create a Firebase project, enable Realtime Database, and register a web app. Keep project-specific values in local configuration files; only publish safe examples.

## 2. Database paths used by the reference application

```text
/gate/status
/gate/command
/gate/events
/gate/eventLog
/gate/dailyOpens/{YYYY-MM-DD}
/gate/cameraSnapshot
/gate/deviceTokens/{sanitized-token}
```

The ESP32 reads commands and writes status. The PWA writes commands and reads status/logs/snapshots. Cloud functions may use `events` and device tokens to deliver notifications.

## 3. Secure the database

Start from [firebase/database.rules.json](../firebase/database.rules.json), then adapt its authentication claims to your application. Do not deploy permissive rules to production. The reference legacy firmware uses a database secret in HTTP URLs; replace this design with authenticated, scoped access or a secure gateway before exposing the system beyond a private network.

## 4. Configure local values

- Copy `.env.example` to `.env` outside version control.
- Fill `web/config.local.js` locally from `web/config.example.js`.
- Fill `firmware/include/Config.h` locally from `Config.h.example`.
- Restrict API keys in Google Cloud/Firebase where supported.
- For push notifications, create a Web Push certificate and place its VAPID public key in local web configuration.

## 5. Deploy the PWA

From `web/`, install the declared dependencies, authenticate with Firebase CLI, configure a local project alias, and deploy Hosting. Keep `.firebaserc` private if it identifies a personal project.

## 6. Verify

Confirm that unauthenticated requests are denied, the PWA can only access intended paths, command writes are auditable, and revoked tokens immediately lose access.
