# Plan: Forms UI neu strukturieren

## Ziele (vom User)

1. **Formular als eigene Seite pro Aktivität** — wie der Mail-Composer. Button oben
   im Header der Aktivität, neben "📧 Mail" und "✏️ Bearbeiten".
2. **Öffentliche Formular-Seite ohne Nav-Leiste & ohne Login** — `/forms/:activityId`
   darf keine globale Navigation zeigen und soll auch ohne Anmeldung funktionieren.
3. **Formular-Vorlagen pro Stufe** — analog zu "Mail-Vorlagen": ein eigener Tab
   (globaler Nav-Link), in dem pro Abteilung (Stufe) eine Basis-Vorlage definiert
   werden kann (welche Felder, Typen, Pflichtfelder usw.).

## Stand heute

- Backend für Forms + Templates ist fertig (14 Routen, inkl. öffentliche
  `GET /forms/:id` und `POST /forms/:id/submit`).
- `ActivityFormsPage.vue` existiert, wird aber als **eingebetteter** Block via
  `<details>` in `DetailPage.vue` angezeigt (nicht als eigene Seite).
- `FormPublicPage.vue` + Route `/forms/:activityId` existieren, aber:
  - Die globale Nav-Leiste (`App.vue`) rendert auf **allen** Seiten.
  - Der Login-Overlay (`v-else-if="!user"` in `App.vue`) blockiert jede Seite
    für nicht angemeldete Benutzer — auch die als `meta.public` markierte Route.
- Für Form-Vorlagen existiert bereits `useFormTemplates()` Composable + DB-Tabelle
  `form_templates (id, name, department, form_type, template_config JSONB, …)`.
  Es fehlt nur die UI.

## Scope der Änderungen

### Frontend

| Datei | Änderung |
|---|---|
| `router/index.ts` | neue Routen `/activities/:id/forms`, `/form-templates`; `/forms/:activityId` bleibt mit `meta.public: true` |
| `App.vue` | Nav-Leiste **und** Login-Overlay via `route.meta.public` überspringen; zusätzlich Nav-Link "Formular-Vorlagen" |
| `pages/ActivityFormsPage.vue` | zu eigenständiger Seite umbauen (Header mit Zurück-Button, nutzt `route.params.id`, nicht mehr `defineProps`) |
| `pages/DetailPage.vue` | Button "📋 Formular" im Header neben Mail/Bearbeiten; eingebetteter `<details>`-Block entfernen; ungenutzten Import entfernen |
| `pages/FormTemplatePage.vue` (neu) | Seite mit Stufen-Tabs, pro Stufe Liste von Vorlagen + FormBuilder, analog zu `MailTemplatePage` |
| `components/FormBuilder.vue` | optional: "Aus Vorlage laden"-Dropdown (wenn Activity → Stufe bekannt); neuer Prop `department?` |
| `components/FormBuilder.vue` | zweite Rolle als Template-Editor (kein Mode-Toggle wenn `hideModeToggle` / alternativ reines `v-model` auf Questions) |

### Backend / DB

Keine Änderungen nötig — Endpoints existieren schon:
- `GET /api/form-templates?department=X`
- `POST /api/form-templates`
- `PUT /api/form-templates/:id`
- `DELETE /api/form-templates/:id`

### Permissions

- Form-Template-Page verwendet denselben Scope wie Mail-Templates
  (`mail_templates_scope`): `'all'` darf alle Stufen, `'own_dept'` nur die eigene.
  Alternativ eine neue `forms_templates_scope` einführen — vorerst reuse, um Schema
  stabil zu halten (konsistent mit der Entscheidung aus der ersten Iteration).
- "📋 Formular"-Button auf DetailPage sichtbar, wenn Benutzer `can_read_dept`
  für die Aktivitäts-Abteilung hat (genauso wie bisher bei der eingebetteten View).

## Implementierungs-Reihenfolge

1. `App.vue` + `router/index.ts`: Public-Route ohne Nav/Login funktional machen.
2. `ActivityFormsPage` zu eigener Seite (Route `/activities/:id/forms`).
3. `DetailPage`: Header-Button, eingebetteten Block entfernen.
4. `FormTemplatePage` + Nav-Link.
5. `FormBuilder`: "Aus Vorlage laden" (wenn Activity-Stufe bekannt).
6. Build-Check.

## Nicht in diesem Schritt

- WebSocket-Live-Collab für Form-Vorlagen (Mail-Templates nutzen es; hier bewusst
  erst mal nicht, weil FormBuilder strukturierte Listen hat und Merge-Strategie
  anders wäre).
- Neue Permission-Spalte in DB.
- Statistik-/Antworten-Teil bleibt unverändert (ist bereits in ActivityFormsPage).
