# MV CHOIR//LAB  v2.2  (Manana Vemos)

Tres plugins VST3. Los dos primeros comparten el mismo motor de voces naturales (cambio de tono conservando los formantes):

1. **MV CHOIR LAB** (EFECTO): se carga en un inserto del mezclador con una voz real. Arma el coro con 10 voces
   (MAIN, HARMONY 1-4, DOUBLER 1-2, TEXTURE, GHOST, FX LAYER) y exporta stems.
2. **MV CHOIR LAB KEYS** (INSTRUMENTO): cargas una grabacion tuya de "aaah" y la tocas con el teclado MIDI.
   Una tecla = un acorde (estilo Omnichord) o cada tecla = una nota. Salidas separadas: bajo, tenor, alto, soprano.

3. **MV MAP** (EFECTO): mapa 2D para mezclar los canales de un bus. Ver abajo.

## MV MAP: mapa 2D de mezcla
FL no deja que un plugin vea cada canal por separado (en el bus llega todo ya sumado), asi que es un solo plugin
con dos modos, como el Visual Mixer de iZotope:
1. Pone **MV MAP** en cada canal que quieras mezclar, en modo **RELAY** (viene asi por defecto). Escribile un nombre.
2. Pone otro **MV MAP** en el bus (la pista donde se juntan esos canales) y cambialo a modo **MAP**.
3. En el mapa aparece un punto por canal: arrastra a los costados = paneo, arriba/abajo = nivel (la linea marcada es 0 dB),
   rueda del mouse sobre un punto = profundidad (mas lejos y mas oscuro), doble click = mute.
   El tamano del punto sigue el volumen en vivo. Los valores quedan guardados en cada canal y se pueden automatizar.
Importante: los relays y el mapa tienen que estar en el mismo FL (mismo proceso). Los canales duplicados se arreglan solos.

## Obtener los plugins (gratis, sin instalar nada)
1. Repositorio de GitHub: subi TODO el contenido de este ZIP (carpetas `Source`, `Assets`, `tools`, `.github` y
   los archivos sueltos). Si `.github` no sube: "Add file > Create new file", nombre `.github/workflows/build.yml`.
2. Pestana **Actions**: compila solo (15-20 min). Con el check verde baja `MV-CHOIR-LAB-Windows`.
   Adentro hay archivos sueltos: `MV CHOIR LAB.vst3`, `MV CHOIR LAB KEYS.vst3` y `MV MAP.vst3`.
   (Si el de KEYS no aparece, el efecto igual se compilo: avisame y mandame el error de esa pestana.)
3. Copia los archivos a `C:\Program Files\Common Files\VST3` (o a tu carpeta de plugins) y en FL:
   Options > Manage plugins > Find installed plugins.

## KEYS: como se usa
- Carga una grabacion (boton LOAD VOICE o arrastrala): una vocal sostenida, una sola voz, seca, sin reverb.
  El plugin detecta el tono solo (ROOT NOTE / ROOT FINE); si lo detecta mal, ajustalo a mano.
- MODE: CHORD = la tecla es la raiz de un acorde (12 tipos). DIRECT = cada tecla es una nota.
- STRUM, UNISON (copias por nota), ATTACK/RELEASE, DETUNE/HUMANIZE/MOVEMENT/WIDTH, BRIGHT/FORMANT, MUTATION, reverb.
- Salidas: en FL, boton del plugin > Processing/outputs: activa Bass, Tenor, Alto, Soprano y rutealas al mezclador.
- REC STEMS: graba BASS, TENOR, ALTO, SOPRANO y MIX en `Documentos\MV CHOIR LAB\Stems`.
- Latencia ~46 ms (el motor necesita mirar un poco hacia adelante). FL la compensa al reproducir; al tocar en vivo
  se nota un pequeno retraso.

## Pruebas del motor (sin JUCE)
`g++ -O2 -std=c++17 tools/test_engine.cpp -o test_engine && ./test_engine`
`g++ -O2 -std=c++17 tools/test_keys.cpp -o test_keys && ./test_keys`
`g++ -O2 -std=c++17 -pthread tools/test_map.cpp -o test_map && ./test_map`
