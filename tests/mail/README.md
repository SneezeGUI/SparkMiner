# Offline-Mailtests

Diese Tests öffnen keine Netzverbindung und verschicken keine Nachrichten. Sie verwenden den produktiven SMTP-/Zeitplan-/Warncode, den tatsächlichen Software-SHA-Referenzcode und die produktiven Einstellungs-Handler mit lokalen Schnittstellen-Doubles.

Vom Projektverzeichnis aus, mit Python 3 und `ziglang==0.13.0`:

```
python tests/mail/run_tests.py
```

Assertions bleiben durch `-UNDEBUG` aktiv. Zwei bereits in der bestehenden Software-SHA-Datei vorhandene Clang-Warnungen zu Makroklammerung und ungenutzten Variablen sind gezielt ausgenommen; alle anderen Warnungen gelten als Fehler. Die Tests schreiben nur in `.test-output/` und `.test-cache/`.

Für den Browsertest sind Node.js, Playwright und Chrome erforderlich. Nach den C++-Tests vom selben Projektverzeichnis aus:

```
node tests/mail/test_mail_browser.cjs
```

Playwright muss für Node auffindbar sein (z. B. über `NODE_PATH`). Standard-Chrome-Pfad ist die macOS-Installation unter `/Applications/Google Chrome.app/Contents/MacOS/Google Chrome`. Sämtliche HTTP-Antworten werden lokal simuliert; SMTP-Testaktionen senden nur an diese Simulation. Getestet werden vier Passwortschalter, kennwortfreier Export auch bei sichtbaren Eingaben, atomare Importablehnung, ausdrücklicher Testmail-Klick, getrennte Prüfung ohne Mail und schmale/breite Layouts.

Die echte Zertifikatsprüfung, Hardware-Timings und Mining-Fortsetzung müssen zusätzlich am ESP32 geprüft werden. Die entsprechende Ergebnisdatei im Ausgabepaket trennt die Verbindungsprüfung ohne Anmeldung von einer tatsächlich versendeten Mail.
