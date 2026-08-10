# FSE DCU 2612 — UI Data Flow: Evolution der Architektur

Dieses Dokument zeigt drei Evolutionsstufen für den Datenfluss von einem CAN-Signal bis zum
dargestellten LVGL-Widget. Als Leitfaden dient das Hinzufügen eines neuen Signals
(`motor_rpm`) zu einem bestehenden Screen.

---

## Stufe 1 — Manuelle Drei-Domänen-Trennung

### Konzept

Jede Domäne besitzt ihre eigene Definition. Es gibt keine gemeinsame Quelle der Wahrheit.
Alle Schritte werden von Hand durchgeführt.

```
┌─────────────────────────────────────────────────────────────────┐
│  CAN-Domäne          App-Domäne           UI-Domäne             │
│  ──────────          ──────────           ─────────             │
│  can_signals.h  →    screen_rtd_data  →   screen_rtd.c          │
│  can_data_            App-Mapping          Widget-Layout        │
│  snapshot                                 update()-Funktion     │
└─────────────────────────────────────────────────────────────────┘
```

---

### Workflow: Neues Signal hinzufügen

**Schritt 1 — CAN-Domäne: Dekodiermakros** (`modules/can/can_signals.h`)

```c
#define CAN_MSG_MOTOR_STATUS_ID      0x201U
#define CAN_SIG_MOTOR_RPM_START      16
#define CAN_SIG_MOTOR_RPM_LEN        16
#define CAN_SIG_MOTOR_RPM_FACTOR     0.1f
#define CAN_SIG_MOTOR_RPM_OFFSET     0.0f
```

**Schritt 2 — CAN-Domäne: Datenfeld im Snapshot** (`services/event_bus/events.h`)

```c
struct can_data_snapshot {
    float motor_rpm;      // ← neu
    float motor_temp_c;
    float hv_soc_pct;
    // ...
};
```

**Schritt 3 — CAN-Domäne: Signal dekodieren** (`modules/can/can.c`)

```c
static void can_decode_motor_status(const struct can_frame *frame,
                                    struct can_data_snapshot *out)
{
    uint16_t raw = (frame->data[2] << 8) | frame->data[3];
    out->motor_rpm = raw * CAN_SIG_MOTOR_RPM_FACTOR + CAN_SIG_MOTOR_RPM_OFFSET;
}
```

**Schritt 4 — App-Domäne: Feld im Screen-Struct** (`services/event_bus/events.h`)

```c
struct screen_rtd_data {
    float motor_rpm;      // ← neu
    float hv_soc_pct;
    float hv_temp_c;
};
```

**Schritt 5 — App-Domäne: Mapping-Funktion** (`src/app/app.c`)

```c
static void app_build_rtd_data(struct screen_rtd_data *out,
                               const struct can_data_snapshot *in)
{
    out->motor_rpm  = in->motor_rpm;   // ← neu
    out->hv_soc_pct = in->hv_soc_pct;
    out->hv_temp_c  = in->hv_temp_max_c;
}
```

**Schritt 6 — UI-Domäne: Widget und Datenbindung** (`src/modules/ui/screens/screen_rtd.c`)

```c
static lv_obj_t *s_rpm_arc;

void screen_rtd_create(void) {
    s_rpm_arc = lv_arc_create(screen);
    lv_arc_set_range(s_rpm_arc, 0, 10000);
    lv_obj_set_pos(s_rpm_arc, 10, 20);
    // ...
}

void screen_rtd_update(const struct screen_rtd_data *data) {
    lv_arc_set_value(s_rpm_arc, (int32_t)data->motor_rpm);  // ← neu
    lv_bar_set_value(s_soc_bar, (int32_t)data->hv_soc_pct, LV_ANIM_OFF);
}
```

---

### Bewertung

| Aspekt | Bewertung |
|--------|-----------|
| Schritte pro neuem Signal | **6** |
| Berührte Dateien | `can_signals.h`, `events.h` (2×), `can.c`, `app.c`, `screen_rtd.c` |
| Fehlerquellen | Mapping vergessen, Feld in falschem Struct, falscher Faktor |
| Kopplung | UI-Modul kennt `can_data_snapshot` nicht; saubere Domänentrennung |
| Tooling | Keines notwendig |

---

---

## Stufe 2 — Optimierung der Klebschicht

Die Schritte 1–5 aus Stufe 1 enthalten Boilerplate, das mechanisch aus einer einzigen
Signaldefinition ableitbar ist. Zwei Ansätze schaffen eine gemeinsame Quelle der Wahrheit.

---

### Option 2a — X-Macro Signal Table

