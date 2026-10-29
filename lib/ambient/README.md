Ambient synthesis engine and shared parameter validation copied from the user's
`/home/roh/code/amb-gen-esp32/lib/ambient/src` project. The engine runs at 32 kHz
and owns its bounded voice pool, filtering, reverb, limiting, and fades.

All engine calls belong to the audio task. Web updates reach it through a
single-element FreeRTOS queue containing complete settings snapshots.
