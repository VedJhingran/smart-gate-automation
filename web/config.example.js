// Copy to config.local.js and replace every placeholder locally.
// config.local.js is intentionally ignored by Git.
window.SMART_GATE_CONFIG = {
  databaseUrl: "https://YOUR_PROJECT-default-rtdb.firebaseio.com",
  databaseAuth: "YOUR_DATABASE_SECRET_OR_TOKEN",
  firebase: {
    apiKey: "YOUR_WEB_API_KEY",
    authDomain: "YOUR_PROJECT.firebaseapp.com",
    databaseURL: "https://YOUR_PROJECT-default-rtdb.firebaseio.com",
    projectId: "YOUR_PROJECT",
    storageBucket: "YOUR_PROJECT.appspot.com",
    messagingSenderId: "YOUR_SENDER_ID",
    appId: "YOUR_APP_ID"
  },
  vapidKey: "YOUR_VAPID_KEY"
};