Alle Signalmetadaten stehen in einer einzigen Datei `signals.def`. Per X-Macro werden
`can_data_snapshot`, `screen_X_data` und die Mapping-Funktion automatisch generiert.

**`signals.def`** — einzige Quelle der Wahrheit:

```c
//  CAN-ID   Feldname        Typ    Faktor   Offset   Screens
X(0x201, motor_rpm,      float, 0.1f,   0.0f,   SCR_RTD | SCR_DEBUG)
X(0x201, motor_temp_c,   float, 0.5f,  -40.0f,  SCR_RTD)
X(0x302, hv_soc_pct,     float, 0.1f,   0.0f,   SCR_RTD)
X(0x303, hv_temp_max_c,  float, 0.5f,  -40.0f,  SCR_RTD)
X(0x401, lv_voltage_v,   float, 0.01f,  0.0f,   SCR_RTD | SCR_DEBUG)
```

**Automatisch generierte Strukturen:**

```c
// can_data_snapshot — alle Felder aus signals.def
struct can_data_snapshot {
#define X(id, field, type, f, o, scr) type field;
#include "signals.def"
#undef X
};

// screen_rtd_data — nur SCR_RTD-Felder
struct screen_rtd_data {
#define X(id, field, type, f, o, scr)  \
    COND_CODE_1(IS_ENABLED(scr & SCR_RTD), (type field;), ())
#include "signals.def"
#undef X
};

// Mapping-Funktion — auto-generiert
static void app_build_rtd_data(struct screen_rtd_data *out,
                               const struct can_data_snapshot *in) {
#define X(id, field, type, f, o, scr)  \
    COND_CODE_1(IS_ENABLED(scr & SCR_RTD), (out->field = in->field;), ())
#include "signals.def"
#undef X
}
```

**Workflow: Neues Signal hinzufügen:**

```
1. Eine Zeile in signals.def                → Struct-Felder + Mapping auto-generiert
2. Dekodierkode in can.c ergänzen           → CAN-Domäne
3. Widget in screen_X.c anlegen + binden    → UI-Domäne
```

| Aspekt | Bewertung |
|--------|-----------|
| Schritte pro neuem Signal | **3** |
| Berührte Dateien | `signals.def`, `can.c`, `screen_rtd.c` |
| Tooling | Keines (reines C, Compile-time) |
| Lesbarkeit | Mittel — X-Macros sind ungewohnt, IDE-Unterstützung eingeschränkt |
| Fehlerquellen | Bit-Position und Faktor in `signals.def` noch manuell; Macro-Fehler schwer zu debuggen |

---

### Option 2b — Code-Generierung aus DBC

Ein Python-Skript läuft als West-Pre-Build-Step und generiert aus der `.dbc`-Datei
C-Code für alle Signale. Die DBC-Datei ist die einzige Quelle der Wahrheit.

**Build-Integration (`CMakeLists.txt`):**

```cmake
find_package(Python3 REQUIRED)

add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/can_generated.h
            ${CMAKE_CURRENT_BINARY_DIR}/can_generated.c
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_can.py
            --dbc  ${CMAKE_CURRENT_SOURCE_DIR}/vehicle.dbc
            --out  ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/vehicle.dbc
    COMMENT "Generating CAN signal code from DBC"
)
```

**Generierter Output (Ausschnitt):**

```c
// can_generated.h — vollständig aus vehicle.dbc
#define CAN_MSG_MOTOR_STATUS_ID     0x201U
#define CAN_SIG_MOTOR_RPM_START     16
#define CAN_SIG_MOTOR_RPM_LEN       16
#define CAN_SIG_MOTOR_RPM_FACTOR    0.1f
#define CAN_SIG_MOTOR_RPM_OFFSET    0.0f

struct can_data_snapshot {
    float motor_rpm;
    float motor_temp_c;
    float hv_soc_pct;
    // ... alle Signale aus der DBC
};

void can_decode_frame(const struct can_frame *f, struct can_data_snapshot *out);
```

**Workflow: Neues Signal hinzufügen:**

```
1. Signal in vehicle.dbc eintragen          → Build generiert Makros + Struct + Decode
2. Widget in screen_X.c anlegen + binden    → UI-Domäne
   (App-Mapping muss noch manuell ergänzt   → oder ebenfalls generieren)
```

| Aspekt | Bewertung |
|--------|-----------|
| Schritte pro neuem Signal | **2** (+ optionales App-Mapping) |
| Berührte Dateien | `vehicle.dbc`, `screen_rtd.c` |
| Tooling | Python, cantools, Jinja2 — Build-Toolchain aufwändiger |
| Lesbarkeit | Hoch — DBC ist etabliertes Automotive-Format |
| Fehlerquellen | Bitfehler in Signalen praktisch ausgeschlossen; generierter Code schwer zu debuggen |
| Empfehlung wenn | DBC wird teamweit gepflegt; CI/CD vorhanden |

