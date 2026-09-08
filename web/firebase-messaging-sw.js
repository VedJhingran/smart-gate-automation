/* Firebase Cloud Messaging service worker. Configuration is loaded from the
 * ignored web/config.local.js file so no project values live in this file. */
importScripts('config.local.js');
importScripts('https://www.gstatic.com/firebasejs/10.13.0/firebase-app-compat.js');
importScripts('https://www.gstatic.com/firebasejs/10.13.0/firebase-messaging-compat.js');

if (self.SMART_GATE_CONFIG && self.SMART_GATE_CONFIG.firebase) {
  firebase.initializeApp(self.SMART_GATE_CONFIG.firebase);
  firebase.messaging();
}
