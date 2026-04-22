# Android App Wrapper fuer DPW

Dieser Ordner enthaelt eine native Android-App, die eure Web-App in einer `WebView` laedt.

## Features

- Laedt die DPW-Web-App ueber `BuildConfig.WEB_APP_URL`
- Bleibt in der App fuer Links auf derselben Domain
- Oeffnet externe HTTP(S)-Links im System-Browser
- Unterstuetzt Zurueck-Navigation innerhalb der WebView
- Zeigt einen Ladebalken waehrend Seitenaufbau

## Voraussetzungen

- Android Studio Iguana oder neuer
- Android SDK Platform 34
- JDK 17

## URL konfigurieren

Standardwert ist in [app/build.gradle.kts](app/build.gradle.kts) gesetzt:

- `https://dpw.example.org`

Zum Ueberschreiben z. B. fuer lokale oder Stage-Umgebung:

1. In `~/.gradle/gradle.properties`:

```properties
dpwWebUrl=https://dpw.example.org
```

2. Oder als Gradle-Property im Android-Studio-Run/Build-Profil:

	-PdpwWebUrl=https://dpw.example.org

## Starten

1. Android Studio oeffnen.
2. Ordner `android-app` als Projekt importieren.
3. Sync ausfuehren.
4. `app` auf Emulator oder Geraet starten.