---

---

## Stufe 3 — LVGL Subject/Observer Pattern

### Konzept

LVGL Subjects (`lv_subject_t`, verfügbar ab LVGL 9.2) sind beobachtbare Werte.
Widgets binden sich beim Erstellen an einen Subject und aktualisieren sich automatisch,
wenn sich der Wert ändert. Die `screen_X_update()`-Funktion entfällt vollständig.

**Entscheidende Architekturgrenze:** Der App Layer ruft **nicht** `lv_subject_set_*()`
auf. Subjects sind ein Implementierungsdetail des UI-Moduls und nach außen unsichtbar.
Die Modulgrenze zwischen App und UI bleibt Zbus — das UI-Modul empfängt Daten über
`ui_cmd_chan` und übersetzt sie intern in Subject-Updates.

```
┌──────────────────────────────────────────────────────────────────────┐
│  App Layer                                                           │
│  Zbus: ui_cmd_chan {can_data_snapshot}   ← kein #include <lvgl.h>    │
└─────────────────────────────┬────────────────────────────────────────┘
                              │ Zbus (Modulgrenze)
┌─────────────────────────────▼────────────────────────────────────────┐
│  UI-Modul (intern)                                                   │
│  ui_handle_cmd()                                                     │
│    lv_subject_set_float(&s_subj_motor_rpm, data->motor_rpm)          │
│    lv_subject_set_float(&s_subj_hv_soc_pct, data->hv_soc_pct)        │
│                             │                                        │
│  screen_rtd_create()        ▼                                        │
│    lv_subject_add_observer_obj(&s_subj_motor_rpm, arc, cb, NULL)     │
│    lv_subject_add_observer_obj(&s_subj_hv_soc_pct, bar, cb, NULL)    │
└──────────────────────────────────────────────────────────────────────┘
                              │ lv_subject_set_* → Observer-Callbacks
┌─────────────────────────────▼────────────────────────────────────────┐
│  Widgets (LVGL-intern)                                               │
│  Arc, Bar, Label aktualisieren sich selbst                           │
└──────────────────────────────────────────────────────────────────────┘
```

---

### Implementierung

**Subjects: `static`, vollständig privat** (`src/modules/ui/ui_data_model.c`)

```c
// Kein öffentlicher Header — nur intern sichtbar
static lv_subject_t s_subj_motor_rpm;
static lv_subject_t s_subj_hv_soc_pct;
static lv_subject_t s_subj_hv_temp_c;
static lv_subject_t s_subj_lv_voltage;

void ui_data_model_init(void) {
    lv_subject_init_float(&s_subj_motor_rpm,   0.0f);
    lv_subject_init_float(&s_subj_hv_soc_pct,  0.0f);
    lv_subject_init_float(&s_subj_hv_temp_c,   0.0f);
    lv_subject_init_float(&s_subj_lv_voltage,   0.0f);
}

// Interner Update — nur von ui.c aufgerufen
void ui_data_model_update(const struct can_data_snapshot *d) {
    lv_subject_set_float(&s_subj_motor_rpm,   d->motor_rpm);
    lv_subject_set_float(&s_subj_hv_soc_pct,  d->hv_soc_pct);
    lv_subject_set_float(&s_subj_hv_temp_c,   d->hv_temp_max_c);
    lv_subject_set_float(&s_subj_lv_voltage,   d->lv_voltage_v);
}
```

**UI-Modul: Zbus-Handler ohne LVGL-Abhängigkeit nach außen** (`src/modules/ui/ui.c`)

```c
// ui.c kennt <lvgl.h> — das ist sein Job
// App Layer kennt <lvgl.h> NICHT — das ist die Grenze

static void ui_handle_cmd(const struct ui_cmd *cmd) {
    switch (cmd->type) {
    case UI_CMD_SET_SCREEN:
        ui_navigate_to(cmd->data.screen);
        break;
    case UI_CMD_UPDATE_DATA:
        ui_data_model_update(&cmd->data.snapshot);  // intern
        break;
    case UI_CMD_SET_STATUS:
        ui_apply_status(&cmd->data.status);
        break;
    }
}
```

**Screen: Widget binden, kein Update-Callback mehr nötig** (`src/modules/ui/screens/screen_rtd.c`)

