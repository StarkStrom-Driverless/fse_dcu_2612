# Benutzerhandbuch {#manual-user}

Im Benutzerhandbuch wird die Bedienung der DCU für Fahrer und Ingenieur erklärt. 

## Bedienkonzept (Taster und Drehencoder)

Die DCU ist im Lenkrad direkt verbaut. Für die Eingabe stehen 4 Taster und 2 Drehencoder zu Verfügung. Für die Ausgabe gibt es ein 3,5" Farbdisplay, einen Piezo Piepser, sowie einen LED Streifen mit 29 LEDs. 

Taster
- Links
- Rechts
- Ready to Drive (RTD)
- Timestamp (TS)

Drehencoder
- Linker Drehencoder
- Rechter Drehencoder

## Menüführung

Sobald die Stromzufuhr hergestellt ist fährt die DCU hoch und zeigt den Boot Screen an. Vom Boot Screen aus Links gesehen sind alle Debug relevanten Screens, Rechts von dem Boot Screen sind die für EV und DV relevanten Screens vorhanden. In @Figure ist eine Übersicht abgebildet, wo alle vorhandenen Screens aufgelistet sind. 

Mit dem linken Drehencoder kann zwischen den Screen geblättert werden. 

## Debugscreens

### DBG TX
In diesem Screen kann das `Debug_SETTING` Signal der `DCU_2_mABX` Message gesetzt werden, welcher an die MABX gesendet wird. Mit dem rechten Drehencoder kann der Wert zwischen 0 und 7 gewählt werden und mit dem rechten Taster wird die Auswahl bestätigt. 

### DBG TS

### DBG PRESSURE

### DBG HV ACCU

### DBG LV ACCU

## Driving Sscreens

### DV MISSION
Im 

### PRE RTD

### EV DRIVING
Der EV Screen wird automatisch von der DCU angezeigt, sobald RTD über die DCU aktiviert wird. In diesem Screen kann nicht mehr zu anderen Screens gewechselt werden, um Fehleingaben zu verhindern. In diesem Modus haben die Drehencoder Links und Rechts, sowie die Taster Links und Rechts eine fest zugewiesenes Funktion. 

Linker Drehencoder: Torque Vectoring Front
Rechter Drehencoder: Torque Vectroing Rear
Linker Taster: Power Limit
Rechter Taster: Torque Vectoring



## Sonstige Screens

### Settings

### Boot
Erster Screen nach dem booten, zeigt die aktuelle Software Version der DCU an. 