```c
// Intern: Zugriff auf Subjects über ui_data_model_internal.h
#include "../ui_data_model_internal.h"

void screen_rtd_create(void) {
    // Arc für Motor-RPM
    lv_obj_t *rpm_arc = lv_arc_create(screen);
    lv_arc_set_range(rpm_arc, 0, 10000);
    lv_subject_add_observer_obj(&s_subj_motor_rpm, rpm_arc,
        [](lv_observer_t *obs, lv_subject_t *subj) {
            lv_arc_set_value(lv_observer_get_target(obs),
                             (int32_t)lv_subject_get_float(subj));
        }, NULL);

    // Progress-Bar für HV SoC
    lv_obj_t *soc_bar = lv_bar_create(screen);
    lv_label_bind_text(lv_label_create(screen), &s_subj_hv_soc_pct, "%.0f%%");
}
// screen_rtd_update() existiert nicht mehr
```

Wird der Screen gelöscht, entfernt LVGL alle Observer automatisch.
Inaktive Screens haben keine Widgets — Subject-Updates haben keinen Effekt.

---

### `ui_cmd_chan`: Vereinfachung durch Wegfall screen-spezifischer Structs

Da der App Layer keinen Mapping-Struct mehr befüllt, entfällt die Notwendigkeit für
`screen_rtd_data`, `screen_debug_data` etc. Der volle `can_data_snapshot` wird direkt
übertragen:

```c
// events.h — vereinfacht gegenüber Stufe 1
enum ui_cmd_type {
    UI_CMD_SET_SCREEN,
    UI_CMD_UPDATE_DATA,   // ← ein einziger Update-Command für alle Screens
    UI_CMD_SET_STATUS,
};

struct ui_cmd {
    enum ui_cmd_type type;
    union {
        enum screen_id           screen;
        struct can_data_snapshot snapshot;  // voller Snapshot, keine screen-Ableitung
        struct ui_status_flags   status;
    } data;
};
```

---

### Workflow: Neues Signal hinzufügen

```
1. can_signals.h              Dekodiermakros                  [CAN-Domäne]
2. can_data_snapshot          float wheel_speed hinzufügen    [events.h]
3. can.c                      Signal dekodieren               [CAN-Domäne]
── App Layer: keine Änderung ────────────────────────────────────────────
4. ui_data_model.c            lv_subject_t + set_float()      [UI-intern, 2 Zeilen]
5. screen_X.c                 Widget anlegen + Observer       [Screen-Datei]
```

Schritte 1–3 lassen sich mit **Option 2a** (X-Macro) oder **Option 2b** (DBC Codegen)
auf eine einzige Aktion reduzieren.

---

### Bewertung

| Aspekt | Bewertung |
|--------|-----------|
| Schritte pro neuem Signal | **5** (davon 1–3 automatisierbar → effektiv **2**) |
| Berührte Dateien | `can_signals.h` + `events.h` + `can.c` (auto), `ui_data_model.c`, `screen_X.c` |
| App Layer Änderungen | **Keine** |
| LVGL-Abhängigkeit im App Layer | **Keine** — Modulgrenze bleibt Zbus |
| LVGL-Mindestversion | 9.2 |
| `screen_X_update()` | Entfällt |
| Screen-spezifische Datenstructs | Entfallen |

---

---

## Gesamtvergleich

| | Stufe 1 | Stufe 2a (X-Macro) | Stufe 2b (DBC Codegen) | Stufe 3 (Subjects) |
|---|---|---|---|---|
| Schritte gesamt | **6** | **3** | **2** | **5 → 2*** |
| App Layer Änderung | Ja | Ja (auto) | Ja (auto) | **Nein** |
| Toolchain-Aufwand | Keiner | Keiner | Python + cantools | Keiner |
| LVGL-Mindestversion | beliebig | beliebig | beliebig | **9.2** |
| `screen_X_update()` | Manuell | Manuell | Manuell | **Entfällt** |
| Screen-Structs | Notwendig | Notwendig | Notwendig | **Entfallen** |
| Lesbarkeit | Hoch | Mittel | Hoch | Hoch |
| Empfehlung | Basis | Kein DBC-Tooling | CI vorhanden | LVGL ≥ 9.2 ✓ |

\* Schritte 1–3 automatisierbar durch Kombination mit Stufe 2

---

## Empfehlung für dieses Projekt

Da LVGL 9.5 eingesetzt wird, ist **Stufe 3 kombiniert mit Option 2a (X-Macro)** die
empfohlene Lösung:

```
signals.def  →  (X-Macro)  →  Snapshot-Struct + Decode
                               App Layer: keine Änderung
                               ui_data_model.c: 2 Zeilen
                               screen_X.c: Widget + Observer
```

Einzige manuelle Schritte pro neuem Signal:
1. Eine Zeile in `signals.def` (Feldname, CAN-ID, Faktor, Offset, Screens)
2. Widget in der zugehörigen Screen-Datei anlegen und an Subject binden
